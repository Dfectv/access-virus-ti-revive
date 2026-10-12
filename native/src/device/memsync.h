// memsync.h - the Virus' RAM banks in a local cache, kept current like the original plug-in does it (docs/protocol.md):
// per bank a bank-info request (39 -> 14: one 14-bit fingerprint per program), then single dump requests (30 -> 10)
// for the programs whose cached dump has a different fingerprint. The Virus answers one request at a time (~1 s each;
// overlapping requests make it truncate replies), so requests are strictly sequential.
//
// Cache files use the original's format, so an existing Access cache can be imported: "MIDI" 00 01 08 00, then
// 128 x (u32 big-endian length 524, single dump).
#pragma once
#include "broker.h"
#include <array>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace vti {

using Dump = std::vector<uint8_t>;
using Bank = std::array<Dump, 128>; // empty dump = not cached

bool load_bank_file(const std::string &path, Bank &bank);
bool save_bank_file(const std::string &path, const Bank &bank);

class MemorySync {
public:
    MemorySync(Broker &broker, int device_id = 0x10);
    ~MemorySync();

    // One request/reply round trip: sends `request` (a SysEx), waits for a SysEx reply whose bytes 6..8 equal
    // `match` (prefix). Retries `tries` times with `timeout_ms` each. Empty result on failure.
    Dump transact(const Dump &request, const std::vector<uint8_t> &match, int timeout_ms = 3000, int tries = 3);

    // Bank info of RAM bank 1..4: 128 fingerprints, or empty.
    std::vector<int> bank_info(int bank);
    Dump single(int bank, int program);

    // Brings `cache` up to date for RAM bank 1..4; progress(program, fetched) is called per fetched program.
    // Returns the number of fetched programs, -1 on failure.
    int sync_bank(int bank, Bank &cache, const std::function<void(int, bool)> &progress = nullptr);

private:
    Broker &broker_;
    int dev_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::vector<Dump> inbox_;
};

} // namespace vti
