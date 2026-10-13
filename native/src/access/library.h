// library.h - the original's "Personal Libraries": Standard MIDI files with Virus single dumps in
// Documents/Access Music/Virus TI/Patches (Virus Control: "file://{mydocuments}/Access Music/Virus TI/Patches").
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace vti {

struct PatchLibrary {
    std::string name, path;                   // file name without ".mid"
    std::vector<std::vector<uint8_t>> dumps; // 524-byte single dumps (TI format)
};

std::vector<PatchLibrary> load_libraries(const std::string &dir);
// the folder for this platform (Linux: ~/Documents/..., else the Wine prefix's Documents when that one exists)
std::string library_dir();

} // namespace vti
