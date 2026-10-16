// Help mode texts (editor/helptext): numbered copies share one text, titles name the section, internal fields have none
#include "check.h"
#include "editor/helptext.h"

using namespace vti;

TEST(help_shared_texts)
{
    HelpText a, b;
    CHECK(help_for("Assign Slot 1/Amount 1", a) && help_for("Assign Slot 6/Amount 3", b) && a.text == b.text);
    CHECK(help_for("LFO 3/Rate", a) && a.title == "Rate (LFO 3)");
    CHECK(help_for("Arpeggiator User Pattern/Step 32 Velocity", a) && !a.text.empty());
    CHECK(help_for("Internal MIDI Standard Parameters/07 Volume", a) && a.title == "Part volume and activity");
    CHECK(help_for("Multi Part Parameter/Output Select Control3", a) && a.text.find("USB 3") != std::string::npos);
    CHECK(help_for("Locks/Matrix Slot 4", a) && a.text.find("Not supported") != std::string::npos);
}

TEST(help_own_text_wins)
{
    HelpText osc1, generic;
    CHECK(help_for("LFO 1/Dest Osc1", osc1) && help_for("LFO 1/Dest Osc2", generic) && osc1.text != generic.text);
    CHECK(help_for("Filter 2/Offset for FilterLink", osc1) && osc1.text.find("offset") == std::string::npos);
    CHECK(help_for("Filter 1/Cutoff", osc1) && osc1.title == "Cutoff (Filter 1)");
    CHECK(help_for("Suboscillator/Volume", osc1) && osc1.title == "Sub oscillator"); // no "(Suboscillator)"
    CHECK(help_for("Delay/Send", osc1) && osc1.title == "Delay send");
}

TEST(help_none_for_internal_fields)
{
    HelpText t;
    CHECK(!help_for("Debug/Sync Events", t));
    CHECK(!help_for("Internal System Info/TIOS Version String", t));
    CHECK(!help_for("No Such/Parameter", t));
    CHECK(help_count() > 250);
}
