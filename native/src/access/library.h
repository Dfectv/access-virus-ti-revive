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

// `page_b`: AccessFiles::default_page_b() - with it, old Virus C (267-byte) dumps are upgraded to the TI format
std::vector<PatchLibrary> load_libraries(const std::string &dir, const std::vector<uint8_t> &page_b = {});
// the folder for this platform (Linux: ~/Documents/..., else the Wine prefix's Documents when that one exists)
std::string library_dir();

} // namespace vti
