// load_rom_banks on hand-made firmware packages (the IFF layout of firmware.h, invented content - no Access data):
// a TABL naming the images, sectors as raw 32 KB chunks or as 70-byte records, the single dumps built from them
// (header, pages, checksums), the model's image chosen by name, and the error paths. Runs in CI.
#include "check.h"
#include "access/firmware.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <chrono>
using namespace vti;
namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

namespace {
// a unique suffix per test run without POSIX getpid (the tests build with MSVC too)
std::string uniq()
{
    static const std::string u = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    return u;
}
void be32(Bytes &b, uint32_t v)
{
    for (int i = 3; i >= 0; i--)
        b.push_back((uint8_t)(v >> (8 * i)));
}
Bytes chunk(const char *tag, const Bytes &data)
{
    Bytes c(tag, tag + 4);
    be32(c, (uint32_t)data.size());
    c.insert(c.end(), data.begin(), data.end());
    return c;
}
Bytes form(const std::vector<Bytes> &chunks)
{
    Bytes body;
    for (auto &c : chunks)
        body.insert(body.end(), c.begin(), c.end());
    Bytes f = {'F', 'O', 'R', 'M'};
    be32(f, (uint32_t)body.size());
    f.insert(f.end(), body.begin(), body.end());
    return f;
}
// sector content: byte i of sector `s` = (s * 7 + i) & 0x7F (7-bit like patch data)
uint8_t at(int s, size_t i) { return (uint8_t)((s * 7 + i) & 0x7F); }
Bytes raw_sector(int s)
{
    Bytes d(0x8000);
    for (size_t i = 0; i < d.size(); i++)
        d[i] = at(s, i);
    return d;
}
// the same 32 KB as 512 records of 70 bytes (data at +3..+34 and +38..+69), 0x8c02 bytes in all
Bytes record_sector(int s)
{
    Bytes d(0x8c02, 0x55);
    for (size_t r = 0; r < 512; r++)
        for (size_t k = 0; k < 32; k++) {
            d[70 * r + 3 + k] = at(s, 64 * r + k);
            d[70 * r + 38 + k] = at(s, 64 * r + 32 + k);
        }
    return d;
}
std::string write_temp(const Bytes &b, const char *tag)
{
    fs::path p = fs::temp_directory_path() / (std::string("vtitests-fw-") + tag + "-" + uniq());
    std::ofstream(p, std::ios::binary).write((const char *)b.data(), (std::streamsize)b.size());
    return p.string();
}
// TABL: names NUL-separated, the first with a 1-byte prefix (as in the real package)
Bytes tabl(const std::vector<std::string> &names)
{
    Bytes t = {0x01};
    for (auto &n : names) {
        t.insert(t.end(), n.begin(), n.end());
        t.push_back(0);
    }
    return t;
}
} // namespace

