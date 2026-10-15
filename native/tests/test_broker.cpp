// Broker client against a fake broker on its own Unix socket (never the real broker's): HELLO/STATE, writes split
// below VTIMSG_MAX, received data, control requests, audio start, observer mode, lost connection. POSIX only.
#include "check.h"
#include "device/broker.h"
#ifndef _WIN32
#include "cc/controlcenter.h"
#include "device/memsync.h"
#include "device/usbmidi.h"
#include "access/library.h"
#include "model/remotedevice.h"
#include <cstdlib>
#include <functional>
#include "../../src/broker/vtiproto.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace vti;
using Bytes = std::vector<uint8_t>;

namespace {
bool io(int fd, void *p, size_t n, bool wr)
{
    auto *b = (uint8_t *)p;
    while (n) {
        ssize_t r = wr ? ::send(fd, b, n, MSG_NOSIGNAL) : ::recv(fd, b, n, 0);
        if (r <= 0)
            return false;
        b += r, n -= (size_t)r;
    }
    return true;
}

struct Msg {
    uint16_t type;
    Bytes data;
};

// one client at a time; answers like the broker, or stays silent where a test wants a timeout
struct FakeBroker {
    std::string path;
    int lfd = -1, cfd = -1;
    std::thread th;
    std::mutex mu;
    std::vector<Msg> got;
    bool present = true;
    std::atomic<bool> answer_control{true}, answer_audio{true}, answer_observe{true};
    std::atomic<int32_t> audio_result{1};
    // optional: own answers to control requests (setup = the 8 setup bytes; empty reply = -1), and SysEx the client
    // writes (USB-MIDI put back together), e.g. a fake Virus answering dump requests with reply_sysex()
    std::function<Bytes(const Bytes &setup)> on_control;
    std::function<void(FakeBroker &, const Bytes &sysex)> on_sysex;
    void reply_sysex(const Bytes &m)
    {
        Bytes rx = {0x86};
        auto pk = usbmidi_pack(m, 3);
        rx.insert(rx.end(), pk.begin(), pk.end());
        for (size_t off = 1; off < rx.size(); off += 1020) { // below VTIMSG_MAX, on packet boundaries
            Bytes part = {0x86};
            part.insert(part.end(), rx.begin() + off, rx.begin() + std::min(rx.size(), off + 1020));
            reply(VTIMSG_RX, part);
        }
    }

