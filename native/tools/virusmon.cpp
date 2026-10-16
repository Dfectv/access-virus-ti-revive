// virusmon - watch the traffic between the plug-in and the Virus TI, read-only (a broker observer, protocol 4).
// One decoded line per MIDI message: parameter names from the user's Access files (as in the Control Center's
// Nerd mode), Access SysEx by name, the rest as hex. It never sends anything to the Virus - the broker refuses an
// observer's writes - so it can run next to a DAW during a hardware test.
//   virusmon [seconds] [--clock] [--hex]   default 120 s; --clock also shows clock, Active Sensing and F5 lines;
//                                          --hex adds every SysEx (dumps too) as hex on the next line
#include "access/accessfiles.h"
#include "cc/decode.h"
#include "device/broker.h"
#include "model/remotedevice.h"
#include "vtiproto.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

int main(int argc, char **argv)
{
    int seconds = 120;
    bool clock = false, hex = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--clock"))
            clock = true;
        else if (!strcmp(argv[i], "--hex"))
            hex = true;
        else if (atoi(argv[i]) > 0)
            seconds = atoi(argv[i]);
        else {
            fprintf(stderr, "usage: virusmon [seconds] [--clock] [--hex]\n");
            return 2;
        }
    }
    std::string err;
    auto files = vti::AccessFiles::open("", err);
    vti::RemoteDevice model;
    const bool named = files && model.load(files->get("remotedevice.xml"), err);
    vti::cc::TrafficDecoder dec(named ? &model : nullptr);
    vti::Broker broker;
    broker.set_name("virusmon (read-only)");
    auto show = [&](uint16_t type, const uint8_t *d, size_t n) {
        if (type == VTIMSG_OBS_LOG)
            printf("broker %.*s\n", (int)n, (const char *)d);
        else if (type == VTIMSG_OBS_TRAFFIC)
            dec.feed(d, n, [clock, hex](vti::cc::TrafficLine &&t) {
                if (t.kind == "clock" && !clock)
                    return;
                static const char *dir[] = {"->", "<-", "ctl"};
                printf("%6u.%03u %s %-6s %s\n", t.ms / 1000, t.ms % 1000, dir[t.dir], t.kind.c_str(), t.text.c_str());
                if (hex && !t.bytes.empty() && t.bytes[0] == 0xF0) {
                    printf("       hex");
                    for (auto b : t.bytes)
                        printf(" %02x", b);
                    printf("\n");
                }
            });
        fflush(stdout);
    };
    if (!broker.open(err) || !broker.observe(show, err)) {
        fprintf(stderr, "virusmon: %s\n", err.c_str());
        return 1;
    }
    printf("observing for %d s (%s)\n", seconds, named ? "parameter names from the Access files" : "no Access files: hex");
    fflush(stdout);
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    return 0;
}
