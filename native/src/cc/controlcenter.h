// controlcenter.h - the core of TI Revive's Control Center (the standalone replacement of Access' "Virus Control
// Center"), shared by the CLI (tools/tirevive-cc) and a later UI.
//
// Phase 1 is read-only: it only uses requests whose behaviour is documented in findings/evidence.md - standard USB
// GET_DESCRIPTOR (E115), bank info 39 -> 14 and single dump request 30 -> 10 (E027, E031, E037). It never writes to
// the Virus. Firmware/OS updates, "Burn To Flash", factory reset and device renaming are out of scope for good: they
// write the Virus' flash and can brick it (AGENTS.md rule 2).
#pragma once
#include "../device/broker.h"
#include "../device/memsync.h"
#include <functional>
#include <string>
#include <vector>

namespace vti::cc {

struct DeviceInfo {
    uint16_t vendor = 0, product = 0, bcd = 0;
    std::string manufacturer, product_name;
};

// The USB identity of the connected Virus through the broker (no serial number: it would end up in bug reports).
bool read_device_info(Broker &broker, DeviceInfo &info, std::string &error);

// The patch name of a 524-byte single dump (10 characters, trailing blanks removed).
std::string patch_name(const Dump &d);

// The plug-in's bank cache (the same folder, so a backup only fetches what changed since the plug-in's last sync).
std::string cache_dir();

// Default backup folder: <Documents>/Access Music/Virus TI/Backups/<YYYY-MM-DD HHMM>.
std::string default_backup_dir();

// Reads RAM banks 1..4 (`banks`, e.g. "1234") - cached programs whose fingerprint still matches are not requested
// again - and writes "RAM A.mid".. to `out_dir` in the original's library format (readable by the TI Revive plug-in
// and Access' Virus Control). progress(bank, program, fetched) per program. `stop` (optional) cancels between two
// requests (what was fetched stays in the cache, no library is written for the bank). The broker serves one program
// at a time: while the plug-in has the Virus, open() reports no device.
bool backup_ram(Broker &broker, const std::string &banks, const std::string &out_dir,
                const std::function<void(int, int, bool, const Dump &)> &progress, std::string &error,
                const std::atomic<bool> *stop = nullptr);

// Writes the dumps as one .syx file (the single dumps back to back, as patch librarians expect).
bool write_syx(const std::vector<Dump> &dumps, const std::string &path, std::string &error);

} // namespace vti::cc
