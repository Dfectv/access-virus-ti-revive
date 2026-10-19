// When the original re-sends the song position (transport.h): at every Play and every loop restart it writes F2,
// the global 73 00 23 01 (+1 MIDI clock tick: the Virus' F2 handler sets position x 6 - 1) and 6F <part> 7F 00 for
// all 16 parts (captures orig-patchchange-2/-3, E275). TI Revive sent F2 only when it anchored its timeline, so the
// arpeggiator and clock-synced LFOs did not restart with the bar.
#include "check.h"
#include "device/transport.h"

using namespace vti;

TEST(transport_restart_on_play_and_on_a_jump)
{
    TransportWatch w;
    CHECK(!w.update(false, 0.0, 0.0));  // stopped
    CHECK(w.update(true, 0.0, 0.25));   // Play
    CHECK(!w.update(true, 0.25, 0.25)); // runs on: the block starts where the last one ended
    CHECK(!w.update(true, 0.5004, 0.25)); // rounding of a block boundary is not a jump
    CHECK(w.update(true, 0.0, 0.25));   // loop back to bar 1
    CHECK(!w.update(true, 0.25, 0.25));
    CHECK(w.update(true, 16.0, 0.25));  // a jump forward (locate while playing)
    CHECK(!w.update(false, 16.25, 0.25));
    CHECK(w.update(true, 16.25, 0.25)); // Play again after a stop
}

TEST(transport_messages_like_the_original)
{
    auto m = transport_messages(17); // 17 sixteenths
    CHECK(m.size() == 18);
    CHECK(m[0] == std::vector<uint8_t>({0xF2, 17, 0}));
    CHECK(m[1] == std::vector<uint8_t>({0xF0, 0x00, 0x20, 0x33, 0x01, 0x00, 0x73, 0x00, 0x23, 0x01, 0xF7}));
    CHECK(m[2] == std::vector<uint8_t>({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x6F, 0x00, 0x7F, 0x00, 0xF7}));
    CHECK(m[17] == std::vector<uint8_t>({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x6F, 0x0F, 0x7F, 0x00, 0xF7}));
    CHECK(transport_messages(200)[0] == std::vector<uint8_t>({0xF2, 200 & 0x7F, 200 >> 7}));
}
