// accessfiles.cpp - see accessfiles.h. The PE resource reader handles the standard three-level resource tree
// (type / name / language) of a PE32+ or PE32 image; only named types "XML" and "PNG" are collected.
#include "accessfiles.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace vti {

static std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

static bool read_file(const fs::path &p, std::vector<uint8_t> &out)
{
    std::ifstream f(p, std::ios::binary);
    if (!f)
        return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

template <class T> static bool rd(const std::vector<uint8_t> &d, size_t off, T &v)
{
    if (off + sizeof(T) > d.size())
        return false;
    std::memcpy(&v, d.data() + off, sizeof(T));
    return true;
}

bool AccessFiles::load_pe(const std::string &dll, std::string &error)
{
    std::vector<uint8_t> d;
    if (!read_file(dll, d)) {
        error = "cannot read " + dll;
        return false;
    }
    uint32_t pe = 0;
    uint16_t nsec = 0, optsz = 0, magic = 0;
    if (d.size() < 0x40 || d[0] != 'M' || d[1] != 'Z' || !rd(d, 0x3c, pe) || !rd(d, pe + 6, nsec) ||
        !rd(d, pe + 20, optsz) || !rd(d, pe + 24, magic)) {
        error = dll + " is not a PE image";
        return false;
    }
    size_t dd = pe + 24 + (magic == 0x20b ? 112 : 96); // data directories
    uint32_t rsrc_rva = 0;
    rd(d, dd + 2 * 8, rsrc_rva);
    struct Sec {
        uint32_t va, vs, ra, rs;
    };
    std::vector<Sec> secs;
    for (uint16_t i = 0; i < nsec; i++) {
        size_t o = pe + 24 + optsz + 40u * i;
        Sec s{};
        rd(d, o + 12, s.va), rd(d, o + 8, s.vs), rd(d, o + 20, s.ra), rd(d, o + 16, s.rs);
        secs.push_back(s);
    }
    auto r2o = [&](uint32_t rva) -> size_t {
        for (auto &s : secs)
            if (rva >= s.va && rva < s.va + std::max(s.vs, s.rs))
                return rva - s.va + s.ra;
        return 0;
    };
    size_t base = r2o(rsrc_rva);
    if (!rsrc_rva || !base) {
        error = dll + " has no resources";
        return false;
    }
    auto dir_name = [&](uint32_t nm) -> std::string {
        if (!(nm & 0x80000000u))
            return std::to_string(nm);
        size_t so = base + (nm & 0x7fffffffu);
        uint16_t len = 0;
        rd(d, so, len);
        std::string s;
        for (uint16_t k = 0; k < len; k++) {
            uint16_t c = 0;
            rd(d, so + 2 + 2u * k, c);
            s += c < 128 ? (char)c : '?';
        }
        return s;
    };
    auto entries = [&](size_t dir) {
        std::vector<std::pair<uint32_t, uint32_t>> v;
        uint16_t named = 0, ids = 0;
        rd(d, dir + 12, named), rd(d, dir + 14, ids);
        for (int i = 0; i < named + ids; i++) {
            uint32_t nm = 0, ent = 0;
            rd(d, dir + 16 + 8u * i, nm), rd(d, dir + 20 + 8u * i, ent);
            v.push_back({nm, ent});
        }
        return v;
    };
    for (auto [tname, tent] : entries(base)) {
        std::string type = dir_name(tname);
        if ((type != "XML" && type != "PNG") || !(tent & 0x80000000u))
            continue;
        for (auto [nname, nent] : entries(base + (tent & 0x7fffffffu))) {
            if (!(nent & 0x80000000u))
                continue;
            auto langs = entries(base + (nent & 0x7fffffffu));
            if (langs.empty() || (langs[0].second & 0x80000000u))
                continue;
            uint32_t drva = 0, dsz = 0;
            rd(d, base + langs[0].second, drva), rd(d, base + langs[0].second + 4, dsz);
            size_t off = r2o(drva);
            if (!off || off + dsz > d.size())
                continue;
            std::string name = lower(dir_name(nname));
            files_[name].assign(d.begin() + (long)off, d.begin() + (long)(off + dsz));
        }
    }
    if (!files_.count("remotedevice.xml")) {
        error = dll + " does not contain the Virus TI resources";
        return false;
    }
    source_ = dll;
    return true;
}

bool AccessFiles::load_dir(const std::string &dir, std::string &error)
{
    std::error_code ec;
    for (auto &e : fs::directory_iterator(dir, ec)) {
        std::string ext = lower(e.path().extension().string());
        if (ext != ".xml" && ext != ".png")
            continue;
        read_file(e.path(), files_[lower(e.path().filename().string())]);
    }
    if (!files_.count("remotedevice.xml")) {
        error = dir + " does not contain the Virus TI resources";
        return false;
    }
    source_ = dir;
    return true;
}

std::vector<uint8_t> AccessFiles::find_dump(uint8_t cmd, size_t size, size_t name_at, const char *name,
                                            std::vector<uint8_t> *binary, size_t *offset) const
{
    std::vector<std::string> bins = {source_};                                       // Windows: the DLL itself
    bins.push_back((fs::path(source_) / ".." / "MacOS" / "Virus Control").string()); // Mac: bundle binary
    const uint8_t hdr[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, cmd};
    size_t nl = strlen(name);
    for (auto &b : bins) {
        std::error_code ec;
        if (!fs::is_regular_file(b, ec))
            continue;
        std::vector<uint8_t> d;
        read_file(b, d);
        for (size_t i = 0; i + size <= d.size(); i++)
            if (d[i] == 0xF0 && !memcmp(&d[i], hdr, sizeof hdr) && d[i + size - 1] == 0xF7 &&
                !memcmp(&d[i + name_at], name, nl)) {
                if (offset)
                    *offset = i;
                std::vector<uint8_t> out(d.begin() + i, d.begin() + i + size);
                if (binary)
                    *binary = std::move(d);
                return out;
            }
    }
    return {};
}

std::vector<uint8_t> AccessFiles::init_single() const { return find_dump(0x10, 524, 249, " -Init-"); }

std::vector<uint8_t> AccessFiles::default_page_b() const
{
    std::vector<uint8_t> bin;
    size_t off = 0;
    if (find_dump(0x10, 524, 249, " -Init-", &bin, &off).empty() || off < 0x400)
        return {};
    std::vector<uint8_t> t(bin.begin() + off - 0x400, bin.begin() + off - 0x300);
    for (auto b : t)
        if (b > 0x7F)
            return {};
    return t;
}

std::vector<uint8_t> AccessFiles::init_multi() const { return find_dump(0x11, 267, 13, "MultiCtrl!"); }

std::unique_ptr<AccessFiles> AccessFiles::open(const std::string &hint, std::string &error)
{
    std::vector<std::string> cands;
    if (!hint.empty())
        cands.push_back(hint);
    if (const char *e = std::getenv("VTI_ACCESS_DIR"))
        cands.push_back(e);
    const char *home = std::getenv("HOME");
    const std::string common = "Access Music/Virus TI/Common";
#if defined(_WIN32)
    for (const char *pf : {"ProgramW6432", "ProgramFiles"})
        if (const char *p = std::getenv(pf))
            cands.push_back(std::string(p) + "/" + common);
#elif defined(__APPLE__)
    cands.push_back("/Library/Application Support/" + common + "/Virus Control.bundle");
#else
    std::string prefix = std::getenv("WINEPREFIX") ? std::getenv("WINEPREFIX") : (home ? std::string(home) + "/.wine" : "");
    if (!prefix.empty())
        cands.push_back(prefix + "/drive_c/Program Files/" + common);
#endif
    std::string last = "no Access Virus TI installation found";
    for (auto c : cands) {
        fs::path p(c);
        std::error_code ec;
        if (fs::is_directory(p / "Contents" / "Resources", ec))
            p = p / "Contents" / "Resources";
        else if (fs::is_directory(p, ec) && fs::exists(p / "Virus Control.dll", ec))
            p = p / "Virus Control.dll";
        else if (fs::is_directory(p, ec) && fs::exists(p / "Virus Control.bundle", ec))
            p = p / "Virus Control.bundle" / "Contents" / "Resources";
        std::unique_ptr<AccessFiles> a(new AccessFiles);
        bool ok = fs::is_directory(p, ec) ? a->load_dir(p.string(), last) : a->load_pe(p.string(), last);
        if (ok)
            return a;
    }
    error = last;
    return nullptr;
}

const std::vector<uint8_t> &AccessFiles::get(const std::string &name) const
{
    static const std::vector<uint8_t> none;
    auto it = files_.find(lower(name));
    return it == files_.end() ? none : it->second;
}

std::vector<std::string> AccessFiles::names() const
{
    std::vector<std::string> v;
    for (auto &kv : files_)
        v.push_back(kv.first);
    return v;
}

} // namespace vti
