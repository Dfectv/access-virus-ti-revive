// state.h - the plug-in's project state (the original's "MIDI" container of SysEx/MIDI records), its editor-only
// records and the meta parameter value mapping of editordevice.xml. Pure functions, unit tested (tests/).
#pragma once
#include "model/remotedevice.h"
#include <cstdint>
#include <string>
#include <vector>

namespace vti {

// "MIDI" + u32 BE length, then records of u32 BE length + message; a truncated record ends the list
std::vector<std::vector<uint8_t>> parse_container(const std::vector<uint8_t> &d);
std::vector<uint8_t> build_container(const std::vector<std::vector<uint8_t>> &recs);
// a 524-byte single dump of `part` (F0 .. 10 00 <part>: edit buffer of that part)
bool is_part_dump(const std::vector<uint8_t> &m, int part);
// E174: part dumps (edit buffers, bank byte 0) older than the current format are upgraded like the Virus does on
// receipt, so the editor shows and saves what the Virus plays; returns how many were upgraded
int upgrade_state_dumps(std::vector<std::vector<uint8_t>> &recs);
// editor-only per-part settings (link buttons): F0 7D 'V' 'T' 'N' 01 <part> <path> 00 <value> F7
std::vector<uint8_t> editor_record(const std::string &path, int part, int value);
bool parse_editor_record(const std::vector<uint8_t> &m, std::string &path, int &part, int &value);
// meta value -> client value (maps first, else a * meta + b) and back (false: no map entry / not invertible)
int meta_to_client(const MetaClient &c, int mv);
bool client_to_meta(const MetaClient &c, int cv, int &mv);

} // namespace vti
