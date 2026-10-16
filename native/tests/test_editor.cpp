// EditorRenderer on a small hand-written layout (the editor.xml format, no Access content) with images drawn in
// code: every element type the renderer knows, frames of knob strips, inactive and hidden controls, tiled bitmaps,
// bitmap-font text, the displays (filter, oscillators, envelope, arpeggiator), the patch browser, hit tests and the
// repaint dependencies. Runs in CI (no Access files needed).
#include "check.h"
#include "editor/editorrender.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <map>
#include <memory>
using namespace vti;
using Bytes = std::vector<uint8_t>;

static const char *DEVICE = R"(<?xml version="1.0"?>
<Remotedevice device="Test" version="1">
 <ParameterSets>
  <ParameterSet name="Osc">
   <Parameter name="Shape" minValue="0" maxValue="127" defaultValue="64" part="all"/>
   <Parameter name="Mode" minValue="0" maxValue="3" defaultValue="0" valuelist="Modes" part="all"/>
   <Parameter name="Wave" minValue="0" maxValue="9" defaultValue="0" valuelist="Waves" part="all"/>
   <Parameter name="Gated" minValue="0" maxValue="127" defaultValue="0" condition="Mode Is Two" part="all"/>
   <Parameter name="Plain" minValue="2" maxValue="4" defaultValue="2" part="all"/>
  </ParameterSet>
  <ParameterSet name="Part"><Parameter name="Volume" minValue="0" maxValue="127" defaultValue="100" part="all"/></ParameterSet>
  <ParameterSet name="Filter 1">
   <Parameter name="Cutoff" minValue="0" maxValue="127" defaultValue="64" part="all"/>
   <Parameter name="Resonance" minValue="0" maxValue="127" defaultValue="32" part="all"/>
  </ParameterSet>
  <ParameterSet name="Oscillator 1">
   <Parameter name="Shape" minValue="0" maxValue="127" defaultValue="0" part="all"/>
   <Parameter name="Pulsewidth" minValue="0" maxValue="127" defaultValue="0" part="all"/>
  </ParameterSet>
  <ParameterSet name="Oscillator 1 HyperSaw">
   <Parameter name="DetuneSpread" minValue="0" maxValue="127" defaultValue="64" part="all"/>
   <Parameter name="Density" minValue="0" maxValue="127" defaultValue="64" part="all"/>
  </ParameterSet>
  <ParameterSet name="Env">
   <Parameter name="Attack" minValue="0" maxValue="127" defaultValue="10" part="all"/>
  </ParameterSet>
  <ParameterSet name="Soft Knobs">
   <Parameter name="Destination 1" minValue="0" maxValue="127" defaultValue="0" part="all"/>
  </ParameterSet>
  <ParameterSet name="Multi Parameters">
   <Parameter name="Panel Part" minValue="0" maxValue="15" defaultValue="0" part="none"/>
  </ParameterSet>
 </ParameterSets>
 <Valuelists>
  <Valuelist name="Modes" type="plain"><Entry index="0">Off</Entry><Entry index="1"> Saw</Entry><Entry index="2">Pulse</Entry><Entry index="3"></Entry></Valuelist>
  <Valuelist name="Waves" type="mapped"><Entry index="0" value="9">Nine</Entry><Entry index="1" value="4">Four</Entry><Entry index="2" value="40">Out</Entry><Entry index="3" value="5">-GGrey</Entry></Valuelist>
 </Valuelists>
 <Conditionsets>
  <Condition name="Mode Is Two"><Test type="equals" param="Osc/Mode" minvalue="2"/></Condition>
  <Condition name="Mode Is Zero"><Test type="equals" param="Osc/Mode" minvalue="0"/></Condition>
  <Condition name="Page Two"><Test type="equals" param="VirusControl/Page" minvalue="2"/></Condition>
 </Conditionsets>
</Remotedevice>)";

