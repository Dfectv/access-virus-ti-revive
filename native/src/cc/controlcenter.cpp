// controlcenter.cpp - see controlcenter.h.
#include "controlcenter.h"
#include "../access/library.h"
#include <ctime>
#include <filesystem>
#include <fstream>

namespace vti::cc {

namespace fs = std::filesystem;

// a string descriptor (UTF-16LE) as ASCII; '?' for anything else
static std::string string_descriptor(Broker &broker, uint8_t index)
{
    uint8_t b[255] = {};
    int n = index ? broker.control(0x80, 6, (uint16_t)(0x0300 | index), 0x0409, b, sizeof b) : -1;
    std::string s;
    for (int i = 2; i + 1 < n && i + 1 < b[0]; i += 2)
        s += (b[i + 1] == 0 && b[i] >= 32 && b[i] < 127) ? (char)b[i] : '?';
    return s;
}

bool read_device_info(Broker &broker, DeviceInfo &info, std::string &error)
{
    uint8_t d[18] = {};
    if (broker.control(0x80, 6, 0x0100, 0, d, sizeof d) != (int)sizeof d) {
        error = "device descriptor not readable";
        return false;
    }
    info.vendor = (uint16_t)(d[8] | d[9] << 8);
    info.product = (uint16_t)(d[10] | d[11] << 8);
    info.bcd = (uint16_t)(d[12] | d[13] << 8);
    info.manufacturer = string_descriptor(broker, d[14]);
    info.product_name = string_descriptor(broker, d[15]);
    return true;
}

std::string patch_name(const Dump &d)
{
    if (d.size() < 259)
        return {};
    std::string s(d.begin() + 249, d.begin() + 259);
    while (!s.empty() && s.back() == ' ')
        s.pop_back();
    return s;
}

std::string cache_dir()
{
    // same folders as the plug-in (plugin/source/virusplugin.cpp own_cache_dir)
#ifdef _WIN32
    const char *base = getenv("LOCALAPPDATA");
    return base ? std::string(base) + "/virus-ti-native/cache" : std::string();
#elif defined(__APPLE__)
    const char *home = getenv("HOME");
    return home ? std::string(home) + "/Library/Application Support/virus-ti-native/cache" : std::string();
#else
    const char *home = getenv("HOME");
    return home ? std::string(home) + "/.local/share/virus-ti-native/cache" : std::string();
#endif
}

std::string default_backup_dir()
{
    // next to the "Patches" folder, not inside it: the plug-in lists everything below Patches as libraries
    std::string lib = library_dir();
    fs::path base = lib.empty() ? fs::path("Virus TI Backups") : fs::path(lib).parent_path() / "Backups";
    char stamp[32];
    time_t t = time(nullptr);
    strftime(stamp, sizeof stamp, "%Y-%m-%d %H%M", localtime(&t));
    return (base / stamp).string();
}

bool backup_ram(Broker &broker, const std::string &banks, const std::string &out_dir,
                const std::function<void(int, int, bool, const Dump &)> &progress, std::string &error)
{
    std::error_code ec;
    fs::create_directories(out_dir, ec);
    if (ec) {
        error = "cannot create " + out_dir + ": " + ec.message();
        return false;
    }
    std::string cache = cache_dir();
    if (!cache.empty())
        fs::create_directories(cache, ec);
    MemorySync sync(broker);
    for (char c : banks) {
        int bank = c - '0';
        if (bank < 1 || bank > 4)
            continue;
        std::string name = std::string("RAM ") + (char)('A' + bank - 1);
        Bank b;
        if (!cache.empty())
            load_bank_file(cache + "/" + name, b);
        int n = sync.sync_bank(bank, b, [&](int p, bool fetched) {
            if (progress)
                progress(bank, p, fetched, b[p]);
        });
        if (!cache.empty())
            save_bank_file(cache + "/" + name, b); // also what was fetched before a failure
        if (n < 0) {
            error = name + ": the Virus did not answer (is the plug-in or another program using it?)";
            return false;
        }
        PatchLibrary lib;
        lib.name = name;
        lib.path = (fs::path(out_dir) / (name + ".mid")).string();
        lib.dumps.assign(b.begin(), b.end());
        for (auto &d : lib.dumps)
            if (d.size() != 524) {
                error = name + ": incomplete";
                return false;
            }
        if (!save_library(lib, error))
            return false;
    }
    return true;
}

bool write_syx(const std::vector<Dump> &dumps, const std::string &path, std::string &error)
{
    std::string tmp = path + ".tmp";
    {
        std::ofstream o(tmp, std::ios::binary);
        for (auto &d : dumps)
            o.write((const char *)d.data(), (std::streamsize)d.size());
        if (!o) {
            error = "cannot write " + tmp;
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec)
        error = ec.message();
    return !ec;
}

} // namespace vti::cc