    FakeBroker()
    {
        path = (std::filesystem::temp_directory_path() / ("vtitests-broker-" + std::to_string(getpid()) + ".sock")).string();
        ::unlink(path.c_str());
        sockaddr_un sa{};
        sa.sun_family = AF_UNIX;
        snprintf(sa.sun_path, sizeof sa.sun_path, "%s", path.c_str());
        lfd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (lfd < 0 || ::bind(lfd, (sockaddr *)&sa, sizeof sa) || ::listen(lfd, 1))
            std::fprintf(stderr, "  fake broker: cannot listen on %s\n", path.c_str());
        th = std::thread([this] { serve(); });
    }
    ~FakeBroker() { stop(); }
    void stop()
    {
        if (lfd >= 0)
            ::shutdown(lfd, SHUT_RDWR);
        if (cfd >= 0)
            ::shutdown(cfd, SHUT_RDWR);
        if (th.joinable())
            th.join();
        if (cfd >= 0)
            ::close(cfd), cfd = -1;
        if (lfd >= 0)
            ::close(lfd), lfd = -1;
        ::unlink(path.c_str());
    }
    void reply(uint16_t type, const Bytes &d)
    {
        std::lock_guard<std::mutex> g(wmu); // the test thread and the server thread both send
        vtimsg h{type, (uint16_t)d.size()};
        io(cfd, &h, sizeof h, true);
        if (!d.empty())
            io(cfd, (void *)d.data(), d.size(), true);
    }
    static Bytes u32(uint32_t v) { return Bytes{(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)}; }
    void serve()
    {
        cfd = ::accept(lfd, nullptr, nullptr);
        if (cfd < 0)
            return;
        Bytes buf(VTIMSG_MAX);
        UsbMidiParser parser([this](uint8_t, int, const Bytes &m) {
            if (on_sysex && !m.empty() && m[0] == 0xF0)
                on_sysex(*this, m);
        });
        vtimsg h;
        while (io(cfd, &h, sizeof h, false) && h.len <= VTIMSG_MAX && (!h.len || io(cfd, buf.data(), h.len, false))) {
            Bytes d(buf.begin(), buf.begin() + h.len);
            {
                std::lock_guard<std::mutex> g(mu);
                got.push_back({h.type, d});
            }
            if (h.type == VTIMSG_HELLO)
                reply(VTIMSG_STATE, u32(present ? 1 : 0));
            else if (h.type == VTIMSG_WRITE && d.size() > 1)
                parser.feed(d[0], d.data() + 1, d.size() - 1);
            else if (h.type == VTIMSG_CONTROL && on_control && d.size() >= 12) {
                Bytes data = on_control(Bytes(d.begin() + 4, d.begin() + 12));
                Bytes r(d.begin(), d.begin() + 4);
                Bytes res = u32(data.empty() ? (uint32_t)-1 : (uint32_t)data.size());
                r.insert(r.end(), res.begin(), res.end());
                r.insert(r.end(), data.begin(), data.end());
                reply(VTIMSG_CONTROL_REPLY, r);
            } else if (h.type == VTIMSG_CONTROL && answer_control && d.size() >= 12) {
                uint16_t len = (uint16_t)(d[10] | d[11] << 8);
                Bytes r(d.begin(), d.begin() + 4); // seq
                bool in = d[4] & 0x80;
                Bytes res = u32(in ? len : (uint32_t)(d.size() - 12));
                r.insert(r.end(), res.begin(), res.end());
                for (int i = 0; in && i < len; i++)
                    r.push_back((uint8_t)(0xA0 + i));
                reply(VTIMSG_CONTROL_REPLY, r);
            } else if (h.type == VTIMSG_AUDIO && answer_audio)
                reply(VTIMSG_AUDIO_REPLY, u32((uint32_t)audio_result.load()));
            else if (h.type == VTIMSG_OBSERVE && answer_observe) {
                reply(VTIMSG_OBSERVE_REPLY, u32(VTI_PROTO_VERSION));
                reply(VTIMSG_OBS_LOG, Bytes{'h', 'i'});
            }
        }
    }
    std::vector<Msg> messages(uint16_t type)
    {
        std::lock_guard<std::mutex> g(mu);
        std::vector<Msg> out;
        for (auto &m : got)
            if (m.type == type)
                out.push_back(m);
        return out;
    }

private:
    std::mutex wmu;
};

template <class F> bool eventually(F f, int ms = 1000)
{
    for (int i = 0; i < ms / 5; i++) {
        if (f())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return f();
}
} // namespace

TEST(broker_connect_errors)
{
    Broker b;
    std::string err;
    b.set_endpoint((std::filesystem::temp_directory_path() / "vtitests-no-such-broker.sock").string());
    CHECK(!b.open(err));
    CHECK(err.find("cannot connect") != std::string::npos);
    err.clear();
    b.set_endpoint("/tmp/" + std::string(200, 'x'));
    CHECK(!b.open(err) && err.find("too long") != std::string::npos);
    CHECK(!b.write(0x06, (const uint8_t *)"x", 1)); // not connected
}

TEST(broker_hello_state_write_rx)
{
    FakeBroker fb;
    Broker b;
    b.set_endpoint(fb.path);
    b.set_name("vtitests");
    std::atomic<int> states{0};
    b.on_state([&](bool c) { states += c ? 1 : 100; });
    Bytes rx;
    std::mutex rxm;
    b.on_rx([&](uint8_t ep, const uint8_t *d, size_t n) {
        std::lock_guard<std::mutex> g(rxm);
        rx.assign(d, d + n);
        rx.insert(rx.begin(), ep);
    });
    std::string err;
    CHECK(b.open(err));
    CHECK(b.device_present());
    CHECK(states.load() == 1);
    auto hello = fb.messages(VTIMSG_HELLO);
    CHECK(hello.size() == 1 && hello[0].data.size() == 4 + 8 && hello[0].data[0] == VTI_PROTO_VERSION &&
          std::string(hello[0].data.begin() + 4, hello[0].data.end()) == "vtitests");
    // a long write is split into messages of at most 1024 data bytes, each with its endpoint
    Bytes big(2500);
    for (size_t i = 0; i < big.size(); i++)
        big[i] = (uint8_t)i;
    CHECK(b.write(0x06, big.data(), big.size()));
    CHECK(eventually([&] { return fb.messages(VTIMSG_WRITE).size() == 3; }));
    auto w = fb.messages(VTIMSG_WRITE);
    Bytes joined;
    bool eps = w.size() == 3;
    for (auto &m : w) {
        eps = eps && !m.data.empty() && m.data[0] == 0x06 && m.data.size() <= 1025;
        joined.insert(joined.end(), m.data.begin() + 1, m.data.end());
    }
    CHECK(eps && joined == big);
    // data from the Virus reaches the handler
    fb.reply(VTIMSG_RX, Bytes{0x86, 0x29, 0x90, 0x3C, 0x64});
    CHECK(eventually([&] {
        std::lock_guard<std::mutex> g(rxm);
        return rx == Bytes{0x86, 0x29, 0x90, 0x3C, 0x64};
    }));
    // the device goes away: STATE 0
    fb.reply(VTIMSG_STATE, FakeBroker::u32(0));
    CHECK(eventually([&] { return !b.device_present(); }));
    CHECK(states.load() == 101);
    b.close();
}

TEST(broker_control_audio_observe)
{
    FakeBroker fb;
    Broker b;
    b.set_endpoint(fb.path);
    std::string err;
    CHECK(b.open(err));
    uint8_t in[18] = {};
    CHECK_EQ(b.control(0x80, 6, 0x0100, 0, in, sizeof in), 18); // IN: the reply's data lands in the buffer
    CHECK(in[0] == 0xA0 && in[17] == 0xB1);
    uint8_t out[3] = {1, 2, 3};
    CHECK_EQ(b.control(0x40, 0x31, 0, 0, out, 3), 3); // OUT: the data goes with the request
    auto c = fb.messages(VTIMSG_CONTROL);
    CHECK(c.size() == 2 && c[1].data.size() == 15 && c[1].data[12] == 1 && c[1].data[14] == 3);
    CHECK_EQ(b.audio(true), 1);
    fb.audio_result = -1;
    CHECK_EQ(b.audio(false), -1);
    std::atomic<int> logs{0};
    CHECK(b.observe(
        [&](uint16_t t, const uint8_t *d, size_t n) {
            if (t == VTIMSG_OBS_LOG && n == 2 && d[0] == 'h')
                logs++;
        },
        err));
    CHECK(eventually([&] { return logs.load() == 1; }));
    // no answers: control and audio time out instead of hanging
    fb.answer_control = false;
    fb.answer_audio = false;
    auto t0 = std::chrono::steady_clock::now();
    CHECK_EQ(b.control(0x80, 6, 0x0100, 0, in, sizeof in, 100), -1);
    CHECK_EQ(b.audio(true, 100), -1);
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2));
    b.close();
    CHECK(!b.device_present());
}