static const char *EDITOR_DEVICE = R"(<?xml version="1.0"?>
<Remotedevice device="Editor" version="1">
 <ParameterSets>
  <ParameterSet name="VirusControl"><Parameter name="Page" minValue="0" maxValue="9" defaultValue="0" part="none"/></ParameterSet>
  <ParameterSet name="Softknobs"><Parameter name="Softknob 1" minValue="0" maxValue="127" defaultValue="0" part="all"/></ParameterSet>
  <ParameterSet name="Browser 1"><Parameter name="Selected Bank" minValue="0" maxValue="3" defaultValue="0" part="none"/></ParameterSet>
 </ParameterSets>
</Remotedevice>)";

// sizes: knob strip 4 frames of 10 px, radio/button strips 2 frames of 8 px, font 96 glyphs of 6 x 7 px
static const char *LAYOUT = R"(<?xml version="1.0"?>
<Editor>
 <Bitmaps>
  <bmp00001 Name="tile.png" Tile="true"><TileInsetRect left="2" top="2" right="2" bottom="2"/></bmp00001>
  <bmp00002 Name="knob.png"/>
 </Bitmaps>
 <Frame width="700" height="400" parameter="Multi Parameters/Panel Part">
  <Container Part="0"><Size left="0" top="0" width="700" height="400"/>
   <StaticText Name="Hi" ControlFont="font.png" Align="Top Left"><Size left="5" top="5" width="40" height="10"/></StaticText>
   <StaticText Name="Box" Transparent="false" ControlFont="font.png" Align="Bottom Right"><BackColor red="10" green="20" blue="30"/><Size left="50" top="5" width="40" height="10"/></StaticText>
   <StaticText Name="Mid" ControlFont="font.png" Align="Middle Center" Condition="Mode Is Zero"><Size left="95" top="5" width="40" height="10"/></StaticText>
   <StaticText Name="Hidden" ControlFont="font.png" Condition="Page Two"><Size left="140" top="5" width="40" height="10"/></StaticText>
   <Knob Parameter="Osc/Shape" NormalBack="knob.png" PressedBack="knobp.png"><Size left="5" top="20" width="10" height="10"/></Knob>
   <Knob Parameter="Osc/Gated" NormalBack="knob.png" DisabledBack="knobd.png"><Size left="20" top="20" width="10" height="10"/></Knob>
   <Knob Parameter="Osc/Gated" NormalBack="knob.png"><Size left="35" top="20" width="10" height="10"/></Knob>
   <Knob Parameter="Softknobs/Softknob 1" NormalBack="knob.png"><Size left="50" top="20" width="10" height="10"/></Knob>
   <RadioButton Parameter="Osc/Mode" OnValue="1" NormalBack="radio.png"><Size left="65" top="20" width="8" height="8"/></RadioButton>
   <Button Parameter="Osc/Mode" NormalBack="radio.png" Enabled="false"><Size left="80" top="20" width="8" height="8"/></Button>
   <VUSlider Parameter="Part/Volume" Part="2" NormalBack="vu.png" NormalHandle="handle.png"><Size left="5" top="35" width="50" height="6"/></VUSlider>
   <Menu Parameter="Osc/Mode" NormalBack="tile.png" ControlFont="font.png" Align="Middle Left"><Size left="60" top="35" width="30" height="11"/></Menu>
   <TextSlider Parameter="Osc/Shape" ControlFont="font.png"><Size left="95" top="35" width="40" height="10"/></TextSlider>
   <SearchEdit Parameter="Osc/Mode" ControlFont="font.png"><Size left="140" top="35" width="40" height="10"/></SearchEdit>
   <NameEdit ControlFont="font.png" Parameter="Osc/Plain"><Size left="185" top="35" width="60" height="10"/></NameEdit>
   <HierarchicMenu Parameter="Osc/Wave" ControlFont="font.png" Align="No Label"><Size left="250" top="35" width="40" height="10"/></HierarchicMenu>
   <LP4Response Parameter="Filter 1/Cutoff" NormalHandle="handle.png"><Size left="5" top="50" width="80" height="30"/></LP4Response>
   <HP4Response Parameter="Filter 1/Cutoff" NormalHandle="handle.png"><Size left="90" top="50" width="80" height="30"/></HP4Response>
   <BP4Response Parameter="Filter 1/Cutoff"><Size left="175" top="50" width="80" height="30"/></BP4Response>
   <BS4Response Parameter="Filter 1/Cutoff"><Size left="260" top="50" width="80" height="30"/></BS4Response>
   <OscClassicShape Parameter="Oscillator 1/Pulsewidth"><Size left="5" top="85" width="40" height="20"/></OscClassicShape>
   <OscHypersawShape Parameter="Oscillator 1 HyperSaw/DetuneSpread"><ForeColor red="1" green="2" blue="3"/><Size left="50" top="85" width="40" height="20"/></OscHypersawShape>
   <ADSTREnvelope Parameter="Env/Attack" NormalHandle="handle.png"><ForeColor red="200" green="0" blue="0"/><Size left="95" top="85" width="200" height="40"/></ADSTREnvelope>
   <ArpEdit Parameter="Arpeggiator User Pattern/Step 1 Bitfield"><ForeColor red="0" green="0" blue="200"/><Size left="5" top="130" width="640" height="130"/></ArpEdit>
   <PatchBrowser Parameter="Browser 1/Selected Bank" ControlFont="font.png"><Size left="0" top="265" width="680" height="180"/></PatchBrowser>
  </Container>
  <Container><Size left="650" top="0" width="50" height="50"/>
   <Knob Parameter="Osc/Shape" NormalBack="knob.png"><Size left="0" top="0" width="10" height="10"/></Knob>
  </Container>
 </Frame>
