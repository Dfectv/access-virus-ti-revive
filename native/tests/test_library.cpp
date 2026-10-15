// Patch libraries and the Control Center's export (src/access/library, src/cc/controlcenter) on generated single
// dumps: .mid and .syx round trips keep the patch data; save_library numbers the patches and recomputes the checksums.
#include "access/library.h"
#include "cc/controlcenter.h"
#include "check.h"
#include <cstring>
#include <filesystem>

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
