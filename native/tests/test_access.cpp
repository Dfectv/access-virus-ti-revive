// tests on the user's own Access files (skipped without them, e.g. in CI): every parameter's MIDI encoding, the
// dump fast path on the real single dump, init patch/upgrader, automation map, editor layout and renderer
#include "access/accessfiles.h"
#include "access/library.h"
#include "check.h"
#include "editor/editorrender.h"
#include "model/remotedevice.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <set>

using namespace vti;
using Bytes = std::vector<uint8_t>;

static AccessFiles *files()
{
    static std::unique_ptr<AccessFiles> f;
    static bool tried = false;
    if (!tried) {
        tried = true;
        std::string err;
        f = AccessFiles::open("", err);
    }
    if (!f)
        SKIP("no Access installation");
    return f.get();
}

static const RemoteDevice &dev()
{
    static RemoteDevice d;
    static bool loaded = false;
    AccessFiles *a = files();
    if (!loaded) {
        std::string err;
        loaded = d.load(a->get("remotedevice.xml"), err);
        CHECK(loaded);
    }
    return d;
}

TEST(access_encode_decode_all_parameters)
{
    const RemoteDevice &d = dev();
    int checked = 0, missing = 0;
    for (auto &p : d.parameters) {
        int part = p.part == "none" ? 16 : 3;
        for (int v : {p.min, (p.min + p.max) / 2, p.max}) {
            Bytes m = d.encode(p.path, v, part);
            if (m.empty())
                continue; // no realtime message (dump-only parameters)
            uint8_t buf[64];
            size_t n = d.encode_into(p.path, v, part, buf, sizeof buf);
            CHECK(n == m.size() && std::equal(m.begin(), m.end(), buf));
            bool found = false;
            for (auto &c : d.decode(m))
                found |= c.path == p.path && c.value == v;
            if (!found && ++missing <= 5)
                std::fprintf(stderr, "  %s = %d does not decode back\n", p.path.c_str(), v);
            checked++;
        }
    }
    CHECK(checked > 500);
    CHECK_EQ(missing, 0);
}

TEST(access_single_dump_fast_path)
{
    const RemoteDevice &d = dev();
    const MidiMessage *t = d.message("VirusTI_Singledump");
    CHECK(t != nullptr);
    if (!t)
        return;
    Bytes init = files()->init_single();
    RemoteDevice::upgrade_single(init);
    CHECK_EQ(init.size(), 524u);
    std::map<std::string, int> vals;
    std::string err;
    CHECK(d.decode_dump(*t, init, vals, err));
    CHECK(t->field_bytes.size() > 400);
    int bad = 0;
    for (auto &kv : t->field_bytes)
        for (int v : {0, 1, 63, 127}) {
            Bytes a = init, b = init;
            d.update_dump(*t, a, {{kv.first, v}});
            d.update_dump(*t, b, {{kv.first, v}, {"zz/none", 0}});
            bad += a != b;
            std::map<std::string, int> back;
            CHECK(d.decode_dump(*t, a, back, err)); // checksums stay valid
        }
    CHECK_EQ(bad, 0);
    int fp = RemoteDevice::single_fingerprint(init);
    CHECK(fp >= 0 && fp < 16384);
}

TEST(access_init_messages)
{
    auto msgs = dev().init_messages();
    CHECK(!msgs.empty());
    for (auto &m : msgs)
        CHECK(!m.empty() && (m[0] & 0x80));
}

TEST(access_automation_map)
{
    std::vector<AutomationEntry> a;
    std::string err;
    CHECK(load_automation(files()->get("automation.xml"), a, err));
    CHECK(a.size() >= 200);
    std::set<int> ids;
    for (auto &e : a) {
        CHECK(ids.insert(e.id).second); // unique VST parameter ids
        if (!dev().find(e.path) && e.path.rfind("Softknobs/", 0) != 0)
            std::fprintf(stderr, "  automation path not in the device: %s\n", e.path.c_str());
    }
}

