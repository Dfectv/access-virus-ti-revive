// virusctl - talk to the Virus TI through the TI Revive broker using the device model (test tool for R2).
//   virusctl state                       is a Virus connected?
//   virusctl set <path> <value> [part]   send a parameter change (encoded from remotedevice.xml)
//   virusctl note <note> [ms] [velocity] play a note on MIDI channel 1 (plug-in cable 2)
//   virusctl sysex <hex bytes> [ms] [file] send a SysEx on cable 2, print the SysEx replies of the next ms (default
//                                        2000) and append them to file (raw .syx)
#include "../src/access/accessfiles.h"
#include "../src/device/broker.h"
#include "../src/device/usbmidi.h"
#include "../src/model/remotedevice.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
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
