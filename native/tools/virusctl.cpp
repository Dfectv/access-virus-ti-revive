// virusctl - talk to the Virus TI through the TI Revive broker using the device model (test tool for R2).
//   virusctl state                       is a Virus connected?
//   virusctl set <path> <value> [part]   send a parameter change (encoded from remotedevice.xml)
//   virusctl note <note> [ms] [velocity] play a note on MIDI channel 1 (plug-in cable 2)
//   virusctl timing                      F5 calibration: notes stamped at newest ring frame + D, onset found in the ring
//   virusctl sysex <hex bytes> [ms] [file] send a SysEx on cable 2, print the SysEx replies of the next ms (default
//                                        2000) and append them to file (raw .syx)
#include "../src/access/accessfiles.h"
#include "../src/device/audioin.h"
#include "../src/device/broker.h"
extern "C" {
#include "vtiaudioshm.h"
}
#include "../src/device/usbmidi.h"
#include "../src/model/remotedevice.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <atomic>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

static const uint8_t EP_PLUGIN_OUT = 0x06, CABLE_PLUGIN = 2;

int main(int argc, char **argv)
{
    std::string cmd = argc > 1 ? argv[1] : "state", err;
    vti::Broker broker;
    if (!broker.open(err)) {
        fprintf(stderr, "virusctl: %s\n", err.c_str());
        return 1;
    }
    if (cmd == "state") {
        printf("Virus %s\n", broker.device_present() ? "connected" : "not connected");
        return broker.device_present() ? 0 : 1;
    }
    if (!broker.device_present()) {
        fprintf(stderr, "virusctl: no Virus (another program may own it)\n");
        return 1;
    }
    if (cmd == "set" && argc >= 4) {
        auto files = vti::AccessFiles::open("", err);
        vti::RemoteDevice dev;
        if (!files || !dev.load(files->get("remotedevice.xml"), err)) {
            fprintf(stderr, "virusctl: %s\n", err.c_str());
            return 1;
        }
        auto msg = dev.encode(argv[2], atoi(argv[3]), argc > 4 ? atoi(argv[4]) : 0);
        if (msg.empty()) {
            fprintf(stderr, "virusctl: cannot encode %s\n", argv[2]);
            return 1;
        }
        auto pk = vti::usbmidi_pack(msg, CABLE_PLUGIN);
        bool ok = broker.write(EP_PLUGIN_OUT, pk.data(), pk.size());
        for (auto b : msg)
            printf("%02x ", b);
        printf("-> %s\n", ok ? "sent" : "FAILED");
        return ok ? 0 : 1;
    }
    if (cmd == "note" && argc >= 3) {
        int n = atoi(argv[2]), ms = argc > 3 ? atoi(argv[3]) : 500, vel = argc > 4 ? atoi(argv[4]) : 100;
        auto on = vti::usbmidi_pack({0x90, (uint8_t)n, (uint8_t)vel}, CABLE_PLUGIN);
        auto off = vti::usbmidi_pack({0x80, (uint8_t)n, 0}, CABLE_PLUGIN);
        broker.write(EP_PLUGIN_OUT, on.data(), on.size());
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        broker.write(EP_PLUGIN_OUT, off.data(), off.size());
        printf("note %d %d ms\n", n, ms);
        return 0;
    }
    if (cmd == "timing") {
        if (broker.audio(true) != 1) {
            fprintf(stderr, "virusctl: no audio stream\n");
            return 1;
        }
        { // the original's start-up globals (remote mode); the Virus ignores plug-in notes without them
            auto files = vti::AccessFiles::open("", err);
            vti::RemoteDevice dev;
            if (files && dev.load(files->get("remotedevice.xml"), err))
                for (auto &m : dev.init_messages()) {
                    auto pk = vti::usbmidi_pack(m, CABLE_PLUGIN);
                    broker.write(EP_PLUGIN_OUT, pk.data(), pk.size());
                }
        }
        vti::AudioIn a;
        if (!a.open()) {
            fprintf(stderr, "virusctl: cannot map the audio ring\n");
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        auto stamp = [&](int64_t f, std::vector<uint8_t> ev) {
            auto pk = vti::usbmidi_pack({0xF5, (uint8_t)(f & 0x7F), (uint8_t)((f >> 7) & 0x7F)}, 5);
            auto e = vti::usbmidi_pack(ev, CABLE_PLUGIN);
            pk.insert(pk.end(), e.begin(), e.end());
            broker.write(EP_PLUGIN_OUT, pk.data(), pk.size());
        };
        // read the raw ring (USB 1 left) via a second mapping of the shm
        char name[64];
        snprintf(name, sizeof name, VTI_AUDIO_SHM_LINUX, (unsigned)getuid());
        int fd = shm_open(name, O_RDONLY, 0);
        auto *shm = (const vti_audio_shm *)mmap(nullptr, sizeof(vti_audio_shm), PROT_READ, MAP_SHARED, fd, 0);
        // MIDI clock like the original's (24 ppqn at 120 BPM = every 918.75 frames), each tick stamped with its frame
        std::atomic<bool> clk_stop{false};
        std::mutex wmu;
        std::thread clk;
        // continuous stamped clock like the original: every ~10 ms a tick stamped at (newest frame - S), S = VTI_S
        int S = getenv("VTI_S") ? atoi(getenv("VTI_S")) : 5000;
        if (getenv("VTI_CLOCK")) {
            clk = std::thread([&] {
                while (!clk_stop) {
                    {
                        std::lock_guard<std::mutex> lk(wmu);
                        if (getenv("VTI_F5ONLY")) { // a bare time stamp (cable 5) without a clock tick
                            int64_t f = a.newest_frame() - S;
                            auto pk = vti::usbmidi_pack({0xF5, (uint8_t)(f & 0x7F), (uint8_t)((f >> 7) & 0x7F)}, 5);
                            broker.write(EP_PLUGIN_OUT, pk.data(), pk.size());
                        } else
                            stamp(a.newest_frame() - S, {0xF8});
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        std::vector<int> ds;
        if (const char *l = getenv("VTI_DS")) { // comma-separated offsets
            for (const char *q = l; *q;) {
                ds.push_back(atoi(q));
                while (*q && *q != ',')
                    q++;
                if (*q)
                    q++;
            }
        } else
            for (int d = -8000; d <= 8000; d += 1000)
                ds.push_back(d);
        for (int d : ds) {
            int64_t now = a.newest_frame(), t = now - S + d;
            {
                std::lock_guard<std::mutex> lk(wmu);
                stamp(t, {0x90, 60, 100});
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(300)); // the ring holds ~370 ms: scan now
            int64_t onset = -1, end = a.newest_frame();
            int quiet = 0; // the first loud frame after at least 1000 quiet ones
            for (int64_t f = std::max<int64_t>(now - 3000, end - VTI_AUDIO_RING + 1); f < end && onset < 0; f++) {
                const int16_t *fr = shm->in + (f & (VTI_AUDIO_RING - 1)) * VTI_AUDIO_IN_CH;
                bool loud = std::abs(fr[2]) > 600 || std::abs(fr[3]) > 600;
                if (loud && quiet >= 1000)
                    onset = f;
                quiet = loud ? 0 : quiet + 1;
            }
            {
                std::lock_guard<std::mutex> lk(wmu);
                stamp(a.newest_frame() - S, {0x80, 60, 0});
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(getenv("VTI_GAP") ? atoi(getenv("VTI_GAP")) : 800));
            printf("D %5d: stamp %lld (sent at ring frame %lld), onset %lld -> onset - stamp %lld, onset - send %lld\n", d,
                   (long long)t, (long long)now, (long long)onset, (long long)(onset - t), (long long)(onset - now));
            fflush(stdout);
        }
        clk_stop = true;
        if (clk.joinable())
            clk.join();
        broker.audio(false);
        return 0;
    }
    if (cmd == "sysex" && argc >= 3) {
        std::vector<uint8_t> msg; // several messages may follow each other ("f0 .. f7 f0 .. f7")
        std::istringstream hex(argv[2]);
        for (std::string t; hex >> t;)
            msg.push_back((uint8_t)strtoul(t.c_str(), nullptr, 16));
        int ms = argc > 3 ? atoi(argv[3]) : 2000;
        FILE *out = argc > 4 ? fopen(argv[4], "ab") : nullptr;
        static const int CIN_LEN[16] = {0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1};
        std::mutex mu;
        auto t0 = std::chrono::steady_clock::now();
        std::map<int, std::vector<uint8_t>> partial; // per endpoint and cable
        broker.on_rx([&](uint8_t ep, const uint8_t *d, size_t len) {
            std::lock_guard<std::mutex> lk(mu);
            if (getenv("VIRUSCTL_DEBUG")) {
                printf("rx ep%02x %zu:", ep, len);
                for (size_t k = 0; k < len && k < 16; k++)
                    printf(" %02x", d[k]);
                printf("\n");
            }
            // replies come on EP 0x86 (plug-in interface) during a plug-in session, else on EP 0x85 (MIDI port)
            for (size_t i = 0; i + 4 <= len; i += 4) {
                int cable = d[i] >> 4, cin = d[i] & 15, key = ep << 4 | cable;
                auto &buf = partial[key];
                if (cin == 4 || (cin >= 5 && cin <= 7 && (!buf.empty() || d[i + 1] == 0xF0))) {
                    buf.insert(buf.end(), d + i + 1, d + i + 1 + CIN_LEN[cin]);
                    if (cin != 4) {
                        printf("%6lld ms ep%02x c%d %4zu B:", (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - t0).count(), ep, cable, buf.size());
                        for (size_t k = 0; k < buf.size() && k < 12; k++)
                            printf(" %02x", buf[k]);
                        printf(buf.size() > 12 ? " ..\n" : "\n");
                        if (out)
                            fwrite(buf.data(), 1, buf.size(), out);
                        buf.clear();
                    }
                }
            }
        });
        for (size_t a = 0; a < msg.size();) {
            size_t b = a;
            while (b < msg.size() && msg[b] != 0xF7)
                b++;
            std::vector<uint8_t> one(msg.begin() + a, msg.begin() + std::min(b + 1, msg.size()));
            auto pk = vti::usbmidi_pack(one, CABLE_PLUGIN);
            if (!broker.write(EP_PLUGIN_OUT, pk.data(), pk.size())) {
                fprintf(stderr, "virusctl: write failed\n");
                return 1;
            }
            a = b + 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        broker.on_rx(nullptr);
        if (out)
            fclose(out);
        return 0;
    }
    fprintf(stderr, "usage: virusctl state | set <path> <value> [part] | note <note> [ms] [velocity] | sysex <hex> [ms] [file]\n");
    return 2;
}
