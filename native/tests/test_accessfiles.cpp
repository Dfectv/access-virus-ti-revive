// AccessFiles on fake installations in a temporary folder (no Access content): a resource folder, a macOS-style
// bundle with its binary (init single / multi dumps found by their headers), a minimal PE image with an XML resource
// tree built here, the search order of open() and open_control_center(), and the error paths. Runs in CI.
#ifndef _WIN32
#include "check.h"
#include "access/accessfiles.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unistd.h>
using namespace vti;
namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

namespace {
// sets environment variables for one test and puts the old values back (the other tests find the real files)
struct Env {
    std::vector<std::pair<std::string, std::pair<bool, std::string>>> saved;
    void set(const char *k, const std::string &v)
    {
        remember(k);
        setenv(k, v.c_str(), 1);
    }
    void unset(const char *k)
    {
        remember(k);
        unsetenv(k);
    }
    void remember(const char *k)
    {
        for (auto &s : saved)
            if (s.first == k)
                return;
        const char *o = getenv(k);
        saved.push_back({k, {o != nullptr, o ? o : ""}});
    }
    ~Env()
    {
        for (auto &s : saved)
            s.second.first ? setenv(s.first.c_str(), s.second.second.c_str(), 1) : unsetenv(s.first.c_str());
    }
};

struct TempDir {
    fs::path dir;
    explicit TempDir(const char *tag)
    {
        dir = fs::temp_directory_path() / (std::string("vtitests-") + tag + "-" + std::to_string(getpid()));
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

void write(const fs::path &p, const Bytes &b)
{
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary).write((const char *)b.data(), (std::streamsize)b.size());
}
void write(const fs::path &p, const std::string &s) { write(p, Bytes(s.begin(), s.end())); }

const std::string REMOTE = "<Remotedevice device=\"Fake\"/>";

// a dump the way find_dump looks for it: F0 00 20 33 01 10 <cmd> ... name at name_at ... F7
Bytes fake_dump(uint8_t cmd, size_t size, size_t name_at, const char *name)
{
    Bytes d(size, 0x01);
    const uint8_t hdr[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, cmd};
    memcpy(d.data(), hdr, sizeof hdr);
    memcpy(d.data() + name_at, name, strlen(name));
    d.back() = 0xF7;
    return d;
}

// a minimal PE32+ image: one section holding a resource tree type -> name -> language -> data, for the named
// resources (type "XML" or "PNG"), plus an id-typed resource (skipped like icons)
Bytes fake_pe(const std::vector<std::pair<std::string, std::string>> &xml, bool with_resources = true)
{
    const uint32_t pe = 0x40, optsz = 240, sec_raw = 0x400, sec_va = 0x1000;
    Bytes rs; // resource section
    auto put16 = [](Bytes &b, size_t o, uint16_t v) { b[o] = v & 0xFF, b[o + 1] = v >> 8; };
    auto put32 = [](Bytes &b, size_t o, uint32_t v) {
        for (int i = 0; i < 4; i++)
            b[o + i] = (v >> (8 * i)) & 0xFF;
    };
    auto grow = [&](size_t n) {
        size_t o = rs.size();
        rs.resize(o + n, 0);
        return o;
    };
    auto dir = [&](int named, int ids) {
        size_t o = grow(16 + 8u * (named + ids));
        put16(rs, o + 12, (uint16_t)named), put16(rs, o + 14, (uint16_t)ids);
        return o;
    };
    auto str = [&](const std::string &s) {
        size_t o = grow(2 + 2 * s.size());
        put16(rs, o, (uint16_t)s.size());
        for (size_t i = 0; i < s.size(); i++)
            put16(rs, o + 2 + 2 * i, (uint8_t)s[i]);
        return o;
    };
    size_t root = dir(1, 1);
    size_t type_xml = dir((int)xml.size(), 0);
    put32(rs, root + 16, 0x80000000u | (uint32_t)str("XML"));
    put32(rs, root + 20, 0x80000000u | (uint32_t)type_xml);
    put32(rs, root + 24, 3);                                   // id type (an icon): skipped
    put32(rs, root + 28, 0x80000000u | (uint32_t)dir(0, 0));   // an empty directory
    for (size_t i = 0; i < xml.size(); i++) {
        size_t lang = dir(0, 1);
        put32(rs, type_xml + 16 + 8 * i, 0x80000000u | (uint32_t)str(xml[i].first));
        put32(rs, type_xml + 20 + 8 * i, 0x80000000u | (uint32_t)lang);
        size_t entry = grow(16);
        put32(rs, lang + 16, 1033), put32(rs, lang + 20, (uint32_t)entry); // a data entry (no high bit)
        size_t data = grow(xml[i].second.size());
        memcpy(rs.data() + data, xml[i].second.data(), xml[i].second.size());
        put32(rs, entry, sec_va + (uint32_t)data), put32(rs, entry + 4, (uint32_t)xml[i].second.size());
    }
    Bytes d(sec_raw + rs.size(), 0);
    d[0] = 'M', d[1] = 'Z';
    put32(d, 0x3c, pe);
    memcpy(d.data() + pe, "PE\0\0", 4);
    put16(d, pe + 6, 1);              // one section
    put16(d, pe + 20, optsz);
    put16(d, pe + 24, 0x20b);         // PE32+
    if (with_resources) {
        put32(d, pe + 24 + 112 + 16, sec_va); // data directory 2: resources
        put32(d, pe + 24 + 112 + 20, (uint32_t)rs.size());
    }
    size_t sh = pe + 24 + optsz;
    put32(d, sh + 8, (uint32_t)rs.size()), put32(d, sh + 12, sec_va);
    put32(d, sh + 16, (uint32_t)rs.size()), put32(d, sh + 20, sec_raw);
    memcpy(d.data() + sec_raw, rs.data(), rs.size());
    return d;
}

void no_installation(Env &env, const fs::path &home)
{
    env.set("HOME", home.string());
    env.unset("VTI_ACCESS_DIR"), env.unset("VTI_ACCESS_CC"), env.unset("WINEPREFIX"), env.unset("XDG_CONFIG_HOME");
}
} // namespace

TEST(accessfiles_resource_folder)
{
    TempDir t("res");
    write(t.dir / "RemoteDevice.XML", REMOTE); // names are found case-insensitively
    write(t.dir / "knob.png", Bytes{0x89, 'P', 'N', 'G'});
    write(t.dir / "notes.txt", std::string("ignored"));
    Env env;
    no_installation(env, t.dir / "home");
    std::string err;
    auto a = AccessFiles::open(t.dir.string(), err);
    CHECK(a != nullptr);
    if (!a)
        return;
    CHECK(a->get("remotedevice.xml").size() == REMOTE.size() && a->get("REMOTEDEVICE.xml").size() == REMOTE.size());
    CHECK(a->get("knob.png").size() == 4 && a->get("notes.txt").empty() && a->get("missing.xml").empty());
    CHECK(a->names().size() == 2 && a->source() == t.dir.string());
    fs::create_directories(t.dir / "empty");
    CHECK(!AccessFiles::open((t.dir / "empty").string(), err) && !err.empty());
}

TEST(accessfiles_bundle_dumps)
{
    TempDir t("bundle");
    fs::path bundle = t.dir / "Virus Control.bundle";
    write(bundle / "Contents" / "Resources" / "remotedevice.xml", REMOTE);
    // the bundle binary: 0x400 bytes of 7-bit data (the default page B), the init single, an init multi
    Bytes bin(0x500, 0x05);
    Bytes single = fake_dump(0x10, 524, 249, " -Init-"), multi = fake_dump(0x11, 267, 13, "MultiCtrl!");
    bin.insert(bin.end(), single.begin(), single.end());
    bin.insert(bin.end(), 7, 0x00);
    bin.insert(bin.end(), multi.begin(), multi.end());
    write(bundle / "Contents" / "MacOS" / "Virus Control", bin);
    Env env;
    no_installation(env, t.dir);
    env.set("VTI_ACCESS_DIR", bundle.string()); // the bundle itself: open() goes to Contents/Resources
    std::string err;
    auto a = AccessFiles::open("", err);
    CHECK(a != nullptr);
    if (!a)
        return;
    CHECK(a->init_single() == single);
    CHECK(a->init_multi() == multi);
    Bytes page = a->default_page_b();
    CHECK(page.size() == 0x100 && page[0] == 0x05);
    // a folder holding the bundle is found too
    auto b = AccessFiles::open(t.dir.string(), err);
    CHECK(b && b->init_single() == single);
}

TEST(accessfiles_no_dumps)
{
    TempDir t("nodump");
    write(t.dir / "remotedevice.xml", REMOTE);
    Env env;
    no_installation(env, t.dir / "home");
    std::string err;
    auto a = AccessFiles::open(t.dir.string(), err);
    CHECK(a && a->init_single().empty() && a->init_multi().empty() && a->default_page_b().empty());
}

TEST(accessfiles_pe_image)
{
    TempDir t("pe");
    fs::path dll = t.dir / "Virus Control.dll";
    write(dll, fake_pe({{"REMOTEDEVICE.XML", REMOTE}, {"EDITOR.XML", "<Editor/>"}}));
    Env env;
    no_installation(env, t.dir);
    std::string err;
    auto a = AccessFiles::open(dll.string(), err); // the DLL itself
    CHECK(a != nullptr);
    if (!a)
        return;
    CHECK(std::string(a->get("remotedevice.xml").begin(), a->get("remotedevice.xml").end()) == REMOTE);
    CHECK(a->get("editor.xml").size() == 9 && a->names().size() == 2);
    // open() with the folder that holds the DLL (the Windows / Wine layout)
    auto b = AccessFiles::open(t.dir.string(), err);
    CHECK(b && b->source() == dll.string());
    // and through WINEPREFIX
    fs::path wine = t.dir / "prefix";
    write(wine / "drive_c/Program Files/Access Music/Virus TI/Common/Virus Control.dll", fake_pe({{"REMOTEDEVICE.XML", REMOTE}}));
    env.set("WINEPREFIX", wine.string());
    auto c = AccessFiles::open("", err);
    CHECK(c && c->get("remotedevice.xml").size() == REMOTE.size());
}

TEST(accessfiles_pe_errors)
{
    TempDir t("pebad");
    Env env;
    no_installation(env, t.dir / "home");
    std::string err;
    // open() goes on to the standard places after the hint and reports the last error, so only the failure and a
    // message are checked; the PE parser's error paths run all the same
    auto fails = [&](const fs::path &p, const char *) {
        err.clear();
        return !AccessFiles::open(p.string(), err) && !err.empty();
    };
    write(t.dir / "text.dll", std::string(100, 'x'));
    CHECK(fails(t.dir / "text.dll", "not a PE"));
    write(t.dir / "nores.dll", fake_pe({{"REMOTEDEVICE.XML", REMOTE}}, false));
    CHECK(fails(t.dir / "nores.dll", "no resources"));
    write(t.dir / "other.dll", fake_pe({{"SOMETHING.XML", "<x/>"}}));
    CHECK(fails(t.dir / "other.dll", "does not contain"));
    CHECK(fails(t.dir / "missing.dll", "cannot read"));
}

TEST(accessfiles_open_nothing)
{
    TempDir t("none");
    Env env;
    no_installation(env, t.dir);
    std::string err;
    CHECK(!AccessFiles::open("", err) && !err.empty());
    err.clear();
    CHECK(!AccessFiles::open_control_center(err) && !err.empty());
    // the installer's note of where it put the Access software (XDG config), pointing to a fake folder
    write(t.dir / "access" / "remotedevice.xml", REMOTE);
    write(t.dir / ".config" / "tirevive" / "access-dir", (t.dir / "access").string() + "\n");
    auto a = AccessFiles::open("", err);
    CHECK(a && a->source() == (t.dir / "access").string());
}

TEST(accessfiles_control_center)
{
    TempDir t("cc");
    Env env;
    no_installation(env, t.dir);
    std::string err;
    // VTI_ACCESS_CC: a folder with the Control Center's resources (it needs engine.xml and editor.xml)
    write(t.dir / "cc" / "engine.xml", std::string("<Engine/>"));
    env.set("VTI_ACCESS_CC", (t.dir / "cc").string());
    CHECK(!AccessFiles::open_control_center(err)); // editor.xml missing
    write(t.dir / "cc" / "editor.xml", std::string("<Editor/>"));
    auto a = AccessFiles::open_control_center(err);
    CHECK(a && a->get("engine.xml").size() == 9);
    // next to the plug-in's DLL in the installer's Common folder (an exe with both resources)
    env.unset("VTI_ACCESS_CC");
    fs::path common = t.dir / "prefix/drive_c/Program Files/Access Music/Virus TI/Common";
    write(common / "Virus Control.dll", fake_pe({{"REMOTEDEVICE.XML", REMOTE}}));
    write(common / "Virus Control Center.exe", fake_pe({{"ENGINE.XML", "<Engine/>"}, {"EDITOR.XML", "<Editor/>"}}));
    env.set("WINEPREFIX", (t.dir / "prefix").string());
    auto b = AccessFiles::open_control_center(err);
    CHECK(b && b->source() == (common / "Virus Control Center.exe").string());
}
#endif

// the classic oscillator's 64 waves (64 x 64 floats in the plug-in binary, row 0 = one sine period): found by
// content, so the Windows DLL and the macOS bundle binary both work; anything implausible is rejected
static std::vector<uint8_t> waves_blob(size_t junk_before, float row1_value)
{
    std::vector<uint8_t> b(junk_before, 0x5A);
    for (int r = 0; r < 64; r++)
        for (int i = 0; i < 64; i++) {
            float v = r == 0 ? (float)std::sin(2 * 3.14159265358979 * i / 64) : (r == 1 ? row1_value : 0.25f * (r % 4));
            uint8_t f[4];
            memcpy(f, &v, 4);
            b.insert(b.end(), f, f + 4);
        }
    b.insert(b.end(), 100, 0xA5);
    return b;
}

TEST(accessfiles_osc_waves_by_content)
{
    auto w = vti::find_osc_waves(waves_blob(4096, 0.5f));
    CHECK(w.size() == 64 * 64);
    if (w.size() == 64 * 64) {
        CHECK(w[0] == 0.0f && std::fabs(w[16] - 1.0f) < 1e-6f); // row 0: sine, peak at index 16
        CHECK(w[64] == 0.5f && w[64 * 5] == 0.25f);
    }
    CHECK(vti::find_osc_waves(waves_blob(4098, 0.5f)).size() == 64 * 64); // not 4-byte aligned in the file
    CHECK(vti::find_osc_waves(waves_blob(4096, 5.0f)).empty());            // a row out of +-1: not the table
    CHECK(vti::find_osc_waves(std::vector<uint8_t>(20000, 0)).empty());     // zeros are no sine
    auto cut = waves_blob(16, 0.5f);
    cut.resize(16 + 64 * 64 * 4 - 8); // truncated
    CHECK(vti::find_osc_waves(cut).empty());
}

// macOS: the resources are in Contents/Resources, the table in the bundle binary Contents/MacOS/Virus Control
TEST(accessfiles_osc_waves_mac_bundle)
{
    TempDir t("bundlewaves");
    fs::path bundle = t.dir / "Virus Control.bundle";
    write(bundle / "Contents" / "Resources" / "remotedevice.xml", REMOTE);
    write(bundle / "Contents" / "MacOS" / "Virus Control", waves_blob(0x1234, 0.5f));
    Env env;
    no_installation(env, t.dir);
    env.set("VTI_ACCESS_DIR", bundle.string());
    std::string err;
    auto a = AccessFiles::open("", err);
    CHECK(a != nullptr);
    if (!a)
        return;
    auto w = a->osc_waves();
    CHECK(w.size() == 64 * 64 && w[64] == 0.5f);
    // a bundle without the table: no waves (the display falls back to the sine)
    write(bundle / "Contents" / "MacOS" / "Virus Control", Bytes(0x2000, 0x5A));
    CHECK(AccessFiles::open("", err)->osc_waves().empty());
}
