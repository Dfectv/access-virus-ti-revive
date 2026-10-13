// library.cpp - see library.h.
#include "library.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace vti {

namespace fs = std::filesystem;

std::vector<PatchLibrary> load_libraries(const std::string &dir)
{
    std::vector<PatchLibrary> out;
    std::error_code ec;
    for (auto &e : fs::directory_iterator(dir, ec)) {
        std::string ext = e.path().extension().string();
        for (auto &ch : ext)
            ch = (char)tolower(ch);
        if (ext != ".mid" && ext != ".syx")
            continue;
        std::ifstream f(e.path(), std::ios::binary);
        std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        PatchLibrary lib;
        lib.name = e.path().stem().string();
        lib.path = e.path().string();
        // SysEx single dumps anywhere in the file (SMF sysex events or raw .syx): 00 20 33 01 <dev> 10 ... F7
        for (size_t i = 0; i + 8 < d.size(); i++) {
            if (!(d[i] == 0x00 && d[i + 1] == 0x20 && d[i + 2] == 0x33 && d[i + 3] == 0x01 && d[i + 5] == 0x10))
                continue;
            auto end = std::find(d.begin() + i, d.end(), 0xF7);
            if (end == d.end())
                break;
            std::vector<uint8_t> m = {0xF0};
            m.insert(m.end(), d.begin() + i, end + 1);
            if (m.size() == 524)
                lib.dumps.push_back(m);
            i = end - d.begin();
        }
        out.push_back(std::move(lib));
    }
    std::sort(out.begin(), out.end(), [](const PatchLibrary &a, const PatchLibrary &b) { return a.name < b.name; });
    return out;
}

std::string library_dir()
{
    std::vector<std::string> c;
#ifdef _WIN32
    if (const char *up = getenv("USERPROFILE"))
        c.push_back(std::string(up) + "/Documents/Access Music/Virus TI/Patches");
#else
    if (const char *home = getenv("HOME")) {
        c.push_back(std::string(home) + "/Documents/Access Music/Virus TI/Patches");
        if (const char *user = getenv("USER")) // an existing Access installation under Wine (Linux)
            c.push_back(std::string(home) + "/.wine/drive_c/users/" + user + "/Documents/Access Music/Virus TI/Patches");
    }
#endif
    std::error_code ec;
    for (auto &d : c)
        if (fs::is_directory(d, ec))
            return d;
    return c.empty() ? std::string() : c.front();
}

} // namespace vti