</Editor>)";

struct Img {
    int w, h;
};

// records every drawing call; images are only sizes (the renderer reads nothing else)
struct RecCanvas : Canvas {
    std::map<std::string, Img> imgs;
    struct Blit {
        std::string img;
        int sx, sy, dx, dy, w, h;
        double alpha;
    };
    std::vector<Blit> blits;
    struct Fill {
        int x, y, w, h;
        Rgba c;
    };
    std::vector<Fill> fills;
    int lines = 0, clips = 0, depth = 0, max_depth = 0;
    std::map<void *, std::string> names;

    void *image(const std::string &name) override
    {
        auto it = imgs.find(name);
        if (it == imgs.end())
            return nullptr;
        names[&it->second] = name;
        return &it->second;
    }
    int image_width(void *i) override { return static_cast<Img *>(i)->w; }
    int image_height(void *i) override { return static_cast<Img *>(i)->h; }
    void blit(void *i, int sx, int sy, int dx, int dy, int w, int h, double alpha) override
    {
        blits.push_back({names[i], sx, sy, dx, dy, w, h, alpha});
    }
    void fill(int x, int y, int w, int h, Rgba c) override { fills.push_back({x, y, w, h, c}); }
    void line(double, double, double, double, Rgba) override { lines++; }
    void push_clip(int, int, int, int) override { clips++, depth++, max_depth = std::max(max_depth, depth); }
    void pop_clip() override { depth--; }

    std::vector<Blit> at(const std::string &img, int dx, int dy) const
    {
        std::vector<Blit> out;
        for (auto &b : blits)
            if (b.img == img && b.dx == dx && b.dy == dy)
                out.push_back(b);
        return out;
    }
    size_t count(const std::string &img) const
    {
        return (size_t)std::count_if(blits.begin(), blits.end(), [&](const Blit &b) { return b.img == img; });
    }
    void clear() { blits.clear(), fills.clear(), lines = clips = depth = max_depth = 0; }
};

