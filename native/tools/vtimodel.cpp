// vtimodel - inspect the user's Access files and the device model built from them.
//   vtimodel files                     list the Access resources found (and where)
//   vtimodel summary                   device model statistics
//   vtimodel params [filter]           parameters (path, range, default, transport, controller)
//   vtimodel encode <path> <value> [part]   the MIDI bytes that set a parameter
// Options: --access <dll|bundle|dir> (default: the standard install locations)
#include "../src/access/accessfiles.h"
#include "../src/model/remotedevice.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char **argv)
{
    std::string hint, cmd = argc > 1 ? argv[1] : "summary";
    std::vector<std::string> args;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--access") && i + 1 < argc)
            hint = argv[++i];
        else if (i > 1)
            args.push_back(argv[i]);
    }
    std::string err;
    auto files = vti::AccessFiles::open(hint, err);
    if (!files) {
        fprintf(stderr, "vtimodel: %s\n", err.c_str());
        return 1;
    }
    if (cmd == "files") {
        printf("source: %s\n", files->source().c_str());
        for (auto &n : files->names())
            printf("%8zu  %s\n", files->get(n).size(), n.c_str());
        return 0;
    }
    vti::RemoteDevice dev;
    if (!dev.load(files->get("remotedevice.xml"), err)) {
        fprintf(stderr, "vtimodel: %s\n", err.c_str());
        return 1;
    }
    if (cmd == "summary") {
        printf("%s, version %s (from %s)\n", dev.device.c_str(), dev.version.c_str(), files->source().c_str());
        printf("%zu parameters, %zu value lists, %zu conditions, %zu messages, %zu soft-knob targets, %zu+%zu init\n",
               dev.parameters.size(), dev.valuelists.size(), dev.conditions.size(), dev.messages.size(),
               dev.softknobs.size(), dev.global_init.size(), dev.param_init.size());
        for (auto &m : dev.messages)
            printf("  message %-24s %zu bytes\n", m.id.c_str(), m.bytes.size());
    } else if (cmd == "params") {
        std::string f = args.empty() ? "" : args[0];
        for (auto &p : dev.parameters)
            if (f.empty() || p.path.find(f) != std::string::npos)
                printf("%-50s %4d..%-4d def %-4d %-22s ctl %-4d part %-6s %s\n", p.path.c_str(), p.min, p.max, p.def,
                       p.message.c_str(), p.controller, p.part.c_str(), p.valuelist.c_str());
    } else if (cmd == "encode" && args.size() >= 2) {
        auto b = dev.encode(args[0], atoi(args[1].c_str()), args.size() > 2 ? atoi(args[2].c_str()) : 0);
        if (b.empty()) {
            fprintf(stderr, "vtimodel: no single-parameter transport for %s\n", args[0].c_str());
            return 1;
        }
        for (auto x : b)
            printf("%02x ", x);
        printf("\n");
    } else {
        fprintf(stderr, "usage: vtimodel files|summary|params [filter]|encode <path> <value> [part] [--access <path>]\n");
        return 2;
    }
    return 0;
}
