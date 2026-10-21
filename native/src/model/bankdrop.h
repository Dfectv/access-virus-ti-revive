// bankdrop.h - patches dragged from one browser pane onto a RAM slot of the other, like the original (E294): each
// source dump goes into the next slot from the drop target on, in order, cut at the bank's end, without asking; the
// original changes nothing but bank/program (bytes 7, 8) and the checksums (bytes 10..12, the patch's origin, stay
// as in the source). "Undo last operation" writes back what the last drop overwrote; used again it puts the drop
// back, and so on - the original toggles within its last operation (E295). Plus the browser's
// selection (click, Shift range, Ctrl toggle). Patches are 524-byte single dumps. Header only. Not thread safe: the
// caller locks.
#pragma once
#include <algorithm>
#include <cstdint>
#include <set>
#include <vector>

namespace vti {

class BankDrop {
public:
    using Patch = std::vector<uint8_t>;
    struct Write {
        int bank; // RAM bank 0..3 (RAM A..D)
        int slot; // program 0..127
        Patch dump;
    };
    static constexpr int BANKS = 4, SLOTS = 128;

    static bool is_single(const Patch &d)
    {
        return d.size() == 524 && d[0] == 0xF0 && d[1] == 0x00 && d[2] == 0x20 && d[3] == 0x33 && d[6] == 0x10 &&
               d[523] == 0xF7;
    }
    // address a single dump to RAM `bank` (0..3) program `slot` - bank byte 1..4 - and recompute both checksums
    static void address(Patch &d, int bank, int slot)
    {
        d[7] = (uint8_t)(bank + 1), d[8] = (uint8_t)slot;
        unsigned a = 0;
        for (int i = 5; i < 265; i++)
            a += d[i];
        d[265] = a & 0x7F;
        for (int i = 265; i < 522; i++)
            a += d[i];
        d[522] = a & 0x7F;
    }

    // the dumps to send for `sources` dropped on RAM `bank` at `slot`; `current` is the bank's cache (what they
    // overwrite, kept for undo). Empty, and the previous undo kept, when the drop cannot be done or not be undone.
    std::vector<Write> drop(const std::vector<Patch> &sources, int bank, int slot, const std::vector<Patch> &current)
    {
        if (bank < 0 || bank >= BANKS || slot < 0 || slot >= SLOTS || sources.empty())
            return {};
        const int n = std::min((int)sources.size(), SLOTS - slot);
        std::vector<Write> writes, undo;
        for (int i = 0; i < n; i++) {
            const int s = slot + i;
            if (!is_single(sources[i]) || s >= (int)current.size() || !is_single(current[s]))
                return {};
            Patch d = sources[i];
            address(d, bank, s);
            writes.push_back({bank, s, std::move(d)});
            Patch old = current[s];
            address(old, bank, s);
            undo.push_back({bank, s, std::move(old)});
        }
        undo_ = std::move(undo);
        redo_ = writes;
        return writes;
    }
    bool can_undo() const { return !undo_.empty(); }
    // "Undo last operation": the writes that restore the slots as they were before the last drop - or, the next
    // time, as the drop left them (the original toggles, E295)
    std::vector<Write> take_undo()
    {
        std::vector<Write> u = undo_;
        undo_.swap(redo_);
        return u;
    }
    // another store into RAM (SAVE, a bank read again): an undo would now write over newer patches
    void forget() { undo_.clear(), redo_.clear(); }

private:
    std::vector<Write> undo_, redo_; // what the next undo writes, and what the one after it writes
};

// the browser's selected entries (programs 0..127 of the bank a pane shows)
class BrowserSelection {
public:
    // mouse down: plain = this entry only (a selected one stays as it is: the selection may be dragged), Shift = the
    // range from the last plain/Ctrl click, Ctrl = toggle this entry
    void press(int prog, bool shift, bool ctrl)
    {
        if (shift && anchor_ >= 0) {
            sel_.clear();
            for (int p = std::min(anchor_, prog); p <= std::max(anchor_, prog); p++)
                sel_.insert(p);
            return;
        }
        anchor_ = prog;
        if (ctrl) {
            if (!sel_.erase(prog))
                sel_.insert(prog);
            return;
        }
        if (!sel_.count(prog))
            sel_ = {prog};
        kept_ = prog;
    }
    // mouse up: a plain press on a selected entry that was not dragged selects only it
    void release(int prog, bool dragged)
    {
        if (!dragged && kept_ == prog)
            sel_ = {prog};
        kept_ = -1;
    }
    std::vector<int> items() const { return {sel_.begin(), sel_.end()}; } // ascending: the order a drop writes
    bool contains(int prog) const { return sel_.count(prog) != 0; }
    void clear() { sel_.clear(), anchor_ = -1, kept_ = -1; }

private:
    std::set<int> sel_;
    int anchor_ = -1, kept_ = -1;
};

} // namespace vti
