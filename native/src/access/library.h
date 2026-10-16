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

// one .mid or .syx file (`lib.name` is left to the caller); false if it cannot be read
bool load_library_file(const std::string &path, PatchLibrary &lib, const std::vector<uint8_t> &page_b = {});
// `page_b`: AccessFiles::default_page_b() - with it, old Virus C (267-byte) dumps are upgraded to the TI format
std::vector<PatchLibrary> load_libraries(const std::string &dir, const std::vector<uint8_t> &page_b = {});
// writes `lib.dumps` to `lib.path` in the original's file layout (SMF type 0, one track, a SysEx per patch with
// delta 120, bank byte 1, program = position, division 96)
bool save_library(const PatchLibrary &lib, std::string &error);
// the folder for this platform. Linux: an existing ~/Documents/... (earlier installs), else the XDG documents folder
// (localized, e.g. ~/Dokumente), else the Wine prefix's Documents when that one exists, else ~/Documents/...
std::string library_dir();
// XDG_DOCUMENTS_DIR from the text of user-dirs.dirs ($HOME expanded); empty if missing, relative or the home itself
std::string xdg_documents_dir(const std::string &home, const std::string &user_dirs);

} // namespace vti
