// Library and bank patches dropped onto a RAM slot of the browser's other pane, like the original (E294): each source
// dump goes unchanged into the next slot from the target on (only bank/program and the checksums are new), cut at
// the bank's end; "Undo last operation" writes back what the last drop overwrote. Plus the browser's selection.
#include "check.h"
#include "model/bankdrop.h"
#include <cstring>
using namespace vti;

namespace {
// a library patch as the original's files hold it: bank byte 1, program = its position, bytes 10..12 its origin
std::vector<uint8_t> patch(const char *name, int program, uint8_t flags = 0x20, uint8_t obank = 6, uint8_t oprog = 0x7E)
{
    std::vector<uint8_t> d(524, 0);
    const uint8_t head[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x10, 0x01};
    memcpy(d.data(), head, sizeof head);
    d[8] = (uint8_t)program, d[9] = 0x0C, d[10] = flags, d[11] = obank, d[12] = oprog;
    for (int i = 13; i < 249; i++)
        d[i] = (uint8_t)((i * 7 + program) & 0x7F); // sound data
    const size_t n = strlen(name);
    for (size_t i = 0; i < 10; i++)
        d[249 + i] = (uint8_t)(i < n ? name[i] : ' ');
    d[523] = 0xF7;
    BankDrop::address(d, 0, program); // valid checksums
    return d;
}
bool checksums_ok(const std::vector<uint8_t> &d)
{
    unsigned a = 0;
    for (int i = 5; i < 265; i++)
        a += d[i];
    if (d[265] != (a & 0x7F))
        return false;
    for (int i = 265; i < 522; i++)
        a += d[i];
    return d[522] == (a & 0x7F);
}
std::vector<std::vector<uint8_t>> ram_bank(int bank)
{
    std::vector<std::vector<uint8_t>> b;
    for (int p = 0; p < 128; p++) {
        auto d = patch("RAM", p, 0x00, (uint8_t)(bank + 1), (uint8_t)p);
        BankDrop::address(d, bank, p);
        b.push_back(d);
    }
    return b;
}
// every byte but bank (7), program (8) and the two checksums equals the source (E294: the original changes nothing else)
bool same_but_address(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
        if (i != 7 && i != 8 && i != 265 && i != 522 && a[i] != b[i])
            return false;
    return true;
}
} // namespace

// one library patch onto RAM A slot 4 (E294 (2)): one dump to bank 1 program 3, bytes 10..12 as in the file
TEST(bankdrop_single)
{
    BankDrop drop;
    auto src = patch("StarSky", 0);
    auto w = drop.drop({src}, 0, 3, ram_bank(0));
    CHECK(w.size() == 1);
    CHECK(w[0].bank == 0 && w[0].slot == 3);
    CHECK(w[0].dump[7] == 1 && w[0].dump[8] == 3);
    CHECK(w[0].dump[10] == 0x20 && w[0].dump[11] == 6 && w[0].dump[12] == 0x7E);
    CHECK(same_but_address(w[0].dump, src) && checksums_ok(w[0].dump));
    CHECK(drop.can_undo());
}

// nine patches onto slot 8 (E294 (3)): consecutive slots in the given order; RAM D = bank byte 4
TEST(bankdrop_several_consecutive)
{
    BankDrop drop;
    std::vector<std::vector<uint8_t>> src;
    for (int i = 0; i < 9; i++)
        src.push_back(patch("Lib", 4 + i, (uint8_t)i));
    auto w = drop.drop(src, 3, 7, ram_bank(3));
    CHECK(w.size() == 9);
    for (int i = 0; i < (int)w.size(); i++) {
        CHECK(w[i].bank == 3 && w[i].slot == 7 + i);
        CHECK(w[i].dump[7] == 4 && w[i].dump[8] == 7 + i);
        CHECK(same_but_address(w[i].dump, src[i]) && checksums_ok(w[i].dump));
    }
}

// more patches than slots left: cut at the bank's end, nothing wraps to slot 1
TEST(bankdrop_cut_at_bank_end)
{
    BankDrop drop;
    std::vector<std::vector<uint8_t>> src;
    for (int i = 0; i < 5; i++)
        src.push_back(patch("Lib", i));
    auto w = drop.drop(src, 1, 125, ram_bank(1));
    CHECK(w.size() == 3);
    CHECK(w[0].slot == 125 && w[2].slot == 127);
}

