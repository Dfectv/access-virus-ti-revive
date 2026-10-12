// vtimodel - inspect the user's Access files and the device model built from them.
//   vtimodel files                     list the Access resources found (and where)
//   vtimodel summary                   device model statistics
//   vtimodel params [filter]           parameters (path, range, default, transport, controller)
//   vtimodel encode <path> <value> [part]   the MIDI bytes that set a parameter
// Options: --access <dll|bundle|dir> (default: the standard install locations)
#include <map>
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
    if (cmd == "cat" && !args.empty()) { // raw content of one Access file (inspection only - output stays local)
        auto d = files->get(args[0]);
        fwrite(d.data(), 1, d.size(), stdout);
        return d.empty();
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
    } else if (cmd == "automation") { // the plug-in parameter list as the original builds it: id, title, unit
        std::vector<vti::AutomationEntry> list;
        if (!vti::load_automation(files->get("automation.xml"), list, err)) {
            fprintf(stderr, "vtimodel: %s\n", err.c_str());
            return 1;
        }
        for (auto &e : list) {
            printf("%d\t[%d] %s\tPart %02d/\n", e.id, e.part, vti::automation_title(dev, e).c_str(), e.part);
        }
    } else if (cmd == "initpatch" && !args.empty()) { // the plug-in's embedded init patch, upgraded, to a file
        auto d = files->init_single();
        int v = d.size() > 9 ? d[9] : -1;
        bool ch = vti::RemoteDevice::upgrade_single(d);
        if (FILE *f = fopen(args[0].c_str(), "wb")) {
            fwrite(d.data(), 1, d.size(), f);
            fclose(f);
        }
        printf("init patch %zu bytes, version %d -> %d%s\n", d.size(), v, d.size() > 9 ? d[9] : -1, ch ? "" : " (unchanged)");
    } else if (cmd == "init") { // start-up messages (GlobalInit + ParamInit)
        for (auto &m : dev.init_messages()) {
            for (auto b : m)
                printf("%02x ", b);
            printf("\n");
        }
    } else if (cmd == "message" && !args.empty()) {
        const vti::MidiMessage *m = dev.message(args[0]);
        if (!m) {
            fprintf(stderr, "vtimodel: no message %s\n", args[0].c_str());
            return 1;
        }
        for (auto &b : m->bytes) {
            printf("%4d %-40s", b.index, b.content.c_str());
            if (b.tag >= 0)
                printf(" tag %02x", b.tag);
            if (b.mask >= 0)
                printf(" mask %02x shift %d", b.mask, b.shift);
            if (b.part >= 0)
                printf(" part %d", b.part);
            if (!b.method.empty())
                printf(" %s %d..%d", b.method.c_str(), b.start, b.stop);
            printf("\n");
        }
    } else if (cmd == "dump" && !args.empty()) {
        // a raw SysEx dump file (single 0x10 / multi 0x11): checksum, decoded values, round trip
        FILE *f = fopen(args[0].c_str(), "rb");
        if (!f) {
            perror(args[0].c_str());
            return 1;
        }
        std::vector<uint8_t> msg;
        for (int c; (c = fgetc(f)) != EOF;)
            msg.push_back((uint8_t)c);
        fclose(f);
        const vti::MidiMessage *m = msg.size() > 6 ? dev.message(msg[6] == 0x11 ? "VirusTI_MultiDump" : "VirusTI_Singledump") : nullptr;
        std::map<std::string, int> values;
        std::string error;
        if (!m || !dev.decode_dump(*m, msg, values, error)) {
            fprintf(stderr, "vtimodel: %s\n", m ? error.c_str() : "not a dump");
            return 1;
        }
        std::string name;
        for (auto &kv : values) {
            if (kv.first.rfind("Single Patch Name/", 0) == 0)
                name += (char)kv.second;
            printf("%-50s %d\n", kv.first.c_str(), kv.second);
        }
        auto copy = msg;
        dev.update_dump(*m, copy, values);
        printf("# %s bank %d program %d, name \"%s\", %zu values, round trip %s, fingerprint %d\n", m->id.c_str(),
               msg[7], msg[8], name.c_str(), values.size(), copy == msg ? "identical" : "DIFFERS",
               vti::RemoteDevice::single_fingerprint(msg));
    } else {
        fprintf(stderr, "usage: vtimodel files|summary|params [filter]|message <id>|dump <file>|cat <file>|encode <path> <value> [part] [--access <path>]\n");
        return 2;
    }
    return 0;
}
