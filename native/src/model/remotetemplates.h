// remotetemplates.h - the REMOTE page's 32 remote templates (E193, E196). The original keeps them in the plug-in, each
// as SysEx records with command 0x13: a name record (28 bytes: template, 0x20, 16-char name, flags with bit 0 = MIDI
// Channel Behaviour) and 32 knob records (65 bytes: template, knob, 10-char name, Knob Behaviour in two nibbles, a
// 40-byte MIDI string, encode_midi). It reads none from the Virus; it loads and saves them in a cache file (a "MIDI" container).
// Sending templates to the Virus (the original does that on every choice and edit) is not implemented: rule 2.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace vti {

class RemoteTemplates {
public:
    static constexpr int kTemplates = 32, kKnobs = 32;

    RemoteTemplates(); // the original's defaults: "Init Template", blank knobs

    std::string name(int t) const; // trimmed; "" outside 0..31
    bool channel_enabled(int t) const;
    std::string knob_name(int t, int k) const; // trimmed
    int knob_behaviour(int t, int k) const;    // 0..127

    std::string midi_string(int t, int k) const; // the knob's MIDI string as text ("B0 05 *")

    // the 40 bytes of a MIDI string typed as text, like the original's parser (0x1800893f0): each MIDI byte as two
    // bytes (low nibble, high nibble), "*" / "X" / "V" = the knob's value (0F 0F), "%n" = a decimal byte, a lone hex
    // digit = that value, spaces and punctuation skipped, the end marked by 0E 0F when there is room
    static std::vector<uint8_t> encode_midi(const std::string &text);
    // the text of such 40 bytes: "B0 05 *" (two hex digits per byte, "*" for the value, up to the end mark)
    static std::string midi_text(const uint8_t *bytes);

    // edits like the original's (0x18008b020): names padded with spaces and cut to 16 / 10 characters; ignored
    // outside the ranges
    void set_name(int t, const std::string &name);
    void set_channel_enabled(int t, bool on);
    void set_knob_name(int t, int k, const std::string &name);
    void set_knob_behaviour(int t, int k, int value);
    void set_midi_string(int t, int k, const std::string &text);

    // the complete records (empty outside the ranges)
    const std::vector<uint8_t> &name_record(int t) const;
    const std::vector<uint8_t> &knob_record(int t, int k) const;

    // a record from a cache or template file: a knob record, a name record, or the older 27-byte name record without
    // the flags byte; false if it is none
    bool take_record(const std::vector<uint8_t> &rec);

    // the cache file: every template's name record followed by its knob records, in a "MIDI" container
    std::vector<uint8_t> cache() const;
    int load_cache(const std::vector<uint8_t> &bytes); // the number of records taken (0: not a container)
    bool load_file(const std::string &path);          // false: no file or no records in it
    bool save_file(const std::string &path) const;    // whole: a temporary file renamed over the old one

private:
    std::array<std::vector<uint8_t>, kTemplates> names_;
    std::array<std::array<std::vector<uint8_t>, kKnobs>, kTemplates> knobs_;
};

// the text of one of the page's code-defined parameters ("" for any other): the selected template's name and MIDI
// channel behaviour ("Disable MIDI channel" / "Enable MIDI Channel"), the selected knob's name and MIDI string;
// `selected` -1 =
// template 0, as the original shows it
std::string remote_text(const std::string &param, const RemoteTemplates &t, int selected, int knob);
// a text field of the page edited: Template Name, Knob Name or MIDI String; false for any other parameter
bool remote_set_text(const std::string &param, RemoteTemplates &t, int selected, int knob, const std::string &text);

} // namespace vti