// undo writes back what the drop overwrote, addressed to the same slots, once
TEST(bankdrop_undo_restores_overwritten)
{
    BankDrop drop;
    auto before = ram_bank(2);
    drop.drop({patch("A", 0), patch("B", 1)}, 2, 10, before);
    auto u = drop.take_undo();
    CHECK(u.size() == 2);
    CHECK(u[0].bank == 2 && u[0].slot == 10 && u[0].dump == before[10]);
    CHECK(u[1].bank == 2 && u[1].slot == 11 && u[1].dump == before[11]);
    CHECK(!drop.can_undo() && drop.take_undo().empty()); // one level, like "Undo last operation"
}

// a later drop replaces the undo; another store into RAM (SAVE) clears it so undo never writes over newer patches
TEST(bankdrop_undo_is_last_operation_only)
{
    BankDrop drop;
    auto bank = ram_bank(0);
    drop.drop({patch("A", 0)}, 0, 0, bank);
    drop.drop({patch("B", 0)}, 0, 5, bank);
    auto u = drop.take_undo();
    CHECK(u.size() == 1 && u[0].slot == 5);
    drop.drop({patch("C", 0)}, 0, 6, bank);
    drop.forget();
    CHECK(!drop.can_undo());
}

// refused (nothing to send, the undo of the last drop stays): no RAM bank, slot outside, no or broken source, an
// overwritten slot the cache does not know (bank not synced: undo could not restore it)
TEST(bankdrop_refuses)
{
    BankDrop drop;
    auto bank = ram_bank(0);
    drop.drop({patch("A", 0)}, 0, 0, bank);
    CHECK(drop.drop({patch("X", 0)}, 4, 0, bank).empty()); // ROM A
    CHECK(drop.drop({patch("X", 0)}, -1, 0, bank).empty());
    CHECK(drop.drop({patch("X", 0)}, 0, 128, bank).empty());
    CHECK(drop.drop({}, 0, 0, bank).empty());
    auto broken = patch("X", 0);
    broken.resize(300);
    CHECK(drop.drop({broken}, 0, 0, bank).empty());
    auto multi = patch("X", 0);
    multi[6] = 0x11; // not a single dump
    CHECK(drop.drop({multi}, 0, 0, bank).empty());
    auto unknown = bank;
    unknown[9].clear();
    CHECK(drop.drop({patch("X", 0), patch("Y", 1)}, 0, 8, unknown).empty());
    CHECK(drop.drop({patch("X", 0)}, 0, 8, std::vector<std::vector<uint8_t>>(3)).empty()); // short cache
    auto u = drop.take_undo();
    CHECK(u.size() == 1 && u[0].slot == 0);
}

// click selects one entry, Shift-click a range from the anchor, Ctrl-click toggles one; a press on a selected entry
// keeps the selection (it may be dragged), releasing it without a drag selects only that entry
TEST(browserselection_click_shift_ctrl)
{
    BrowserSelection s;
    s.press(5, false, false);
    CHECK(s.items() == std::vector<int>{5});
    s.press(8, true, false);
    CHECK((s.items() == std::vector<int>{5, 6, 7, 8}));
    s.press(2, true, false); // the range goes from the anchor (5) the other way
    CHECK((s.items() == std::vector<int>{2, 3, 4, 5}));
    s.press(9, false, true);
    CHECK((s.items() == std::vector<int>{2, 3, 4, 5, 9}));
    s.press(3, false, true);
    CHECK((s.items() == std::vector<int>{2, 4, 5, 9}));
    s.press(4, false, false); // already selected: kept for a drag
    CHECK((s.items() == std::vector<int>{2, 4, 5, 9}));
    s.release(4, true);
    CHECK((s.items() == std::vector<int>{2, 4, 5, 9}));
    s.press(4, false, false);
    s.release(4, false);
    CHECK(s.items() == std::vector<int>{4});
    CHECK(s.contains(4) && !s.contains(5));
    s.clear();
    CHECK(s.items().empty());
}
