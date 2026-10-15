// library.cpp - see library.h.
#include "library.h"
#include "model/remotedevice.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace vti {

namespace fs = std::filesystem;

bool load_library_file(const std::string &path, PatchLibrary &lib, const std::vector<uint8_t> &page_b)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    lib.path = path;
    lib.dumps.clear();
    // SysEx single dumps anywhere in the file (SMF sysex events or raw .syx): 00 20 33 01 <dev> 10 ... F7
    for (size_t i = 0; i + 8 < d.size(); i++) {
        if (!(d[i] == 0x00 && d[i + 1] == 0x20 && d[i + 2] == 0x33 && d[i + 3] == 0x01 && d[i + 5] == 0x10))
            continue;
        auto end = std::find(d.begin() + i, d.end(), 0xF7);
        if (end == d.end())
            break;
        std::vector<uint8_t> m = {0xF0};
        m.insert(m.end(), d.begin() + i, end + 1);
        if (m.size() == 524) {
            lib.dumps.push_back(m);
        } else if (m.size() == 267 && page_b.size() == 256) { // Virus C: page A only -> upgrade like the original
            m.resize(524, 0);
            m[523] = 0xF7;
            if (RemoteDevice::upgrade_single(m, page_b))
                lib.dumps.push_back(m);
        }
        i = end - d.begin();
    }
    return true;
}

std::vector<PatchLibrary> load_libraries(const std::string &dir, const std::vector<uint8_t> &page_b)
{
    std::vector<PatchLibrary> out;
    std::error_code ec;
    // like the original: also the libraries in subfolders (e.g. "Virus TI2 Factory Backup/Rom/Virus TI2 Rom A")
    for (auto &e : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
        if (!e.is_regular_file(ec))
            continue;
        std::string ext = e.path().extension().string();
        for (auto &ch : ext)
            ch = (char)tolower(ch);
        if (ext != ".mid" && ext != ".syx")
            continue;
        PatchLibrary lib;
        if (!load_library_file(e.path().string(), lib, page_b))
            continue;
        lib.name = (e.path().parent_path().lexically_relative(dir) / e.path().stem()).generic_string();
        if (lib.name.rfind("./", 0) == 0)
            lib.name = lib.name.substr(2);
        out.push_back(std::move(lib));
    }
    std::sort(out.begin(), out.end(), [](const PatchLibrary &a, const PatchLibrary &b) { return a.name < b.name; });
    return out;
}

bool save_library(const PatchLibrary &lib, std::string &error)
{
    std::vector<uint8_t> trk;
    auto varlen = [&](uint32_t v) {
        uint8_t b[5];
        int n = 0;
        b[n++] = v & 0x7F;
        while (v >>= 7)
            b[n++] = (uint8_t)(0x80 | (v & 0x7F));
        while (n)
            trk.push_back(b[--n]);
    };
    for (size_t i = 0; i < lib.dumps.size() && i < 128; i++) {
        std::vector<uint8_t> m = lib.dumps[i];
        if (m.size() != 524)
            continue;
        m[7] = 1, m[8] = (uint8_t)i;
        unsigned a = 0;
        for (int k = 5; k < 265; k++)
            a += m[k];
        m[265] = a & 0x7F;
        for (int k = 265; k < 522; k++)
            a += m[k];
        m[522] = a & 0x7F;
        varlen(120);
        trk.push_back(0xF0);
        varlen((uint32_t)m.size() - 1);
        trk.insert(trk.end(), m.begin() + 1, m.end());
    }
    varlen(120);
    trk.insert(trk.end(), {0xFF, 0x2F, 0x00});
    std::vector<uint8_t> f = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 0x60, 'M', 'T', 'r', 'k'};
    for (int s = 24; s >= 0; s -= 8)
        f.push_back((uint8_t)(trk.size() >> s));
    f.insert(f.end(), trk.begin(), trk.end());
    std::string tmp = lib.path + ".tmp";
    {
        std::ofstream o(tmp, std::ios::binary);
        o.write((const char *)f.data(), (std::streamsize)f.size());
        if (!o) {
            error = "cannot write " + tmp;
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, lib.path, ec);
    if (ec)
        error = ec.message();
    return !ec;
}

std::string library_dir()
{
    std::vector<std::string> c;
#ifdef _WIN32
    if (const char *up = getenv("USERPROFILE"))
        c.push_back(std::string(up) + "/Documents/Access Music/Virus TI/Patches");
#else
    if (const char *home = getenv("HOME")) {
        c.push_back(std::string(home) + "/Documents/Access Music/Virus TI/Patches");
        if (const char *user = getenv("USER")) // an existing Access installation under Wine (Linux)
            c.push_back(std::string(home) + "/.wine/drive_c/users/" + user + "/Documents/Access Music/Virus TI/Patches");
    }
#endif
    std::error_code ec;
    for (auto &d : c)
        if (fs::is_directory(d, ec))
            return d;
    return c.empty() ? std::string() : c.front();
}

} // namespace vti
