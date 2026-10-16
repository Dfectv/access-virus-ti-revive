// MemorySync against a fake Virus (the embedded constructor: requests go to a callback, replies come back via feed):
// request/reply matching, bank info, which programs a bank sync fetches, cancelling, and the cache file format
#include "check.h"
#include "device/memsync.h"
#include "model/remotedevice.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>

using namespace vti;
using Bytes = std::vector<uint8_t>;

// a single dump as the Virus sends it (524 bytes, 30 bank program -> 10 bank program); the content depends on `seed`
static Dump fake_single(int bank, int program, int seed)
{
    Dump d(524, 0);
    const uint8_t head[9] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x10, (uint8_t)bank, (uint8_t)program};
    std::copy(head, head + 9, d.begin());
    for (size_t i = 9; i < 523; i++)
        d[i] = (uint8_t)((i * 7 + seed * 13) & 0x7F);
    d[523] = 0xF7;
    return d;
}

// answers bank info (39 bank -> 14 bank, 128 fingerprints) and single dump requests from `ram`, synchronously
struct FakeVirus {
    std::vector<Bank> ram = std::vector<Bank>(5);
    MemorySync *sync = nullptr;
    std::vector<Bytes> requests;
    int corrupt = -1; // this program's dump comes back with a different fingerprint than bank info says
    std::function<bool(const Bytes &)> send()
    {
        return [this](const Bytes &m) {
            requests.push_back(m);
            if (m.size() < 9)
                return true;
            int bank = m[7];
            if (m[6] == 0x39) {
                Bytes r = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x14, (uint8_t)bank, 0x00};
                for (int p = 0; p < 128; p++) {
                    int fp = RemoteDevice::single_fingerprint(ram[bank][p]);
                    r.push_back((uint8_t)(fp & 0x7F));
                    r.push_back((uint8_t)(fp >> 7 & 0x7F));
                }
                r.push_back(0x00); // checksum position (not checked by bank_info)
                r.push_back(0xF7);
                sync->feed(r);
            } else if (m[6] == 0x30) {
                int p = m[8];
                sync->feed(p == corrupt ? fake_single(bank, p, 999) : ram[bank][p]);
            }
            return true;
        };
    }
};

TEST(memsync_transact_matches_reply)
{
    MemorySync *ms = nullptr;
    MemorySync sync([&](const Bytes &) {
        ms->feed({0xF0, 0x7E, 0x00, 0x06, 0x02, 0xF7});                         // not an Access SysEx: dropped
        ms->feed({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x14, 0x01, 0x00, 0xF7}); // another reply: not matched
        ms->feed({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x10, 0x00, 0x00, 0x42, 0xF7});
        return true;
    });
    ms = &sync;
    Dump r = sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7}, {0x10, 0x00, 0x00}, 500, 1);
    CHECK_EQ(r.size(), 11u);
    CHECK(r.size() == 11 && r[9] == 0x42);
}

TEST(memsync_transact_times_out_without_reply)
{
    MemorySync sync([](const Bytes &) { return true; });
    auto t0 = std::chrono::steady_clock::now();
    Dump r = sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7}, {0x10, 0x00, 0x00}, 100, 1);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(r.empty());
    CHECK(ms >= 100); // one try waits its timeout, then the drain pause for a late reply (1.5 s)
    CHECK(ms < 2500);
}

TEST(memsync_transact_failed_send)
{
    int sends = 0;
    MemorySync sync([&](const Bytes &) {
        sends++;
        return false;
    });
    CHECK(sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7}, {0x10}, 1000, 3).empty());
    CHECK_EQ(sends, 1); // no retries when the message cannot even be queued
}

TEST(memsync_transact_late_reply)
{
    // the readiness probe's case: the Virus answers only after it worked through what was queued before
    MemorySync *ms = nullptr;
    std::thread late;
    MemorySync sync([&](const Bytes &) {
        late = std::thread([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            ms->feed(fake_single(0, 0, 1));
        });
        return true;
    });
    ms = &sync;
    Dump r = sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7}, {0x10, 0x00, 0x00}, 2000, 1);
    late.join();
    CHECK_EQ(r.size(), 524u);
}

