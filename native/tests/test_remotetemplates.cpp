// REMOTE page, read-only (E193): the original keeps 32 remote templates in the plug-in (default " Init Template   ",
// constructor 0x1800899b0) and reads none from the Virus; its code defines the page's parameters ("Remote Templates/
// ...", value list "Val MIDI Channel Behaviour": Disable MIDI channel / Enable MIDI Channel).
#include "check.h"
#include "model/remotetemplates.h"
using namespace vti;

TEST(remote_templates_defaults)
{
    RemoteTemplates t;
    CHECK(t.name(0) == "Init Template" && t.name(31) == "Init Template");
    CHECK(t.name(-1).empty() && t.name(32).empty());
    CHECK(remote_text("Remote Templates/Template Name", t, 5, 0) == "Init Template");
    CHECK(remote_text("Remote Templates/Template Name", t, -1, 0) == "Init Template"); // nothing selected: template 0
    CHECK(remote_text("Remote Templates/MIDI Channel Behaviour", t, 0, 0) == "Disable MIDI channel");
    CHECK(remote_text("Remote Templates/MIDI Channel Behaviour", t, 0, 1) == "Enable MIDI Channel");
    CHECK(remote_text("Osc/Shape", t, 0, 0).empty());
}

// a template's name from its SysEx name record (command 0x13, byte 8 = 0x20): trimmed like the original shows it
TEST(remote_templates_name_record)
{
    RemoteTemplates t;
    const uint8_t rec[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x13, 0x03, 0x20, ' ', 'M', 'y', ' ', 'S', 'y', 'n', 't',
                           'h', ' ', ' ', ' ', ' ', ' ', ' ', ' ', 0x00, 0x00, 0xF7};
    CHECK(t.take_record(std::vector<uint8_t>(rec, rec + sizeof rec)));
    CHECK(t.name(3) == "My Synth");
    std::vector<uint8_t> other(rec, rec + sizeof rec);
    other[6] = 0x10; // a single dump: not ours
    CHECK(!t.take_record(other));
    other = std::vector<uint8_t>(rec, rec + sizeof rec);
    other[7] = 40; // template out of range
    CHECK(!t.take_record(other));
}