// a canvas that draws nothing and records what is drawn where
struct RecordCanvas : Canvas {
    int depth = 0, max_depth = 0, draws = 0, outside = 0;
    std::vector<std::array<int, 4>> clips;
    void *image(const std::string &) override { return nullptr; }
    int image_width(void *) override { return 0; }
    int image_height(void *) override { return 0; }
    void blit(void *, int, int, int, int, int, int, double) override { draws++; }
    void fill(int x, int y, int w, int h, Rgba) override { note(x, y, x + w, y + h); }
    void line(double x0, double y0, double x1, double y1, Rgba) override
    {
        note((int)std::min(x0, x1), (int)std::min(y0, y1), (int)std::max(x0, x1), (int)std::max(y0, y1));
    }
    void push_clip(int x, int y, int w, int h) override
    {
        clips.push_back({x, y, x + w, y + h});
        max_depth = std::max(max_depth, ++depth);
    }
    void pop_clip() override
    {
        clips.pop_back();
        depth--;
    }
    void note(int x0, int y0, int x1, int y1)
    {
        draws++;
        if (!clips.empty()) { // inside a display: nothing outside its element (1 px for anti-aliased lines)
            auto &c = clips.back();
            if (x0 < c[0] - 1 || y0 < c[1] - 1 || x1 > c[2] + 1 || y1 > c[3] + 1)
                outside++;
        }
    }
};

TEST(access_editor_render_deps_and_clip)
{
    AccessFiles *a = files();
    static RemoteDevice ed;
    std::string err;
    CHECK(ed.load(a->get("editordevice.xml"), err));
    EditorLayout layout;
    CHECK(layout.load(a->get("editor.xml"), err));
    Conditions cond(dev(), ed);
    EditorRenderer r(layout, cond, dev(), ed);
    // extreme values: the longest envelopes, highest cutoff/resonance
    r.values = [&](const std::string &p, int) {
        if (p == "VirusControl/SelectedPage")
            return 3; // FILTER page
        if (p == "VirusControl/InitialSyncProgress")
            return 127; // start-up sync done
        const Parameter *q = cond.find(p);
        bool extreme = p.rfind("Filter", 0) == 0 || p.rfind("Envelope", 0) == 0;
        return q ? (extreme ? q->max : q->def) : 0;
    };
    EditorRenderer::Deps deps;
    r.deps = &deps;
    RecordCanvas c;
    r.render(c);
    CHECK_EQ(c.depth, 0);   // every push_clip popped
    CHECK(c.max_depth >= 1); // the page has displays
    CHECK(c.draws > 0);
    CHECK(!deps.cond.empty());
    if (getenv("VTITESTS_VERBOSE"))
        for (auto &p : deps.cond)
            std::fprintf(stderr, "  cond: %s\n", p.c_str());
    CHECK(deps.cond.count("VirusControl/SelectedPage")); // page switch = structure change
    CHECK(!deps.rects.empty());
    if (c.outside)
        std::fprintf(stderr, "  %d display strokes outside their element (the canvas clips them)\n", c.outside);
    for (auto &kv : deps.rects)
        for (auto &rc : kv.second)
            CHECK(rc[2] > 0 && rc[3] > 0);
}

TEST(library_save_load_round_trip)
{
    // no Access files needed: synthetic single dumps through the SMF writer and reader
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / ("vtitests-lib-" + std::to_string(::time(nullptr)));
    fs::create_directories(dir);
    PatchLibrary lib;
    lib.name = "Test";
    lib.path = (dir / "Test.mid").string();
    for (int i = 0; i < 3; i++) {
        Bytes m(524, (uint8_t)(i + 1));
        m[0] = 0xF0, m[1] = 0x00, m[2] = 0x20, m[3] = 0x33, m[4] = 0x01, m[5] = 0x10, m[6] = 0x10, m[7] = 0x01;
        m[8] = (uint8_t)i, m[523] = 0xF7;
        lib.dumps.push_back(m);
    }
    std::string err;
    CHECK(save_library(lib, err));
    auto libs = load_libraries(dir.string());
    CHECK_EQ(libs.size(), 1u);
    if (libs.size() == 1) {
        CHECK(libs[0].name == "Test");
        CHECK_EQ(libs[0].dumps.size(), 3u);
        for (size_t i = 0; i < libs[0].dumps.size() && i < 3; i++)
            CHECK(libs[0].dumps[i].size() == 524 && libs[0].dumps[i][9] == lib.dumps[i][9]);
    }
    fs::remove_all(dir);
}
