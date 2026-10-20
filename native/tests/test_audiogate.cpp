// The audio ring's gate (audiogate.h, review 2026-10-19 H1): process() checked that the ring was there and then read
// it, while the worker could unmap it in between (Virus switched off, a wake, a failed write) - a crash in the host.
// Readers now hold a Use while they read; close() lets no new reader in and returns only when the last one is out.
#include "check.h"
#include "device/audiogate.h"
#include <atomic>
#include <chrono>
#include <thread>

using namespace vti;

TEST(audiogate_closed_until_opened)
{
    AudioGate g;
    CHECK(!g.is_open());
    AudioGate::Use u(g);
    CHECK(!u);
}

TEST(audiogate_reader_gets_in_while_open)
{
    AudioGate g;
    g.open();
    CHECK(g.is_open());
    AudioGate::Use u(g);
    CHECK(u);
}

TEST(audiogate_no_reader_after_close)
{
    AudioGate g;
    g.open();
    g.close();
    CHECK(!g.is_open());
    AudioGate::Use u(g);
    CHECK(!u);
}

TEST(audiogate_close_waits_for_the_reader_inside)
{
    AudioGate g;
    g.open();
    std::atomic<bool> inside{false}, release{false}, unmapped{false}, read_after_unmap{false};
    std::thread reader([&] {
        AudioGate::Use u(g);
        inside = true;
        while (!release)
            std::this_thread::yield();
        read_after_unmap = unmapped.load(); // the "read" at the end of the reader's region
    });
    while (!inside)
        std::this_thread::yield();
    std::thread closer([&] {
        g.close();
        unmapped = true; // what DeviceOut does right after close(): AudioIn::close()
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(!unmapped); // still waiting for the reader
    release = true;
    reader.join();
    closer.join();
    CHECK(unmapped);
    CHECK(!read_after_unmap);
}

TEST(audiogate_reopen_after_close)
{
    AudioGate g;
    g.open();
    g.close();
    g.open();
    AudioGate::Use u(g);
    CHECK(u);
}
