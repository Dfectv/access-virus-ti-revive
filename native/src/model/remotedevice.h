// remotedevice.h - the Virus device model, read from the user's Access "remotedevice.xml" (see docs/formats.md).
//
//   parameters (group/name, range, default, value list, transport message, visibility condition),
//   value lists (texts per value, plain or mapped), conditions (tests on other parameters),
//   MIDI message templates (CC, poly pressure, parameter SysEx per bank, single/multi dumps with byte layouts),
//   soft-knob mapping and initialisation lists.
#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace vti {

struct Parameter {
    std::string group, name, path; // path = "group/name", the key used everywhere in the XML
    int min = 0, max = 127, def = 0;
    std::string valuelist, part, condition, message, shortname, displayname;
    int controller = -1; // controllerindex
    bool automatable = false, performance = false;
};

struct Valuelist {
    std::string name;
    bool mapped = false;
    struct Entry {
        int index = 0;  // value of the parameter
        int value = -1; // mapped lists: display order / alternate value
        std::string text;
    };
    std::vector<Entry> entries;
    std::string text(int v) const;
};

struct Condition {
    std::string name;
    bool any = true;     // operand "or" (true) / "and"
    bool invert = false; // result "invert"
    struct Test {
        bool inside = true; // "inside" min..max, "equals" == min
        int min = 0, max = 0;
        std::string param;
    };
    std::vector<Test> tests;
};

struct MessageByte {
    int index = 0;
    std::string content; // sysexstart, byte, deviceid, part, controllerid, value, sysexend, checksum or a parameter path
    int tag = -1;        // fixed byte value (decimal in the XML)
    int mask = -1, shift = 0, part = -1;
    std::string method;  // checksum method
    int start = -1, stop = -1;
    // controllerid byte: (map index, parameter path); several parameters can share an index (e.g. CC 40 = Filter 1 Cutoff
    // or Vocoder Carrier Center Frequency, depending on the mode)
    std::vector<std::pair<int, std::string>> controllers;
};

struct MidiMessage {
    std::string id, name;
    std::vector<MessageByte> bytes;
};

// automation.xml: the original plug-in's VST3 parameter ids. id = ParamID, title as the original shows it
// ("[<part>] <shortname>"), unit "Part NN/".
struct AutomationEntry {
    int id = 0, part = 1;
    std::string path;
};
bool load_automation(const std::vector<uint8_t> &xml, std::vector<AutomationEntry> &out, std::string &error);
// Short title as the original shows it: the parameter's shortname; "Softknobs/Softknob N" (virtual) -> "Softknob N".
std::string automation_title(const class RemoteDevice &dev, const AutomationEntry &e);

class RemoteDevice {
public:
    bool load(const std::vector<uint8_t> &xml, std::string &error);

    std::string device, version;
    std::vector<Parameter> parameters;
    std::map<std::string, Valuelist> valuelists;
    std::multimap<std::string, Condition> conditions; // several entries per name = alternatives
    std::vector<MidiMessage> messages;
    std::vector<std::string> softknobs;                  // index -> parameter path
    std::vector<std::pair<std::string, std::string>> global_init;
    struct ParamInit {
        std::string param, value;
        int part = 0;
    };
    std::vector<ParamInit> param_init;

    const Parameter *find(const std::string &path) const;
    const MidiMessage *message(const std::string &id) const;
    // The message that changes `path` to `value` for `part` (0-based; 16 = global) and device id - CC, poly pressure
    // or a parameter SysEx, built from the parameter's transport template. Empty if the parameter has none.
    std::vector<uint8_t> encode(const std::string &path, int value, int part, int device_id = 0x10) const;
    // Value of `path` for a valuelist text ("MIDI REMOTE", "Off", "01"); -1 if neither an entry nor a number.
    int value_of(const std::string &path, const std::string &text) const;
    // The messages the original sends at start-up before the project state: GlobalInit, then ParamInit.
    std::vector<std::vector<uint8_t>> init_messages(int device_id = 0x10) const;

    // Dumps (VirusTI_Singledump, VirusTI_MultiDump, ...): the template's byte index is the offset in the SysEx message.
    // Parameter values of a received dump; key = path, or "path@part" for bytes with a part attribute. Several
    // parameters can share a byte (mode-dependent meaning) - all of them get the value. Fails on a wrong length or
    // checksum.
    bool decode_dump(const MidiMessage &m, const std::vector<uint8_t> &msg, std::map<std::string, int> &values,
                     std::string &error) const;
    // Writes `values` (same keys) into an existing dump and recomputes the checksums. Bytes the template does not
    // describe keep their content, so start from a dump the Virus sent.
    void update_dump(const MidiMessage &m, std::vector<uint8_t> &msg, const std::map<std::string, int> &values) const;
    static size_t dump_size(const MidiMessage &m);
    // Brings a 524-byte single dump of an older format version (byte 9) up to the plug-in's current version, as the
    // original does before sending a stored/init patch (decompile 0x1800a0c00, docs/protocol.md). Versions below 8
    // need the original's default page B (`page_b`, 256 bytes from the user's plug-in binary); without it they are
    // left unchanged. Recomputes both checksums. Returns true if the dump was changed.
    static bool upgrade_single(std::vector<uint8_t> &dump, const std::vector<uint8_t> &page_b = {});
    // The 14-bit value the Virus reports per program in a bank info reply (39 -> 14): sum of a 524-byte single dump's
    // bytes 13..521 without 265, mod 16384. Equal value = the cached patch is current. -1 for a wrong size.
    static int single_fingerprint(const std::vector<uint8_t> &msg);

private:
    std::map<std::string, size_t> index_;
};

} // namespace vti
