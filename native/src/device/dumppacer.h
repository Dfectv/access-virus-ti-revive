// dumppacer.h - a single dump on cable 3 goes out in pieces, with the DAW's events in between on cable 5.
//
// Why: the original writes a patch dump (524 bytes = 700 bytes of USB-MIDI packets) in pieces of 64 bytes, one
// every 6-12 ms (~130 ms per dump, also when the patches change fast; never two dumps at once), and sends its notes, clock ticks and time stamps between them on cable 5 (capture of a patch change
// while another part plays, E270). Cable 3 reaches only the Virus' host CPU, which parses the dump byte by byte;
// cable 2 would put a note's bytes into that parse (one parser for all cables but 5, E252), cable 5 goes to the DSPs
// only. Written in one piece, the dump held the notes until the Virus had taken all of it, and the other parts
// stopped for a moment (E269). Queued messages wait until the dump is out (they go on cable 2).
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vti {

class DumpPacer {
public:
    static constexpr size_t PIECE = 64;              // bytes per write (16 USB-MIDI packets), as the original
    static constexpr int64_t GAP_NS = 10 * 1000000LL; // between two pieces: the original's average is ~11 ms

    bool active() const { return off_ < pk_.size(); }
    // the packed USB-MIDI bytes of a dump; the first piece is due at once
    void start(const std::vector<uint8_t> &pk, int64_t now_ns)
    {
        pk_ = pk;
        off_ = 0;
        due_ = now_ns;
    }
    // the next piece if it is due
    bool next(int64_t now_ns, std::vector<uint8_t> &out)
    {
        if (!active() || now_ns < due_)
            return false;
        size_t n = pk_.size() - off_ < PIECE ? pk_.size() - off_ : PIECE;
        out.assign(pk_.begin() + (long)off_, pk_.begin() + (long)(off_ + n));
        off_ += n;
        due_ = now_ns + GAP_NS;
        if (!active())
            pk_.clear(), off_ = 0;
        return true;
    }
    // nanoseconds until the next piece is due (0: due now or no dump)
    int64_t wait_ns(int64_t now_ns) const { return active() && now_ns < due_ ? due_ - now_ns : 0; }
    // the cable for an event from the DAW: 5 while a dump runs (the host CPU is in the middle of parsing it)
    uint8_t event_cable(uint8_t cable) const { return active() ? 5 : cable; }

private:
    std::vector<uint8_t> pk_;
    size_t off_ = 0;
    int64_t due_ = 0;
};

} // namespace vti