struct Rig {
    RemoteDevice dev, ed;
    EditorLayout layout;
    std::unique_ptr<Conditions> cond;
    std::map<std::pair<std::string, int>, int> vals;
    RecCanvas canvas;
    std::unique_ptr<EditorRenderer> r;

    Rig()
    {
        std::string err;
        CHECK(dev.load(Bytes(DEVICE, DEVICE + strlen(DEVICE)), err));
        CHECK(ed.load(Bytes(EDITOR_DEVICE, EDITOR_DEVICE + strlen(EDITOR_DEVICE)), err));
        CHECK(layout.load(Bytes(LAYOUT, LAYOUT + strlen(LAYOUT)), err));
        cond = std::make_unique<Conditions>(dev, ed);
        r = std::make_unique<EditorRenderer>(layout, *cond, dev, ed);
        r->values = [this](const std::string &path, int part) {
            for (int p : {part, -1}) {
                auto it = vals.find({path, p});
                if (it != vals.end())
                    return it->second;
            }
            return cond->default_value(path);
        };
        canvas.imgs = {{"knob.png", {10, 40}}, {"knobp.png", {10, 40}}, {"knobd.png", {10, 40}},
                       {"radio.png", {8, 16}}, {"vu.png", {50, 24}},    {"handle.png", {4, 4}},
                       {"tile.png", {8, 11}},  {"font.png", {6, 7 * 96}}};
    }
};

TEST(editor_layout_load)
{
    Rig g;
    CHECK(g.layout.width == 700 && g.layout.height == 400);
    CHECK(g.layout.frame_parameter == "Multi Parameters/Panel Part");
    CHECK(g.layout.elements.size() == 2);
    CHECK(g.layout.bitmaps.at("bmp00002") == "knob.png");
    CHECK(g.layout.tiles.count("tile.png") && g.layout.tiles.at("tile.png").left == 2);
    const Element &box = g.layout.elements[0].children[1];
    CHECK(box.back.r == 10 && box.back.g == 20 && box.back.b == 30 && box.back.a == 255);
    CHECK(box.x == 50 && box.w == 40);
    std::string err;
    EditorLayout bad;
    CHECK(!bad.load(Bytes{'<', 'x'}, err) && !err.empty());
    const char *empty = "<Editor><Frame width=\"10\" height=\"10\"/></Editor>";
    CHECK(!bad.load(Bytes(empty, empty + strlen(empty)), err));
}

TEST(editor_render_controls)
{
    Rig g;
    g.vals[{"Osc/Shape", 0}] = 127; // last frame of the knob strip
    g.vals[{"Osc/Mode", 0}] = 1;    // radio on, button on, menu " Saw"
    g.r->render(g.canvas);
    auto &c = g.canvas;
    // knob: frame 3 of 4 (sy = 30)
    auto k = c.at("knob.png", 5, 20);
    CHECK(k.size() == 1 && k[0].sy == 30 && k[0].alpha == 1.0);
    // the inactive knob with a DisabledBack strip uses it at full opacity; without one it is translucent
    CHECK(c.at("knobd.png", 20, 20).size() == 1 && c.at("knobd.png", 20, 20)[0].alpha == 1.0);
    auto gated = c.at("knob.png", 35, 20);
    CHECK(gated.size() == 1 && gated[0].alpha < 1.0);
    // soft knob without a destination: translucent
    auto sk = c.at("knob.png", 50, 20);
    CHECK(sk.size() == 1 && sk[0].alpha < 1.0);
    // radio and button: frame 1 (selected / on)
    CHECK(c.at("radio.png", 65, 20).size() == 1 && c.at("radio.png", 65, 20)[0].sy == 8);
    CHECK(c.at("radio.png", 80, 20).size() == 1 && c.at("radio.png", 80, 20)[0].sy == 8);
    // VU slider of part 3: level frame 0 (no meter), handle at volume 100 of 0..127 over 50 - 4 px
    CHECK(c.at("vu.png", 5, 35).size() == 1);
    CHECK(c.at("handle.png", 5 + (int)std::lround(100.0 / 127 * 46), 36).size() == 1);
    // the filled box (Transparent="false", no bitmap)
    bool box = false;
    for (auto &f : c.fills)
        box |= f.x == 50 && f.y == 5 && f.w == 40 && f.h == 10 && f.c.r == 10;
    CHECK(box);
    // the hidden text (condition false) draws no glyphs at its place
    for (auto &b : c.blits)
        CHECK(!(b.img == "font.png" && b.dx >= 140 && b.dx < 180 && b.dy < 20));
    // the second container's knob is drawn too
    CHECK(c.at("knob.png", 650, 0).size() == 1);
}

