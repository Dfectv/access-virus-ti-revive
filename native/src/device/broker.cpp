// broker.cpp - see broker.h (POSIX implementation; Windows pipe backend follows in R6).
#include "broker.h"
extern "C" {
#include "vtiproto.h"
}
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace vti {

Broker::~Broker() { close(); }

static bool io_all(int fd, void *p, size_t n, bool wr)
{
    for (auto *b = (uint8_t *)p; n;) {
        ssize_t r = wr ? ::send(fd, b, n, MSG_NOSIGNAL) : ::recv(fd, b, n, 0);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return false;
        b += r;
        n -= (size_t)r;
    }
    return true;
}

bool Broker::open(std::string &error)
{
    sockaddr_un sa{};
    sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof sa.sun_path, "/tmp/vtibroker-%u.sock", (unsigned)getuid());
    fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ < 0 || ::connect(fd_, (sockaddr *)&sa, sizeof sa)) {
        error = std::string("cannot connect to the TI Revive broker at ") + sa.sun_path + ": " + strerror(errno);
        if (fd_ >= 0)
            ::close(fd_);
        fd_ = -1;
        return false;
    }
    uint32_t ver = VTI_PROTO_VERSION;
    send(VTIMSG_HELLO, &ver, 4);
    stop_ = false;
    reader_ = std::thread([this] { read_loop(); });
    for (int i = 0; i < 100 && !present_; i++) // STATE arrives right after connecting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return true;
}

void Broker::close()
{
    stop_ = true;
    if (fd_ >= 0) {
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
        fd_ = -1;
    }
    if (reader_.joinable())
        reader_.join();
}

bool Broker::send(uint16_t type, const void *a, size_t alen, const void *b, size_t blen)
{
    if (fd_ < 0 || alen + blen > VTIMSG_MAX)
        return false;
    vtimsg h{type, (uint16_t)(alen + blen)};
    std::lock_guard<std::mutex> g(wr_);
    return io_all(fd_, &h, sizeof h, true) && (!alen || io_all(fd_, (void *)a, alen, true)) &&
           (!blen || io_all(fd_, (void *)b, blen, true));
}

bool Broker::write(uint8_t ep, const uint8_t *data, size_t len)
{
    for (size_t off = 0; off < len;) { // keep messages below VTIMSG_MAX
        size_t n = std::min<size_t>(len - off, 1024);
        if (!send(VTIMSG_WRITE, &ep, 1, data + off, n))
            return false;
        off += n;
    }
    return true;
}

int Broker::control(uint8_t rt, uint8_t req, uint16_t value, uint16_t index, uint8_t *data, uint16_t len,
                    int timeout_ms)
{
    uint8_t hdr[12];
    uint32_t seq;
    {
        std::lock_guard<std::mutex> g(ctl_);
        seq = ++seq_;
    }
    memcpy(hdr, &seq, 4);
    hdr[4] = rt, hdr[5] = req, hdr[6] = value & 0xff, hdr[7] = value >> 8, hdr[8] = index & 0xff,
    hdr[9] = index >> 8, hdr[10] = len & 0xff, hdr[11] = len >> 8;
    if (!send(VTIMSG_CONTROL, hdr, 12, (rt & 0x80) ? nullptr : data, (rt & 0x80) ? 0 : len))
        return -1;
    std::unique_lock<std::mutex> l(ctl_);
    if (!ctl_cv_.wait_for(l, std::chrono::milliseconds(timeout_ms), [&] { return replies_.count(seq) > 0; }))
        return -1;
    auto rep = std::move(replies_[seq]);
    replies_.erase(seq);
    int32_t r = -1;
    if (rep.size() >= 4)
        memcpy(&r, rep.data(), 4);
    if (r > 0 && (rt & 0x80))
        memcpy(data, rep.data() + 4, std::min<size_t>((size_t)r, rep.size() - 4));
    return r;
}

int Broker::audio(bool on, int timeout_ms)
{
    {
        std::lock_guard<std::mutex> g(ctl_);
        audio_replied_ = false;
    }
    uint32_t v = on ? 1 : 0;
    if (!send(VTIMSG_AUDIO, &v, 4, "", 1)) // u32 on + event name (unused on POSIX)
        return -1;
    std::unique_lock<std::mutex> l(ctl_);
    if (!ctl_cv_.wait_for(l, std::chrono::milliseconds(timeout_ms), [&] { return audio_replied_; }))
        return -1;
    return audio_reply_;
}

void Broker::read_loop()
{
    std::vector<uint8_t> buf(VTIMSG_MAX);
    while (!stop_) {
        vtimsg h;
        if (!io_all(fd_, &h, sizeof h, false) || h.len > VTIMSG_MAX || !io_all(fd_, buf.data(), h.len, false))
            break;
        switch (h.type) {
        case VTIMSG_RX:
            if (h.len >= 1 && rx_)
                rx_(buf[0], buf.data() + 1, h.len - 1u);
            break;
        case VTIMSG_STATE:
            present_ = h.len >= 4 && buf[0];
            if (state_)
                state_(present_);
            break;
        case VTIMSG_CONTROL_REPLY:
            if (h.len >= 8) {
                uint32_t seq;
                memcpy(&seq, buf.data(), 4);
                std::lock_guard<std::mutex> g(ctl_);
                replies_[seq].assign(buf.begin() + 4, buf.begin() + h.len);
                ctl_cv_.notify_all();
            }
            break;
        case VTIMSG_AUDIO_REPLY:
            if (h.len >= 4) {
                std::lock_guard<std::mutex> g(ctl_);
                memcpy(&audio_reply_, buf.data(), 4);
                audio_replied_ = true;
                ctl_cv_.notify_all();
            }
            break;
        default:
            break;
        }
    }
    present_ = false;
}

} // namespace vti
