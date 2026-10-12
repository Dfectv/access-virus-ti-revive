// accessfiles.h - the user's own Access "Virus TI" files (skin bitmaps, editor/device XML), read at runtime.
//
// Nothing of Access is shipped with this project: the skin and the device description come from the user's Access
// installation. Windows/Linux (Wine prefix): resources of type "XML" and "PNG" inside "Virus Control.dll";
// macOS: plain files in "Virus Control.bundle/Contents/Resources".
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace vti {

class AccessFiles {
public:
    // Locate and open the Access installation. `hint` may name Virus Control.dll / .bundle or its folder; empty =
    // search the standard locations (and $VTI_ACCESS_DIR). Returns nullptr and sets `error` if nothing is found.
    static std::unique_ptr<AccessFiles> open(const std::string &hint, std::string &error);

    // File by its Access name, case-insensitive ("editor.xml", "BG_Page_Browser.png"); empty if missing.
    const std::vector<uint8_t> &get(const std::string &name) const;
    std::vector<std::string> names() const; // all names (lower case)
    const std::string &source() const { return source_; } // the DLL or bundle that was read

private:
    std::map<std::string, std::vector<uint8_t>> files_; // key: lower-case name
    std::string source_;
    bool load_pe(const std::string &dll, std::string &error);
    bool load_dir(const std::string &dir, std::string &error);
};

} // namespace vti
