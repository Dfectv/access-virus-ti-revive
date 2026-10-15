// tirevive-cc - TI Revive Control Center, command line (phase 1: read-only, src/cc/controlcenter.h).
//   tirevive-cc info                          USB identity of the Virus, RAM banks compared with the plug-in's cache
//   tirevive-cc backup [--out DIR] [--banks 1234]
//                                             RAM banks -> "RAM A.mid".. (library format; default <Documents>/
//                                             Access Music/Virus TI/Backups/<date time>)
//   tirevive-cc list FILE                     patch names in a library (.mid or .syx), no Virus needed
//   tirevive-cc export FILE OUT.syx|OUT.mid   convert a library, no Virus needed
// Firmware/OS update, Burn To Flash, factory reset and renaming the Virus are not supported, on purpose: they write
// the Virus' flash (AGENTS.md rule 2).
#include "../src/access/accessfiles.h"
#include "../src/access/library.h"
#include "../src/cc/controlcenter.h"
#include "../src/model/remotedevice.h"
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

static int usage()
{
    fprintf(stderr, "usage: tirevive-cc info\n"
                    "       tirevive-cc backup [--out DIR] [--banks 1234]\n"
                    "       tirevive-cc list FILE.mid|FILE.syx\n"
                    "       tirevive-cc export FILE.mid|FILE.syx OUT.syx|OUT.mid\n"
                    "Read-only: nothing is written to the Virus. Close the TI Revive plug-in (or your DAW) first.\n");
    return 2;
}

static bool connect(vti::Broker &broker)
{
    std::string err;
    if (!broker.open(err) || !broker.device_present()) {
        fprintf(stderr, "tirevive-cc: %s\n", err.empty() ? "no Virus (not connected, or another program owns it)"
                                                         : err.c_str());
        return false;
    }
    return true;
}

static int info()
{
    vti::Broker broker;
    if (!connect(broker))
        return 1;
    std::string err;
    vti::cc::DeviceInfo di;
    if (!vti::cc::read_device_info(broker, di, err)) {
        fprintf(stderr, "tirevive-cc: %s\n", err.c_str());
        return 1;
    }
    printf("USB device   %04x:%04x, bcdDevice %x.%02x\n", di.vendor, di.product, di.bcd >> 8, di.bcd & 0xFF);
    printf("Manufacturer %s\nProduct      %s\n", di.manufacturer.c_str(), di.product_name.c_str());
    vti::MemorySync sync(broker);
    std::string cache = vti::cc::cache_dir();
    for (int bank = 1; bank <= 4; bank++) {
        std::string name = std::string("RAM ") + (char)('A' + bank - 1);
        auto fp = sync.bank_info(bank);
        if (fp.size() != 128) {
            printf("%s        no answer\n", name.c_str());
            continue;
        }
        vti::Bank b;
        int current = 0;
        if (vti::load_bank_file(cache + "/" + name, b))
            for (int p = 0; p < 128; p++)
                current += vti::RemoteDevice::single_fingerprint(b[p]) == fp[p];
        printf("%s        128 programs, %d up to date in the cache", name.c_str(), current);
        if (current)
            printf(" (1: %s)", vti::cc::patch_name(b[0]).c_str());
        printf("\n");
    }
    printf("Cache        %s\n", cache.c_str());
    return 0;
}

static int backup(int argc, char **argv)
{
    std::string out, banks = "1234";
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--out") && i + 1 < argc)
            out = argv[++i];
        else if (!strcmp(argv[i], "--banks") && i + 1 < argc)
            banks = argv[++i];
        else
            return usage();
    }
    if (out.empty())
        out = vti::cc::default_backup_dir();
    vti::Broker broker;
    if (!connect(broker))
        return 1;
    printf("Backup of the RAM banks to %s\n(unchanged programs come from the cache, the others take about 1 s each)\n",
           out.c_str());
    std::string err;
    int fetched = 0;
    bool ok = vti::cc::backup_ram(broker, banks, out, [&](int bank, int p, bool f, const vti::Dump &d) {
        fetched += f;
        if (f)
            printf("  RAM %c %3d %s\n", 'A' + bank - 1, p + 1, vti::cc::patch_name(d).c_str());
        else if (p == 127)
            printf("  RAM %c checked\n", 'A' + bank - 1);
        fflush(stdout);
    }, err);
    if (!ok) {
        fprintf(stderr, "tirevive-cc: %s\n", err.c_str());
        return 1;
    }
    printf("Done: %d programs read from the Virus, libraries in %s\n", fetched, out.c_str());
    return 0;
}

static bool load(const char *path, vti::PatchLibrary &lib)
{
    // with the Access files, old Virus C dumps are upgraded like in the plug-in; without them they are skipped
    std::string err;
    auto files = vti::AccessFiles::open("", err);
    if (!vti::load_library_file(path, lib, files ? files->default_page_b() : std::vector<uint8_t>())) {
        fprintf(stderr, "tirevive-cc: cannot read %s\n", path);
        return false;
    }
    if (lib.dumps.empty()) {
        fprintf(stderr, "tirevive-cc: no Virus TI single dumps in %s\n", path);
        return false;
    }
    return true;
}

static int list(const char *path)
{
    vti::PatchLibrary lib;
    if (!load(path, lib))
        return 1;
    for (size_t i = 0; i < lib.dumps.size(); i++)
        printf("%3zu %s\n", i + 1, vti::cc::patch_name(lib.dumps[i]).c_str());
    return 0;
}

static int export_(const char *in, const char *out)
{
    vti::PatchLibrary lib;
    if (!load(in, lib))
        return 1;
    std::string ext = fs::path(out).extension().string(), err;
    for (auto &c : ext)
        c = (char)tolower(c);
    bool ok;
    if (ext == ".syx") {
        ok = vti::cc::write_syx(lib.dumps, out, err);
    } else if (ext == ".mid") {
        if (lib.dumps.size() > 128)
            fprintf(stderr, "tirevive-cc: a library holds 128 patches, the other %zu are left out\n",
                    lib.dumps.size() - 128);
        lib.path = out;
        ok = vti::save_library(lib, err);
    } else
        return usage();
    if (!ok) {
        fprintf(stderr, "tirevive-cc: %s\n", err.c_str());
        return 1;
    }
    printf("%zu patches -> %s\n", std::min<size_t>(lib.dumps.size(), ext == ".mid" ? 128 : SIZE_MAX), out);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return usage();
    std::string cmd = argv[1];
    if (cmd == "info" && argc == 2)
        return info();
    if (cmd == "backup")
        return backup(argc, argv);
    if (cmd == "list" && argc == 3)
        return list(argv[2]);
    if (cmd == "export" && argc == 4)
        return export_(argv[2], argv[3]);
    return usage();
}