TEST(editor_render_text)
{
    Rig g;
    g.vals[{"Osc/Mode", 0}] = 1;
    g.r->patch_name = [](int part) { return "Patch" + std::to_string(part); };
    g.r->live_text = [](const std::string &p) { return p == "Osc/Shape" ? std::string("LIVE") : std::string(); };
    g.r->render(g.canvas);
    auto &c = g.canvas;
    auto glyph = [&](int dx, int dy, char ch) {
        for (auto &b : c.blits)
            if (b.img == "font.png" && b.dx == dx && b.dy == dy && b.sy == (ch - 32) * 7)
                return true;
        return false;
    };
    CHECK(glyph(5, 5, 'H') && glyph(11, 5, 'i'));                  // "Hi" top left
    CHECK(glyph(50 + 40 - 18, 5 + 10 - 7, 'B'));                   // "Box" bottom right
    // "Mid" is shown only while Osc/Mode is 0: hidden here
    CHECK(!glyph(95 + (40 - 18) / 2, 5 + (10 - 7 - 1) / 2, 'M'));
    // the menu shows its entry trimmed ("Saw"), inside the tile insets (left 2, top 2)
    CHECK(glyph(60 + 2, 35 + 2 + (11 - 2 - 2 - 7 - 1) / 2, 'S'));
    CHECK(glyph(95 + (40 - 24) / 2, 35 + (10 - 7 - 1) / 2, 'L')); // TextSlider: live text "LIVE"
    // NameEdit: the part's patch name
    CHECK(glyph(185 + (60 - 36) / 2, 35 + (10 - 7 - 1) / 2, 'P'));
    // SearchEdit shows nothing while its text equals the valuelist entry (" Saw")
    for (auto &b : c.blits)
        CHECK(!(b.img == "font.png" && b.dx >= 140 && b.dx < 180 && b.dy >= 35 && b.dy < 45));
    // "No Label": the bitmap carries the text
    for (auto &b : c.blits)
        CHECK(!(b.img == "font.png" && b.dx >= 250 && b.dx < 290 && b.dy >= 35 && b.dy < 45));
    // the tiled menu bitmap is drawn in nine parts (more than one blit for a 30 px wide control from 8 px)
    size_t tiles = 0;
    for (auto &b : c.blits)
        tiles += b.img == "tile.png";
    CHECK(tiles > 3);
}

