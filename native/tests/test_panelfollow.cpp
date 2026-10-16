// PanelFollow: what the plug-in does with changes made on the Virus' own panel (E152) - the 150 ms preset debounce
// per part, the soft knob value reports, the controllers restored after a patch load, the edit-buffer request
#include "check.h"
#include "access/accessfiles.h"
#include "model/panelfollow.h"
#include "model/remotedevice.h"
#include <memory>
using namespace vti;
using Clock = PanelFollow::Clock;
using std::chrono::milliseconds;

TEST(panel_preset_paths)
{
    CHECK(PanelFollow::is_preset_path("Multi Part Parameter/Bank"));
    CHECK(PanelFollow::is_preset_path("Multi Part Parameter/Patch"));
    CHECK(!PanelFollow::is_preset_path("Multi Part Parameter/Part Volume"));
    CHECK(!PanelFollow::is_preset_path("Multi Part Parameter/Bank "));
    PanelFollow f;
    const auto t = Clock::now();
    CHECK(!f.preset_report("Filter 1/Resonance", 0, t)); // not a preset report: nothing marked
    CHECK(!f.preset_report("Multi Part Parameter/Patch", 16, t)); // out of range
    CHECK(!f.preset_report("Multi Part Parameter/Patch", -1, t));
    CHECK(f.presets_due(t + milliseconds(1000)) == 0);
}

TEST(panel_preset_debounce)
{
    PanelFollow f;
    const auto t = Clock::now();
    CHECK(f.preset_report("Multi Part Parameter/Bank", 0, t));
    CHECK(f.presets_due(t) == 0);
    CHECK(f.presets_due(t + milliseconds(149)) == 0);
    // the Patch report follows the Bank report: the timer starts again from the last one
    CHECK(f.preset_report("Multi Part Parameter/Patch", 0, t + milliseconds(100)));
    CHECK(f.presets_due(t + milliseconds(200)) == 0);
    CHECK(f.presets_due(t + milliseconds(250)) == 1u);
    CHECK(f.presets_due(t + milliseconds(500)) == 0); // forgotten once handed out
}

TEST(panel_preset_turning_through)
{
    // turning the Virus' value knob through ten presets in 50 ms steps loads only the last one, once
    PanelFollow f;
    const auto t = Clock::now();
    int loads = 0;
    for (int i = 0; i < 10; i++) {
        f.preset_report("Multi Part Parameter/Patch", 0, t + milliseconds(50 * i));
        if (f.presets_due(t + milliseconds(50 * i + 1)))
            loads++;
    }
    CHECK(loads == 0);
    CHECK(f.presets_due(t + milliseconds(450 + 150)) == 1u);
}

TEST(panel_preset_several_parts)
{
    PanelFollow f;
    const auto t = Clock::now();
    f.preset_report("Multi Part Parameter/Patch", 0, t);
    f.preset_report("Multi Part Parameter/Patch", 3, t + milliseconds(10));
    f.preset_report("Multi Part Parameter/Bank", 15, t + milliseconds(20));
    CHECK(f.pending(3) && !f.pending(4) && !f.pending(-1) && !f.pending(16));
    CHECK(f.presets_due(t + milliseconds(200)) == ((1u << 0) | (1u << 3) | (1u << 15)));
    CHECK(!f.pending(3)); // handed out
}

TEST(panel_softknob_reports)
{
    CHECK(PanelFollow::softknob_report("Global Soft Knob Configuration/Value Softknob0") == 1);
    CHECK(PanelFollow::softknob_report("Global Soft Knob Configuration/Value Softknob1") == 2);
    CHECK(PanelFollow::softknob_report("Global Soft Knob Configuration/Value Softknob2") == 3);
    CHECK(PanelFollow::softknob_report("Global Soft Knob Configuration/Value Softknob3") == 0);
    CHECK(PanelFollow::softknob_report("Global Soft Knob Configuration/Value Softknob") == 0);
    CHECK(PanelFollow::softknob_report("Global Soft Knob Configuration/Value Softknob10") == 0);
    CHECK(PanelFollow::softknob_report("Global Soft Knob Configuration/Destination 1") == 0);
    CHECK(PanelFollow::softknob_report("Softknobs/Softknob 1") == 0);
}

TEST(panel_edit_buffer_request)
{
    const std::vector<uint8_t> r = PanelFollow::edit_buffer_request(5);
    const std::vector<uint8_t> want = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x05, 0xF7};
    CHECK(r == want);
    CHECK(PanelFollow::edit_buffer_reply(5) == (std::vector<uint8_t>{0x10, 0x00, 0x05}));
    CHECK(PanelFollow::edit_buffer_request(0)[8] == 0);
}

TEST(panel_restored_controllers_in_model)
{
    CHECK(PanelFollow::restored_controllers().size() == 13);
    std::string err;
    auto files = AccessFiles::open("", err);
    if (!files)
        SKIP("no Access installation");
    RemoteDevice dev;
    CHECK(dev.load(files->get("remotedevice.xml"), err));
    for (auto &p : PanelFollow::restored_controllers()) {
        if (!dev.find(p))
            std::fprintf(stderr, "  missing: %s\n", p.c_str());
        CHECK(dev.find(p) != nullptr);
        CHECK(!dev.encode(p, 64, 0).empty()); // a controller the plug-in can send
    }
    // the reports the plug-in reacts to exist in the description with these names
    CHECK(dev.find("Multi Part Parameter/Bank") && dev.find("Multi Part Parameter/Patch"));
    CHECK(dev.find("Global Soft Knob Configuration/Value Softknob0") &&
          dev.find("Global Soft Knob Configuration/Value Softknob2"));
    CHECK(dev.find("Multi Parameters/Panel Part"));
}
