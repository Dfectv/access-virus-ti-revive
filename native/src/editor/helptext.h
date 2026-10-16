// helptext.h - short explanations of the editor's controls for the help mode ("?" button). Our own words (community
// notes from the documented behaviour of the Virus and TI Revive's own features), not text from Access' manuals.
#pragma once
#include <string>

namespace vti {

struct HelpText {
    std::string title; // e.g. "Cutoff (Filter 1)"
    std::string text;  // one to three sentences
};

// The explanation for a parameter path ("Filter 1/Cutoff"); numbered copies share one text (Assign Slot 1..6,
// LFO 1..3, arpeggiator steps ...). False if there is none (internal displays, debug fields).
bool help_for(const std::string &param, HelpText &out);

// a layout condition name as a reason a reader understands: "VirusControl/Display/Chorus/Type Is Off" ->
// "Chorus Type is Off" (the help note of a disabled control says why); "" for none
std::string inactive_reason(const std::string &condition);

// how many parameter keys have a text (tests)
int help_count();

} // namespace vti