TEST(editor_render_displays)
{
    Rig g;
    auto &c = g.canvas;
    g.r->render(g.canvas);
    CHECK(c.clips == 8 && c.depth == 0 && c.max_depth == 1); // 4 filters, 2 oscillators, envelope, arp (no clip leak)
    CHECK(c.lines > 20);                                       // filter curves + envelope line
    CHECK(c.count("handle.png") >= 2 + 5 + 1);                 // 2 filter handles, 5 envelope handles, VU handle
    const size_t base_fills = c.fills.size();
    // classic oscillator: sine, saw and pulse draw different columns
    std::vector<size_t> n;
    for (int shape : {0, 64, 127}) {
        g.vals[{"Oscillator 1/Shape", 0}] = shape;
        c.clear();
        g.r->render(c);
        n.push_back(c.fills.size());
    }
    CHECK(n[0] > 0 && n[1] > 0 && n[2] > 0);
    // hypersaw: more saws with a higher density
    auto saws = [&](int density) {
        g.vals[{"Oscillator 1 HyperSaw/Density", 0}] = density;
        c.clear();
        g.r->render(c);
        size_t k = 0;
        for (auto &f : c.fills)
            k += f.c.r == 1 && f.c.g == 2 && f.c.b == 3;
        return k;
    };
    CHECK(saws(0) == 1);
    CHECK(saws(127) == 9);
    CHECK(saws(100) > saws(32));
    // arpeggiator: steps with their bit set show a framed bar
    g.vals[{"Arpeggiator User Pattern/Step 1 Bitfield", 0}] = 1;
    g.vals[{"Arpeggiator User Pattern/Step 1 Velocity", 0}] = 100;
    g.vals[{"Arpeggiator User Pattern/Step 1 Length", 0}] = 64;
    g.vals[{"Arpeggiator User Pattern/Step 3 Bitfield", 0}] = 1;
    g.vals[{"Arpeggiator User Pattern/Step 3 Velocity", 0}] = 1;
    c.clear();
    g.r->render(c);
    size_t frame = 0;
    for (auto &f : c.fills)
        frame += f.c.r == 192 && f.c.g == 192;
    CHECK(frame == 8); // two steps x four border lines
    CHECK(base_fills > 0);
    CHECK(EditorRenderer::arp_step_width(0) == 0 && EditorRenderer::arp_step_width(127) == 18);
    CHECK(EditorRenderer::arp_step_width(64) == 10 && EditorRenderer::arp_step_width(-5) == 0);
    int cx = 0, bottom = 0;
    EditorRenderer::arp_step_geometry(g.layout.elements[0].children[23], 5, 130, 2, cx, bottom);
    CHECK(cx == 5 + 1 + 2 * 20 && bottom == 130 + 130 - 1);
}

TEST(editor_render_browser)
{
    Rig g;
    g.r->bank_patch = [](int bank, int program) { return "B" + std::to_string(bank) + "P" + std::to_string(program); };
    g.r->browser_match = [](int, int program) { return program % 2 == 0; };
    g.r->selected[0] = 5;
    g.r->render(g.canvas);
    auto &c = g.canvas;
    // 128 entries: program digits (filled pixels) and names; odd programs dimmed
    size_t dim = 0, full = 0;
    for (auto &b : c.blits)
        if (b.img == "font.png" && b.dy >= 265)
            (b.alpha < 1.0 ? dim : full)++;
    CHECK(dim > 0 && full > 0);
    CHECK(c.lines >= 4); // the frame around the selected entry
    Hit h = g.r->hit_test(30, 270);
    CHECK(h.element && h.element->type == "PatchBrowser");
    CHECK(EditorRenderer::browser_entry(h, h.x + 21, h.y + 1) == 0);
    CHECK(EditorRenderer::browser_entry(h, h.x + 21 + 84, h.y + 1 + 11 * 3) == 19);
    CHECK(EditorRenderer::browser_entry(h, h.x - 100, h.y) == -1);
    Hit none;
    CHECK(EditorRenderer::browser_entry(none, 0, 0) == -1);
}

TEST(editor_layers_and_clip)
{
    Rig g;
    auto &c = g.canvas;
    g.r->layer = 1; // static only: no control bitmaps
    g.r->render(c);
    CHECK(c.count("knob.png") == 0 && c.count("font.png") > 0);
    // the visible conditional statics ("Mid", shown while Osc/Mode is 0) are the static cache's key
    CHECK(g.r->static_key.size() == 1);
    CHECK(g.r->static_signature() == g.r->static_key);
    g.vals[{"Osc/Mode", 0}] = 2;
    CHECK(g.r->static_signature().empty()); // "Mid" hidden now: the cache would be invalid
    c.clear();
    g.r->layer = 2; // controls only
    g.r->render(c);
    CHECK(c.count("knob.png") > 0);
    for (auto &b : c.blits)
        CHECK(!(b.img == "font.png" && b.dx == 5 && b.dy == 5)); // the static "Hi" is not in layer 2
    // a repaint area: only elements inside it
    c.clear();
    g.r->layer = 0;
    g.r->clip_x0 = 0, g.r->clip_y0 = 0, g.r->clip_x1 = 30, g.r->clip_y1 = 31;
    g.r->render(c);
    CHECK(c.at("knob.png", 5, 20).size() == 1);
    CHECK(c.count("vu.png") == 0 && c.at("knob.png", 650, 0).empty());
}

