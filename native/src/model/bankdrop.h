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
#include <string>
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
        checksums(d);
    }
    // the patch with another name (10 characters; printable ASCII, the rest spaces), nothing else changed but the
    // checksums - the original's Alt+Click rename in RAM (E297 (D)); empty if `d` is no single dump
    static Patch renamed(const Patch &d, const std::string &name)
    {
        if (!is_single(d))
            return {};
        Patch r = d;
        for (size_t i = 0; i < 10; i++) {
            const unsigned char c = i < name.size() ? (unsigned char)name[i] : ' ';
            r[249 + i] = c >= 32 && c < 127 ? c : ' ';
        }
        checksums(r);
        return r;
    }
    static void checksums(Patch &d)
    {
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
        std::vector<Write> writes;
        for (int i = 0; i < n; i++) {
            if (!is_single(sources[i]))
                return {};
            Patch d = sources[i];
            address(d, bank, slot + i);
            writes.push_back({bank, slot + i, std::move(d)});
        }
        return remember(std::move(writes), current);
    }

    // a drag within the same RAM bank (E297 (A), (B)): `programs` swap places with the slots from `slot` on, in
    // order; first the selected slots get the target patches, then the targets get the selected ones, like the
    // original. Refused when the two sets overlap or run past the bank's end (the original's result is not known).
    std::vector<Write> swap(const std::vector<int> &programs, int bank, int slot, const std::vector<Patch> &current)
    {
        const int n = (int)programs.size();
        if (bank < 0 || bank >= BANKS || n == 0 || slot < 0 || slot + n > SLOTS || (int)current.size() < SLOTS)
            return {};
        std::set<int> from(programs.begin(), programs.end());
        if ((int)from.size() != n)
            return {};
        for (int i = 0; i < n; i++)
            if (programs[i] < 0 || programs[i] >= SLOTS || from.count(slot + i) || !is_single(current[programs[i]]) ||
                !is_single(current[slot + i]))
                return {};
        std::vector<Write> writes;
        for (int i = 0; i < n; i++) {
            Patch d = current[slot + i];
            address(d, bank, programs[i]);
            writes.push_back({bank, programs[i], std::move(d)});
        }
        for (int i = 0; i < n; i++) {
            Patch d = current[programs[i]];
            address(d, bank, slot + i);
            writes.push_back({bank, slot + i, std::move(d)});
        }
        return remember(std::move(writes), current);
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
    // keeps what `writes` overwrite (from the cache `current`) for the undo; empty if a slot is not cached
    std::vector<Write> remember(std::vector<Write> writes, const std::vector<Patch> &current)
    {
        std::vector<Write> undo;
        for (auto &w : writes) {
            if (w.slot >= (int)current.size() || !is_single(current[w.slot]))
                return {};
            Patch old = current[w.slot];
            address(old, w.bank, w.slot);
            undo.push_back({w.bank, w.slot, std::move(old)});
        }
        undo_ = std::move(undo);
        redo_ = writes;
        return writes;
    }

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