TEST(broker_old_broker_refuses_observer)
{
    FakeBroker fb;
    fb.answer_observe = false; // a broker older than protocol 4 ignores OBSERVE
    Broker b;
    b.set_endpoint(fb.path);
    std::string err;
    CHECK(b.open(err));
    CHECK(!b.observe([](uint16_t, const uint8_t *, size_t) {}, err));
    CHECK(err.find("too old") != std::string::npos);
    CHECK(!b.write(0x06, (const uint8_t *)"x", 1)); // the connection was closed: never left as the owner
}

TEST(broker_connection_lost)
{
    FakeBroker fb;
    Broker b;
    b.set_endpoint(fb.path);
    std::string err;
    CHECK(b.open(err) && b.device_present());
    fb.stop(); // the broker exits
    CHECK(eventually([&] { return !b.device_present(); }));
    b.close();
}
namespace {
// a 524-byte single dump as the Virus sends it; the content depends on `seed`
Bytes fake_single(int bank, int program, int seed)
{
    Bytes d(524, 0);
    const uint8_t head[9] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x10, (uint8_t)bank, (uint8_t)program};
    std::copy(head, head + 9, d.begin());
    for (size_t i = 9; i < 523; i++)
        d[i] = (uint8_t)((i * 5 + seed * 11) & 0x7F);
    for (int i = 0; i < 10; i++)
        d[249 + i] = (uint8_t)("Fake Patch"[i]);
    d[523] = 0xF7;
    return d;
}

// the Virus side of the fake broker: bank info (39 -> 14) and single dump requests (30 -> 10) of RAM bank 1
void fake_virus(FakeBroker &fb, const Bytes &m)
{
    if (m.size() < 9 || m[1] != 0x00 || m[2] != 0x20 || m[3] != 0x33)
        return;
    if (m[6] == 0x39) {
        Bytes r = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x14, m[7], 0x00};
        for (int p = 0; p < 128; p++) {
            int fp = RemoteDevice::single_fingerprint(fake_single(m[7], p, p));
            r.push_back((uint8_t)(fp & 0x7F));
            r.push_back((uint8_t)(fp >> 7 & 0x7F));
        }
        r.push_back(0x00);
        r.push_back(0xF7);
        fb.reply_sysex(r);
    } else if (m[6] == 0x30 && m.size() >= 10)
        fb.reply_sysex(fake_single(m[7], m[8], m[8]));
}

