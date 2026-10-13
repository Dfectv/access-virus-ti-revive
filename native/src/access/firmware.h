// firmware.h - the Virus' ROM banks from the user's firmware package (firmware.bin next to Virus Control), as the
// original builds them (decompile 0x1800b01c0): an IFF "FORM" whose TABL names the chunks; the images vti.bin (TI),
// vti_2.bin (TI2), vti_snow.bin (Snow) are FORMs of 32 KB flash sectors (35842-byte chunks = 512 records of 70 bytes,
// 64 data bytes at +3 and +38); ROM bank i = sectors P(064+2i) and P(065+2i), 64 patches of 512 bytes each.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace vti {

enum class VirusModel { TI, TI2, Snow };

// ROM banks A.. of `model` as 524-byte single dumps (bank byte 5 + i, program 0..127). Empty on failure.
std::vector<std::vector<std::vector<uint8_t>>> load_rom_banks(const std::string &firmware_bin, VirusModel model,
                                                               std::string &error);
// firmware.bin of the installation that holds `virus_control` (the DLL or the bundle's Resources folder)
std::string firmware_path(const std::string &virus_control);

} // namespace vti