TEST(memsync_bank_info_and_single)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    for (int p = 0; p < 128; p++)
        v.ram[2][p] = fake_single(2, p, p);
    auto info = sync.bank_info(2);
    CHECK_EQ(info.size(), 128u);
    bool same = info.size() == 128;
    for (int p = 0; same && p < 128; p++)
        same = info[p] == RemoteDevice::single_fingerprint(v.ram[2][p]);
    CHECK(same);
    CHECK(sync.single(2, 5) == v.ram[2][5]);
    CHECK(v.requests.size() == 2 && v.requests[0][6] == 0x39 && v.requests[1][6] == 0x30 && v.requests[1][8] == 5);
}

TEST(memsync_sync_bank_fetches_only_stale)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    Bank cache;
    for (int p = 0; p < 128; p++) {
        v.ram[1][p] = fake_single(1, p, p);
        cache[p] = v.ram[1][p];
    }
    cache[3] = fake_single(1, 3, 500); // changed on the Virus
    cache[100].clear();                // not cached yet
    int progress_calls = 0, stale_calls = 0;
    int n = sync.sync_bank(1, cache, [&](int, bool stale) {
        progress_calls++;
        stale_calls += stale;
    });
    CHECK_EQ(n, 2);
    CHECK_EQ(progress_calls, 128);
    CHECK_EQ(stale_calls, 2);
    CHECK(cache[3] == v.ram[1][3] && cache[100] == v.ram[1][100]);
    CHECK_EQ(v.requests.size(), 3u); // bank info + 2 single dumps
    // up to date now: only the bank info
    v.requests.clear();
    CHECK_EQ(sync.sync_bank(1, cache), 0);
    CHECK_EQ(v.requests.size(), 1u);
}

TEST(memsync_sync_bank_cancel_and_errors)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    for (int p = 0; p < 128; p++)
        v.ram[1][p] = fake_single(1, p, p);
    Bank cache; // empty: every program is stale
    std::atomic<bool> stop{true};
    CHECK_EQ(sync.sync_bank(1, cache, nullptr, &stop), -2);
    sync.before_request = [] { return false; }; // e.g. the plug-in is closing
    CHECK_EQ(sync.sync_bank(1, cache), -2);
    int calls = 0;
    sync.before_request = [&] { return ++calls < 3; }; // bank info + 1 program, then cancelled
    CHECK_EQ(sync.sync_bank(1, cache), -2);
    CHECK(cache[0] == v.ram[1][0] && cache[1].empty());
    sync.before_request = nullptr;
    v.corrupt = 2; // a reply that does not match the bank info: the sync stops instead of caching it
    CHECK_EQ(sync.sync_bank(1, cache), -1);
    CHECK(cache[2].empty());
}

TEST(memsync_bank_file_round_trip)
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "vtitests-memsync";
    fs::create_directories(dir);
    const std::string path = (dir / "bank.bin").string();
    Bank out;
    for (int p = 0; p < 128; p += 3)
        out[p] = fake_single(1, p, p);
    CHECK(save_bank_file(path, out));
    CHECK(!fs::exists(path + ".tmp"));
    CHECK(save_bank_file(path, out)); // replaces an existing file
    Bank in;
    CHECK(load_bank_file(path, in));
    CHECK(in == out);
    CHECK(fs::file_size(path) == 8 + 128 * 4 + 43 * 524u);

    Bank none;
    CHECK(!load_bank_file((dir / "missing.bin").string(), none));
    auto write = [&](const std::string &name, const Bytes &b) {
        FILE *f = fopen((dir / name).string().c_str(), "wb");
        fwrite(b.data(), 1, b.size(), f);
        fclose(f);
        return (dir / name).string();
    };
    CHECK(!load_bank_file(write("header.bin", {'X', 'I', 'D', 'I', 0, 1, 8, 0}), none));
    CHECK(!load_bank_file(write("huge.bin", {'M', 'I', 'D', 'I', 0, 1, 8, 0, 0x00, 0x01, 0x00, 0x00}), none));
    CHECK(!load_bank_file(write("short.bin", {'M', 'I', 'D', 'I', 0, 1, 8, 0, 0, 0, 2, 0x0C, 0xF0}), none));
    fs::remove_all(dir);
}
