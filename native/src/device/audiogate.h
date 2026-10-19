// audiogate.h - who may read the Virus' audio ring, and when the worker may unmap it.
//
// Why: the worker closes the ring (munmap / UnmapViewOfFile / delete on macOS) when the Virus goes away (switched
// off, a wake, a failed broker write), while process() on the host's audio thread and the editor's diag report read
// it. A reader that checked "audio ready" and then read could find the mapping gone in between: a crash in the host
// (review 2026-10-19, H1). Readers now hold a Use for each short read; close() lets no new reader in and returns
// only when the readers inside are out, so the caller can unmap right after it. A reader never waits: Use is two
// atomic operations and no lock (real-time safe). Hold a Use only around the reads themselves, never across a wait
// for the worker (the worker may be waiting in close()).
#pragma once
#include <atomic>
#include <thread>

namespace vti {

class AudioGate {
public:
    class Use {
    public:
        explicit Use(AudioGate &g) : g_(g)
        {
            // seq_cst on both sides: either close() sees this reader, or this reader sees the gate closed
            g_.users_.fetch_add(1);
            ok_ = g_.open_.load();
            if (!ok_)
                g_.users_.fetch_sub(1);
        }
        ~Use()
        {
            if (ok_)
                g_.users_.fetch_sub(1);
        }
        Use(const Use &) = delete;
        Use &operator=(const Use &) = delete;
        explicit operator bool() const { return ok_; }

    private:
        AudioGate &g_;
        bool ok_ = false;
    };

    void open() { open_.store(true); } // after the mapping is in place
    bool is_open() const { return open_.load(); }

    // No new reader gets in; returns once the readers inside are out (they hold a Use for microseconds).
    void close()
    {
        open_.store(false);
        while (users_.load() > 0)
            std::this_thread::yield();
    }

private:
    std::atomic<bool> open_{false};
    std::atomic<int> users_{0};
};

} // namespace vti
