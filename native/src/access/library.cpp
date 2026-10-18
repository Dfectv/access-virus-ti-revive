// library.cpp - see library.h.
#include "library.h"
#include "model/remotedevice.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

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

bool create_empty_library(const std::string &dir, std::string &path, std::string &error)
{
    if (dir.empty()) {
        error = "no library folder";
        return false;
    }
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::path p = fs::path(dir) / "New Library.mid";
    for (int n = 2; fs::exists(p, ec); n++)
        p = fs::path(dir) / ("New Library " + std::to_string(n) + ".mid");
    // byte for byte the original's empty file (Virus Control 0x180065900 writes it): the end of track at delta 120
    static const uint8_t empty[] = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xE0,
                                    'M', 'T', 'r', 'k', 0, 0, 0, 4, 0x78, 0xFF, 0x2F, 0x00};
    std::ofstream o(p, std::ios::binary);
    o.write((const char *)empty, sizeof empty);
    o.close();
    if (!o) {
        error = "cannot write " + p.string();
        return false;
    }
    path = p.string();
    return true;
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

std::string xdg_documents_dir(const std::string &home, const std::string &user_dirs)
{
    std::istringstream in(user_dirs);
    std::string line;
    while (std::getline(in, line)) {
        size_t k = line.find_first_not_of(" \t");
        if (k == std::string::npos || line.compare(k, 19, "XDG_DOCUMENTS_DIR=\"") != 0)
            continue;
        size_t b = k + 19, e = line.find('"', b);
        if (e == std::string::npos)
            return {};
        std::string v = line.substr(b, e - b);
        if (v.rfind("$HOME", 0) == 0)
            v = home + v.substr(5);
        while (v.size() > 1 && v.back() == '/')
            v.pop_back();
        if (v.empty() || v[0] != '/' || v == home)
            return {}; // XDG: "$HOME/" means "no documents folder"
        return v;
    }
    return {};
}

std::string library_dir()
{
    const std::string tail = "/Access Music/Virus TI/Patches";
    std::vector<std::string> c;
#ifdef _WIN32
    if (const char *up = getenv("USERPROFILE"))
        c.push_back(std::string(up) + "/Documents" + tail);
#else
    if (const char *home = getenv("HOME")) {
        std::string h = home, docs = h + "/Documents" + tail;
        const char *cfg = getenv("XDG_CONFIG_HOME");
        std::ifstream f((cfg && *cfg ? std::string(cfg) : h + "/.config") + "/user-dirs.dirs");
        std::string xdg;
        if (f) {
            std::stringstream t;
            t << f.rdbuf();
            xdg = xdg_documents_dir(h, t.str());
        }
        std::error_code ec;
        if (!fs::is_directory(docs, ec) && !xdg.empty())
            c.push_back(xdg + tail);
        c.push_back(docs);
        if (const char *user = getenv("USER")) // an existing Access installation under Wine (Linux)
            c.push_back(h + "/.wine/drive_c/users/" + user + "/Documents" + tail);
    }
#endif
    std::error_code ec;
    for (auto &d : c)
        if (fs::is_directory(d, ec))
            return d;
    return c.empty() ? std::string() : c.front();
}

} // namespace vti
