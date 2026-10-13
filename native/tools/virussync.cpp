// virussync - keep a local cache of the Virus' RAM banks current (test tool for the sync engine, src/device/memsync).
//   virussync [--cache DIR] [--banks 1234] [--import DIR]
// --import copies "RAM A".."RAM D" from an Access cache directory (e.g. .../ProgramData/Access Music/Virus TI/Cache/<id>)
// first; only programs whose fingerprint differs from the Virus' bank info are fetched afterwards.
#include "../src/device/broker.h"
#include "../src/device/memsync.h"
#include "../src/model/remotedevice.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <filesystem>

int main(int argc, char **argv)
{
    const char *home = getenv("HOME");
    std::string cache = std::string(home ? home : ".") + "/.local/share/virus-ti-native/cache", banks = "1234", import;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--cache") && i + 1 < argc)
            cache = argv[++i];
        else if (!strcmp(argv[i], "--banks") && i + 1 < argc)
            banks = argv[++i];
        else if (!strcmp(argv[i], "--import") && i + 1 < argc)
            import = argv[++i];
        else {
            fprintf(stderr, "usage: virussync [--cache DIR] [--banks 1234] [--import ACCESS_CACHE_DIR]\n");
            return 2;
        }
    }
    std::error_code ec;
    std::filesystem::create_directories(cache, ec);

    std::string err;
    vti::Broker broker;
    if (!broker.open(err) || !broker.device_present()) {
        fprintf(stderr, "virussync: %s\n", err.empty() ? "no Virus (another program may own it)" : err.c_str());
        return 1;
    }
    vti::MemorySync sync(broker);
    int total = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (char c : banks) {
        int bank = c - '0';
        if (bank < 1 || bank > 4)
            continue;
        std::string name = std::string("RAM ") + (char)('A' + bank - 1), path = cache + "/" + name;
        vti::Bank b;
        if (!import.empty() && vti::load_bank_file(import + "/" + name, b))
            printf("%s: imported from %s\n", name.c_str(), import.c_str());
        else
            vti::load_bank_file(path, b);
        int n = sync.sync_bank(bank, b, [&](int p, bool fetched) {
            if (fetched) {
                std::string nm(b[p].begin() + 249, b[p].begin() + 259);
                printf("  %s %3d %s\n", name.c_str(), p + 1, nm.c_str());
                fflush(stdout);
            }
        });
        if (n < 0) {
            fprintf(stderr, "virussync: %s failed\n", name.c_str());
            vti::save_bank_file(path, b); // keep what was fetched
            return 1;
        }
        if (!vti::save_bank_file(path, b)) {
            fprintf(stderr, "virussync: cannot write %s\n", path.c_str());
            return 1;
        }
        printf("%s: %d fetched\n", name.c_str(), n);
        total += n;
    }
    printf("%d programs fetched in %.1f s, cache %s\n", total,
           std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), cache.c_str());
    return 0;
}