TEST(firmware_rom_banks_from_sectors)
{
    // TI2 image: bank A = P064 (raw) + P065 (records), bank B = P066 + P067 (raw); a short odd chunk is ignored
    Bytes ti2 = form({chunk("P064", raw_sector(64)), chunk("P065", record_sector(65)), chunk("P066", raw_sector(66)),
                      chunk("P067", raw_sector(67)), chunk("XTRA", Bytes(10, 1))});
    Bytes ti = form({chunk("P064", raw_sector(1)), chunk("P065", raw_sector(2))});
    Bytes pkg = form({chunk("TABL", tabl({"os/vti.bin", "os/vti_2.bin"})), chunk("IMG1", ti), chunk("IMG2", ti2)});
    std::string path = write_temp(pkg, "ok");
    std::string err;
    auto banks = load_rom_banks(path, VirusModel::TI2, err);
    CHECK(banks.size() == 2 && err.empty());
    if (banks.size() == 2) {
        const Bytes &a0 = banks[0][0], &a64 = banks[0][64], &b127 = banks[1][127];
        CHECK(a0.size() == 524 && a0[0] == 0xF0 && a0.back() == 0xF7);
        CHECK(a0[6] == 0x10 && a0[7] == 5 && a0[8] == 0); // single dump, ROM bank A = 5, program 0
        CHECK(b127[7] == 6 && b127[8] == 127);
        CHECK(a0[9] == at(64, 0) && a0[9 + 255] == at(64, 255));      // page A
        CHECK(a0[266] == at(64, 256) && a0[266 + 255] == at(64, 511)); // page B
        CHECK(a64[9] == at(65, 0));                                    // program 64 from the second (record) sector
        CHECK(banks[0][100][9] == at(65, 36 * 512));
        CHECK(b127[9] == at(67, 63 * 512));
        unsigned s = 0; // checksums as the Virus expects them
        for (int i = 5; i < 265; i++)
            s += a0[i];
        CHECK(a0[265] == (s & 0x7F));
        for (int i = 265; i < 522; i++)
            s += a0[i];
        CHECK(a0[522] == (s & 0x7F));
    }
    // the TI's image is chosen by its name for the TI
    auto ti_banks = load_rom_banks(path, VirusModel::TI, err);
    CHECK(ti_banks.size() == 1 && ti_banks[0][0][9] == at(1, 0));
    // no Snow image in this package
    err.clear();
    CHECK(load_rom_banks(path, VirusModel::Snow, err).empty() && err.find("vti_snow.bin") != std::string::npos);
    std::remove(path.c_str());
}

TEST(firmware_errors)
{
    std::string err;
    CHECK(load_rom_banks("/vtitests-no-such-firmware.bin", VirusModel::TI2, err).empty() && !err.empty());
    std::string notform = write_temp(Bytes{'R', 'I', 'F', 'F', 0, 0, 0, 0}, "riff");
    err.clear();
    CHECK(load_rom_banks(notform, VirusModel::TI2, err).empty() && err.find("not a Virus firmware") != std::string::npos);
    // an image without the ROM sectors
    Bytes pkg = form({chunk("TABL", tabl({"vti_2.bin"})), chunk("IMG1", form({chunk("P000", raw_sector(0))}))});
    std::string nosec = write_temp(pkg, "nosec");
    err.clear();
    CHECK(load_rom_banks(nosec, VirusModel::TI2, err).empty() && err.find("no ROM sectors") != std::string::npos);
    // a chunk claiming more bytes than the file has: parsing stops, nothing found
    Bytes cut = form({chunk("TABL", tabl({"vti_2.bin"}))});
    cut.insert(cut.end(), {'I', 'M', 'G', '1', 0x7F, 0, 0, 0});
    cut[7] = (uint8_t)(cut.size() - 8);
    std::string trunc = write_temp(cut, "trunc");
    err.clear();
    CHECK(load_rom_banks(trunc, VirusModel::TI2, err).empty() && !err.empty());
    for (auto &p : {notform, nosec, trunc})
        std::remove(p.c_str());
}

TEST(firmware_path_search)
{
    fs::path d = fs::temp_directory_path() / ("vtitests-fwpath-" + uniq());
    fs::create_directories(d / "Common" / "Virus Control.bundle" / "Contents" / "Resources");
    std::ofstream(d / "firmware.bin") << "x";
    // from the bundle's Resources folder up to the installation folder that holds firmware.bin
    std::string res = (d / "Common" / "Virus Control.bundle" / "Contents" / "Resources").string();
    CHECK(firmware_path(res).empty()); // four levels up only: Resources, Contents, bundle, Common
    std::ofstream(d / "Common" / "firmware.bin") << "y";
    CHECK(firmware_path(res) == (d / "Common" / "firmware.bin").string());
    CHECK(firmware_path((d / "Common" / "Virus Control.dll").string()) == (d / "Common" / "firmware.bin").string());
    CHECK(firmware_path("").empty());
    std::error_code ec;
    fs::remove_all(d, ec);
}
