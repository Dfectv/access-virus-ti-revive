#include "model/remotetemplates.h"
#include <algorithm>

namespace vti {

static std::string trimmed(const std::string &s)
{
    size_t a = s.find_first_not_of(' '), b = s.find_last_not_of(' ');
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

RemoteTemplates::RemoteTemplates()
{
    names_.fill(" Init Template   "); // the original's default name record
}

std::string RemoteTemplates::name(int t) const
{
    return t >= 0 && t < 32 ? trimmed(names_[t]) : std::string();
}

// F0 00 20 33 01 <dev> 13 <template> 20 <16-char name> ... F7
bool RemoteTemplates::take_record(const std::vector<uint8_t> &r)
{
    static const uint8_t head[] = {0xF0, 0x00, 0x20, 0x33, 0x01};
    if (r.size() < 9 + 16 + 1 || !std::equal(head, head + 5, r.begin()) || r[6] != 0x13 || r[8] != 0x20 || r[7] >= 32 ||
        r.back() != 0xF7)
        return false;
    std::string n;
    for (int i = 0; i < 16; i++)
        n += r[9 + i] >= 32 && r[9 + i] < 127 ? (char)r[9 + i] : ' ';
    names_[r[7]] = n;
    return true;
}

std::string remote_text(const std::string &param, const RemoteTemplates &t, int selected, int behaviour)
{
    if (param == "Remote Templates/Template Name")
        return t.name(selected < 0 ? 0 : selected); // nothing selected yet: the original shows template 0
    if (param == "Remote Templates/MIDI Channel Behaviour")
        return behaviour ? "Enable MIDI Channel" : "Disable MIDI channel";
    return {};
}

} // namespace vti
