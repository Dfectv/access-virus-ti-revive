// remotetemplates.h - the REMOTE page's 32 remote templates, read-only (E193). The original keeps them in the
// plug-in, each as 33 SysEx records with command 0x13 (a name record, byte 8 = 0x20, and 32 knob records), defaults
// " Init Template   "; it reads none from the Virus. Its code defines the page's "Remote Templates/..." parameters.
// Sending templates to the Virus (the original does that while browsing) and SAVE are not implemented: rule 2.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace vti {

class RemoteTemplates {
public:
    RemoteTemplates();
    std::string name(int t) const; // trimmed; "" outside 0..31
    // a 0x13 name record (from a template file); false if it is none
    bool take_record(const std::vector<uint8_t> &rec);

private:
    std::array<std::string, 32> names_;
};

// the text of one of the page's code-defined parameters ("" for any other): the selected template's name, the
// MIDI channel behaviour (0 = "Disable MIDI channel", 1 = "Enable MIDI Channel")
std::string remote_text(const std::string &param, const RemoteTemplates &t, int selected, int behaviour);

} // namespace vti
