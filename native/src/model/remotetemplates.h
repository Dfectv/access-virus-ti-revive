// remotetemplates.h - the REMOTE page's 32 remote templates (E193, E196). The original keeps them in the plug-in, each
// as SysEx records with command 0x13: a name record (28 bytes: template, 0x20, 16-char name, flags with bit 0 = MIDI
// Channel Behaviour) and 32 knob records (65 bytes: template, knob, 10-char name, Knob Behaviour in two nibbles, a
// 40-byte MIDI string, encode_midi). It reads none from the Virus; it loads and saves them in a cache file (a "MIDI" container).
// The plug-in sends them like the original (E199, E200): a chosen or loaded template whole, an edit its record.
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
    // what the MIDI string means, like the original's "MIDI String Info" (0x18008bee0): "Control Change Ch 1, 5,
    // Value " - per byte a data value in decimal, a channel message's type and channel, or SysEx / End / Value /
    // System Realtime
    std::string midi_info(int t, int k) const;

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
    // what the original sends when template t is chosen or loaded: the name record, then knobs 0..31 (E199, E200)
    std::vector<std::vector<uint8_t>> template_records(int t) const;

    // a record from a cache or template file: a knob record, a name record, or the older 27-byte name record without
    // the flags byte; false if it is none
    bool take_record(const std::vector<uint8_t> &rec);

    // the cache file: every template's name record followed by its knob records, in a "MIDI" container
    std::vector<uint8_t> cache() const;
    int load_cache(const std::vector<uint8_t> &bytes); // the number of records taken (0: not a container)
    // a template file (*.vrt, E198) of template t: "RMOT" header, an "RSET" chunk with its 33 records in a "MIDI"
    // container
    std::vector<uint8_t> template_file(int t) const;
    // a template file's records into template t (whatever template they were saved from, like the original); the
    // number of records taken, 0 if it is no template file (or another version)
    int load_template_file(const std::vector<uint8_t> &bytes, int t);
    bool load_file(const std::string &path);          // false: no file or no records in it
    bool save_file(const std::string &path) const;    // whole: a temporary file renamed over the old one

private:
    std::array<std::vector<uint8_t>, kTemplates> names_;
    std::array<std::array<std::vector<uint8_t>, kKnobs>, kTemplates> knobs_;
};

// the record the original sends after an edit of `param` (E199): the name record for Template Name and MIDI Channel
// Behaviour, the knob's record for Knob Name and MIDI String (a knob template sets the MIDI string); empty otherwise
std::vector<uint8_t> remote_edit_record(const std::string &param, const RemoteTemplates &t, int selected, int knob);

// the template files below `dir` like the original's list: *.vrt (any case), in sub folders as "folder/name.vrt",
// sorted; empty if there is no such folder
std::vector<std::string> remote_template_files(const std::string &dir);
// a list entry's menu text: the path without ".vrt"
std::string remote_template_name(const std::string &file);

// the text of one of the page's code-defined parameters ("" for any other): the selected template's name and MIDI
// channel behaviour ("Disable MIDI channel" / "Enable MIDI Channel"), the selected knob's name, MIDI string and info;
// `selected` -1 =
// template 0, as the original shows it
std::string remote_text(const std::string &param, const RemoteTemplates &t, int selected, int knob);
// a text field of the page edited: Template Name, Knob Name or MIDI String; false for any other parameter
bool remote_set_text(const std::string &param, RemoteTemplates &t, int selected, int knob, const std::string &text);

} // namespace vti
