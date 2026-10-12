// virusctl - talk to the Virus TI through the TI Revive broker using the device model (test tool for R2).
//   virusctl state                       is a Virus connected?
//   virusctl set <path> <value> [part]   send a parameter change (encoded from remotedevice.xml)
//   virusctl note <note> [ms] [velocity] play a note on MIDI channel 1 (plug-in cable 2)
#include "../src/access/accessfiles.h"
#include "../src/device/broker.h"
#include "../src/device/usbmidi.h"
#include "../src/model/remotedevice.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
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
    fprintf(stderr, "usage: virusctl state | set <path> <value> [part] | note <note> [ms] [velocity]\n");
    return 2;
}