// the Control Center's folders below a temporary HOME: never the user's real cache or Documents
struct TempHome {
    std::string old;
    bool had = false;
    std::filesystem::path dir;
    TempHome()
    {
        if (const char *h = getenv("HOME"))
            old = h, had = true;
        dir = std::filesystem::temp_directory_path() / ("vtitests-home-" + std::to_string(getpid()));
        std::filesystem::create_directories(dir);
        setenv("HOME", dir.string().c_str(), 1);
    }
    ~TempHome()
    {
        if (had)
            setenv("HOME", old.c_str(), 1);
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};
} // namespace

TEST(cc_read_device_info)
{
    FakeBroker fb;
    auto str = [](const std::string &s) {
        Bytes d = {(uint8_t)(2 + 2 * s.size()), 3};
        for (char c : s)
            d.push_back((uint8_t)c), d.push_back(0);
        return d;
    };
    fb.on_control = [&](const Bytes &setup) {
        uint16_t value = (uint16_t)(setup[2] | setup[3] << 8);
        if (value == 0x0100) // device descriptor: 133e:0815 bcd 0300, strings 1 and 2
            return Bytes{18, 1, 0, 2, 0, 0, 0, 64, 0x3E, 0x13, 0x15, 0x08, 0x00, 0x03, 1, 2, 0, 1};
        if (value == 0x0301)
            return str("Access Music");
        if (value == 0x0302)
            return str("Virus TI\x01"); // a non-ASCII character becomes '?'
        return Bytes();
    };
    Broker b;
    b.set_endpoint(fb.path);
    std::string err;
    CHECK(b.open(err));
    cc::DeviceInfo info;
    CHECK(cc::read_device_info(b, info, err));
    CHECK(info.vendor == 0x133E && info.product == 0x0815 && info.bcd == 0x0300);
    CHECK(info.manufacturer == "Access Music");
    CHECK(info.product_name == "Virus TI?");
    fb.on_control = [](const Bytes &) { return Bytes(); }; // nothing readable
    CHECK(!cc::read_device_info(b, info, err) && err == "device descriptor not readable");
    b.close();
}

TEST(cc_backup_ram)
{
    TempHome home;
    FakeBroker fb;
    fb.on_sysex = fake_virus;
    Broker b;
    b.set_endpoint(fb.path);
    std::string err;
    CHECK(b.open(err));
    const std::string out = (home.dir / "backup").string();
    int fetched = 0, calls = 0;
    auto progress = [&](int bank, int, bool f, const Dump &d) {
        calls++;
        fetched += f;
        CHECK(bank == 1 && d.size() == 524);
    };
    CHECK(cc::backup_ram(b, "19", out, progress, err)); // bank 9 does not exist: skipped
    CHECK_EQ(calls, 128);
    CHECK_EQ(fetched, 128);
    auto libs = load_libraries(out);
    CHECK(libs.size() == 1 && libs[0].name == "RAM A" && libs[0].dumps.size() == 128);
    CHECK(libs.size() == 1 && cc::patch_name(libs[0].dumps[7]) == "Fake Patch");
    // the cache (below the temporary HOME) makes the next backup fetch nothing
    CHECK(std::filesystem::exists(cc::cache_dir() + "/RAM A"));
    fetched = calls = 0;
    CHECK(cc::backup_ram(b, "1", out, progress, err));
    CHECK_EQ(fetched, 0);
    std::atomic<bool> stop{true};
    CHECK(!cc::backup_ram(b, "1", out, nullptr, err, &stop) && err == "cancelled");
    // an output folder that cannot be created
    FILE *f = fopen((home.dir / "file").string().c_str(), "wb");
    fclose(f);
    CHECK(!cc::backup_ram(b, "1", (home.dir / "file" / "sub").string(), nullptr, err) &&
          err.find("cannot create") != std::string::npos);
    CHECK(cc::default_backup_dir().find("Backups") != std::string::npos);
    b.close();
}
#else
TEST(broker_fake_server)
{
    SKIP("the fake broker uses Unix sockets");
}
#endif