TEST(editor_pressed_and_proxy)
{
    Rig g;
    auto &c = g.canvas;
    g.r->pressed = &g.layout.elements[0].children[4]; // the Osc/Shape knob
    g.r->render(c);
    CHECK(c.at("knobp.png", 5, 20).size() == 1 && c.at("knob.png", 5, 20).empty());
    // a soft knob as proxy of its target: it shows the target's value (and is active with a destination)
    g.r->pressed = nullptr;
    g.vals[{"Soft Knobs/Destination 1", 0}] = 3;
    g.vals[{"Filter 1/Cutoff", 0}] = 127;
    g.r->proxy_target = [](const std::string &p, int) {
        return p == "Softknobs/Softknob 1" ? std::string("Filter 1/Cutoff") : std::string();
    };
    c.clear();
    g.r->render(c);
    auto sk = c.at("knob.png", 50, 20);
    CHECK(sk.size() == 1 && sk[0].sy == 30 && sk[0].alpha == 1.0);
    CHECK(g.r->param_active("Softknobs/Softknob 1", 0));
    CHECK(!g.r->param_active("Osc/Gated", 0));
    g.vals[{"Osc/Mode", 0}] = 2;
    CHECK(g.r->param_active("Osc/Gated", 0));
}

TEST(editor_dependencies)
{
    Rig g;
    EditorRenderer::Deps deps;
    g.r->deps = &deps;
    g.r->render(g.canvas);
    g.r->deps = nullptr;
    CHECK(deps.cond.count("Osc/Mode"));          // read by the show/hide conditions
    CHECK(deps.cond.count("VirusControl/Page"));
    CHECK(deps.rects.count("Osc/Shape"));        // drawn by the knobs
    auto &r = deps.rects["Osc/Shape"];
    CHECK(std::find(r.begin(), r.end(), std::array<int, 4>{5, 20, 10, 10}) != r.end());
    CHECK(deps.rects.count("Filter 1/Resonance")); // read while drawing the filter displays
    // the values function is the plain one again after the render
    CHECK(g.r->values("Osc/Shape", 0) == 64);
}

TEST(editor_hit_and_rects)
{
    Rig g;
    Hit h = g.r->hit_test(7, 22);
    CHECK(h.element && h.element->get("Parameter") == "Osc/Shape" && h.x == 5 && h.y == 20 && h.part == 0);
    CHECK(!g.r->hit_test(82, 22).element);  // Enabled="false"
    CHECK(!g.r->hit_test(7, 7).element);    // static text: no parameter
    Hit vu = g.r->hit_test(10, 37);
    CHECK(vu.element && vu.part == 2);      // the control's own part
    g.r->part = 4;
    CHECK(g.r->hit_test(7, 22).part == 4);  // others follow the current part
    auto rects = g.r->param_rects("Osc/Shape");
    CHECK(rects.size() == 3);                // knob, text slider, second container's knob
    CHECK(g.r->param_rects("Nope").empty());
}

