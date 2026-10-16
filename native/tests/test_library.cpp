// Patch libraries and the Control Center's export (src/access/library, src/cc/controlcenter) on generated single
// dumps: .mid and .syx round trips keep the patch data; save_library numbers the patches and recomputes the checksums.
#include "access/library.h"
#include "cc/controlcenter.h"
#include "check.h"
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace vti;
namespace fs = std::filesystem;

namespace {
Dump single(int n)
{
    Dump m(524);
    for (size_t i = 0; i < m.size(); i++)
        m[i] = (uint8_t)((i * 7 + n * 13) & 0x7F);
    const uint8_t head[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x10, 0x02, 0x55};
    memcpy(m.data(), head, sizeof head);
    char name[11];
    snprintf(name, sizeof name, "Test %-5d", n);
    memcpy(m.data() + 249, name, 10);
    m[523] = 0xF7;
    return m;
}

// everything except the bank/program bytes and the two checksums
bool same_patch(const Dump &a, const Dump &b)
{
    if (a.size() != 524 || b.size() != 524)
        return false;
    for (size_t i = 0; i < 524; i++)
        if (i != 7 && i != 8 && i != 265 && i != 522 && a[i] != b[i])
            return false;
    return true;
}
} // namespace

TEST(library_round_trip)
{
    fs::path dir = fs::temp_directory_path() / "vtitests-library";
    fs::remove_all(dir);
    fs::create_directories(dir);
    PatchLibrary lib;
    for (int i = 0; i < 5; i++)
        lib.dumps.push_back(single(i));
    lib.path = (dir / "a.mid").string();
    std::string err;
    CHECK(save_library(lib, err));
    CHECK(cc::write_syx(lib.dumps, (dir / "a.syx").string(), err));

    PatchLibrary mid, syx;
    CHECK(load_library_file((dir / "a.mid").string(), mid));
    CHECK(load_library_file((dir / "a.syx").string(), syx));
    CHECK(mid.dumps.size() == 5 && syx.dumps.size() == 5);
    for (size_t i = 0; i < mid.dumps.size() && i < syx.dumps.size(); i++) {
        CHECK(same_patch(mid.dumps[i], lib.dumps[i]));
        CHECK(syx.dumps[i] == lib.dumps[i]); // .syx keeps the dumps as they are
        CHECK(mid.dumps[i][7] == 1 && mid.dumps[i][8] == i); // library: bank 1, program = position
    }
    CHECK(cc::patch_name(mid.dumps[3]) == "Test 3");
    CHECK(load_libraries(dir.string()).size() == 2);
    fs::remove_all(dir);
}

TEST(library_edge_cases)
{
    fs::path dir = fs::temp_directory_path() / "vtitests-library-edge";
    fs::remove_all(dir);
    fs::create_directories(dir / "sub");
    auto write = [](const fs::path &p, const Dump &d) {
        std::ofstream o(p, std::ios::binary);
        o.write((const char *)d.data(), (std::streamsize)d.size());
    };
    // a Virus C dump (page A only, 267 bytes, version 0) between a good one and a cut-off one
    Dump c(267, 0);
    const uint8_t head[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x10, 0x01, 0x05};
    memcpy(c.data(), head, sizeof head);
    c[266] = 0xF7;
    Dump file = single(1);
    file.insert(file.end(), c.begin(), c.end());
    Dump cut = single(2);
    cut.pop_back(); // no F7: the scan ends here
    file.insert(file.end(), cut.begin(), cut.end());
    write(dir / "sub" / "Mixed.SYX", file);
    write(dir / "notes.txt", single(3));

    PatchLibrary plain, upgraded;
    CHECK(load_library_file((dir / "sub" / "Mixed.SYX").string(), plain));
    CHECK(plain.dumps.size() == 1); // without page B a Virus C dump cannot be upgraded
    CHECK(load_library_file((dir / "sub" / "Mixed.SYX").string(), upgraded, std::vector<uint8_t>(256, 0)));
    CHECK(upgraded.dumps.size() == 2);
    if (upgraded.dumps.size() == 2)
        CHECK(upgraded.dumps[1].size() == 524 && upgraded.dumps[1][9] != 0 && upgraded.dumps[1][523] == 0xF7);
    PatchLibrary none;
    CHECK(!load_library_file((dir / "missing.mid").string(), none));

    auto libs = load_libraries(dir.string()); // subfolders, any case of the extension, other files skipped
    CHECK(libs.size() == 1 && libs[0].name == "sub/Mixed");

    PatchLibrary out;
    out.dumps.push_back(single(4));
    out.dumps.push_back(Dump(10, 0)); // not a single dump: left out
    out.path = (dir / "no-such-folder" / "x.mid").string();
    std::string err;
    CHECK(!save_library(out, err) && !err.empty());
    out.path = (dir / "x.mid").string();
    PatchLibrary back;
    CHECK(save_library(out, err) && load_library_file(out.path, back) && back.dumps.size() == 1);
    fs::remove_all(dir);
}
