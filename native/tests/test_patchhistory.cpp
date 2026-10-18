// "Revert to" and "Recently Used" of the LOAD menu (E220): per part, the patch before the last load (swapped by a
// revert, edits included) and up to 10 loaded patches, newest first, without duplicates
#include "check.h"
#include "model/patchhistory.h"
#include <cstdio>
#include <cstring>
using namespace vti;

namespace {
std::vector<uint8_t> patch(const char *name, int edit = 0)
{
    std::vector<uint8_t> d(524, 0);
    d[0] = 0xF0, d[523] = 0xF7;
    const size_t n = strlen(name);
    for (size_t i = 0; i < 10; i++)
        d[249 + i] = (uint8_t)(i < n ? name[i] : ' ');
    d[20] = (uint8_t)edit; // a knob value: an edit
    return d;
}
} // namespace

// the user's session in the original (E220 (1), (2))
TEST(patchhistory_load_and_revert)
{
    PatchHistory h;
    CHECK(!h.can_revert(0) && h.recent(0).empty());
    auto project = patch("Fimplex HS");
    h.loaded(0, project, patch("HyprNochBC"));
    h.loaded(0, patch("HyprNochBC"), patch("Fimplex HS"));
    h.loaded(0, patch("Fimplex HS"), patch("Veldisa BC"));
    h.loaded(0, patch("Veldisa BC"), patch("HyprNochBC")); // again: moves to the top
    auto names = h.recent_names(0);
    CHECK(names.size() == 3 && names[0] == "HyprNochBC" && names[1] == "Veldisa BC" && names[2] == "Fimplex HS");
    CHECK(h.can_revert(0) && h.revert_name(0) == "Veldisa BC");
    // the user edits HyprNochBC, then reverts: Veldisa comes back, the edited HyprNochBC is kept for the way back
    auto edited = patch("HyprNochBC", 35);
    auto back = h.revert(0, edited);
    CHECK(back == patch("Veldisa BC") && h.revert_name(0) == "HyprNochBC");
    CHECK(h.revert(0, back) == edited); // the edit survives the swap
    CHECK(h.recent_names(0) == names);  // a revert does not touch the list
}

// a Recently Used entry loads its patch and sets Revert, but keeps the order (E220 (4))
TEST(patchhistory_load_from_recent)
{
    PatchHistory h;
    for (const char *n : {"A", "B", "C"})
        h.loaded(1, patch("Init"), patch(n));
    auto p = h.take_recent(1, 2, patch("C", 7)); // "A", the oldest
    CHECK(p == patch("A") && h.revert_name(1) == "C");
    auto names = h.recent_names(1);
    CHECK(names.size() == 3 && names[0] == "C" && names[2] == "A");
    CHECK(h.take_recent(1, 3, patch("A")).empty()); // no such entry
    CHECK(h.recent(0).empty()); // per part
}

// at most 10 entries per part: the oldest drops
TEST(patchhistory_ten_entries)
{
    PatchHistory h;
    for (int i = 0; i < 12; i++) {
        char n[11];
        snprintf(n, sizeof n, "P%d", i);
        h.loaded(3, patch("Init"), patch(n));
    }
    auto names = h.recent_names(3);
    CHECK(names.size() == PatchHistory::MAX && names.front() == "P11" && names.back() == "P2");
    CHECK(h.loaded(16, patch("x"), patch("y")) == false); // parts 0..15
}