TEST(editor_visible_controls)
{
    // help mode: every visible control with a parameter, at the same place and part as the hit test finds it
    Rig g;
    auto all = g.r->visible_controls();
    CHECK(!all.empty());
    bool shape = false;
    for (auto &h : all) {
        CHECK(h.element && !h.element->get("Parameter").empty() && h.element->get("Enabled") != "false");
        Hit t = g.r->hit_test(h.x + 1, h.y + 1);
        if (t.element == h.element)
            CHECK(t.x == h.x && t.y == h.y && t.part == h.part);
        shape = shape || (h.element->get("Parameter") == "Osc/Shape" && h.x == 5 && h.y == 20);
    }
    CHECK(shape);
    for (auto &h : all) // the disabled button at 80,20 is not listed
        CHECK(!(h.x == 80 && h.y == 20));
}

TEST(editor_value_texts)
{
    Rig g;
    CHECK(g.r->value_text("Osc/Mode", 1) == " Saw");
    CHECK(g.r->value_text("Osc/Mode", 3).empty()); // an existing entry with empty text stays blank
    CHECK(g.r->value_text("Osc/Mode", 7) == "7");
    CHECK(g.r->value_text("Osc/Wave", 4) == "Four"); // mapped list: value, not position
    CHECK(g.r->value_text("Unknown/Param", 5) == "5");
    auto m = g.r->menu_entries("Osc/Wave");
    CHECK(m.size() == 3 && m[0].first == 9 && m[0].second == "Nine" && m[1].first == 4); // menu order; 40 out of range
    CHECK(m[2].first == 5 && m[2].second == "-GGrey");   // the menu keeps the "greyed" marker (the editor disables it)
    CHECK(g.r->value_text("Osc/Wave", 5) == "Grey");    // the control shows the text without it
    auto modes = g.r->menu_entries("Osc/Mode");
    CHECK(modes.size() == 3); // the empty entry is left out
    auto plain = g.r->menu_entries("Osc/Plain");
    CHECK(plain.size() == 3 && plain[0].second == "2" && plain[2].first == 4);
    CHECK(g.r->menu_entries("Unknown/Param").empty());
}

TEST(editor_digits_and_status)
{
    Rig g;
    RecCanvas &c = g.canvas;
    g.r->digits(c, 127, 100, 10);
    CHECK(!c.fills.empty());
    for (auto &f : c.fills)
        CHECK(f.x <= 100 && f.x > 100 - 15 && f.y >= 10 && f.y < 15);
    c.clear();
    g.r->status_text(c, "font.png", "OK", 0, 0, 60, 20);
    CHECK(c.blits.size() == 2);
    c.clear();
    g.r->status_text(c, "missing.png", "OK", 0, 0, 60, 20);
    CHECK(c.blits.empty());
    c.clear();
    g.r->status_text(c, "font.png", std::string("\x01", 1), 0, 0, 60, 20); // unprintable -> '?'
    CHECK(c.blits.size() == 1 && c.blits[0].sy == ('?' - 32) * 7);
}

TEST(editor_conditions)
{
    Rig g;
    auto vals = g.r->values;
    CHECK(g.cond->eval("", vals, 0));
    CHECK(g.cond->eval("Never Defined", vals, 0)); // unknown conditions count as true
    CHECK(g.cond->eval("Mode Is Zero", vals, 0));
    CHECK(!g.cond->eval("Page Two", vals, 0));
    g.vals[{"VirusControl/Page", -1}] = 2;
    CHECK(g.cond->eval("Page Two", vals, 0));
    CHECK(!g.cond->eval("Internal System Info/Display Recall Progress", vals, 0));
    CHECK(!g.cond->eval("VirusControl/Browser/Category Filter Selected", vals, 0));
    CHECK(g.cond->eval("VirusControl/Browser/Category Filter Not Selected", vals, 0));
    g.vals[{"Repository Actions/Category Filter", -1}] = 3;
    CHECK(g.cond->eval("VirusControl/Browser/Category Filter Selected", vals, 0));
    CHECK(g.cond->default_value("Osc/Shape") == 64 && g.cond->default_value("VirusControl/Page") == 0);
    CHECK(g.cond->default_value("Nope") == 0 && !g.cond->find("Nope"));
}
