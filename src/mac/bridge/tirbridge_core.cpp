// tirbridge_core.cpp - see tirbridge_core.h.
#include "tirbridge_core.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <os/os_sync_wait_on_address.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "pluginterfaces/vst/ivsteditcontroller.h"

void blog(const char *fmt, ...)
{
    static std::mutex mx;
    std::lock_guard<std::mutex> g(mx);
    FILE *f = fopen("/tmp/tirevive-bridge.log", "a");
    if (!f)
        return;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    fprintf(f, "%ld.%03ld pid %d ", (long)ts.tv_sec % 100000, ts.tv_nsec / 1000000, getpid());
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

// ------------------------------------------------------------------------------- helper process
static std::string bundle_resources()
{
    Dl_info di{};
    dladdr((void *)&bundle_resources, &di); // .../X.vst3|X.component/Contents/MacOS/<binary>
    std::string p = di.dli_fname ? di.dli_fname : "";
    size_t k = p.rfind("/MacOS/");
    return k == std::string::npos ? "" : p.substr(0, k) + "/Resources";
}

static int connect_sock(const char *path)
{
    struct sockaddr_un sa = {};
    sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof sa.sun_path, "%s", path);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) == 0)
        return fd;
    close(fd);
    return -1;
}

extern char **environ;

// connect to this DAW's helper, starting it if needed (spawned by us so the DAW's Microphone permission covers it)
static int helper_connect()
{
    static std::mutex mx;
    std::lock_guard<std::mutex> g(mx);
    char sock[104];
    snprintf(sock, sizeof sock, TIRB_SOCK_FMT, (unsigned)getuid(), (int)getpid());
    int fd = connect_sock(sock);
    if (fd >= 0)
        return fd;
    std::string helper = bundle_resources() + "/vtihelper";
    char pid[16];
    snprintf(pid, sizeof pid, "%d", (int)getpid());
    const char *argv[] = {helper.c_str(), "--serve", sock, pid, nullptr};
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 2, "/tmp/tirevive-helper.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawnattr_t at; // no inherited descriptors from the DAW (open files, sockets, pipes)
    posix_spawnattr_init(&at);
    posix_spawnattr_setflags(&at, POSIX_SPAWN_CLOEXEC_DEFAULT);
    pid_t child;
    int e = posix_spawn(&child, helper.c_str(), &fa, &at, (char *const *)argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&at);
    if (e) {
        blog("posix_spawn %s: %s", helper.c_str(), strerror(e));
        return -1;
    }
    blog("helper started (pid %d): %s", (int)child, helper.c_str());
    pthread_t t; // reap the helper when it exits (no zombie in the DAW)
    pthread_create(&t, nullptr, [](void *p) -> void * {
        int st;
        waitpid((pid_t)(intptr_t)p, &st, 0);
        return nullptr;
    }, (void *)(intptr_t)child);
    pthread_detach(t);
    for (int i = 0; i < 300; i++) { // up to 30 s: Rosetta translation + loading the original on first start
        usleep(100000);
        if ((fd = connect_sock(sock)) >= 0)
            return fd;
    }
    blog("helper did not come up");
    return -1;
}

// ------------------------------------------------------------------------------- control connection
static int io_all(int fd, void *p, size_t n, bool wr)
{
    for (uint8_t *b = (uint8_t *)p; n;) {
        ssize_t r = wr ? send(fd, b, n, 0) : recv(fd, b, n, 0);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return -1;
        b += r;
        n -= (size_t)r;
    }
    return 0;
}

bool Conn::start()
{
    if ((fd = helper_connect()) < 0)
        return false;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
    pthread_create(&reader, nullptr, [](void *a) -> void * {
        ((Conn *)a)->read_loop();
        return nullptr;
    }, this);
    return true;
}

void Conn::read_loop()
{
    for (;;) {
        tirb_msg h;
        if (io_all(fd, &h, sizeof h, false))
            break;
        std::vector<uint8_t> b(h.len);
        if (h.len && io_all(fd, b.data(), h.len, false))
            break;
        if (h.type & TIRB_REPLY) {
            std::lock_guard<std::mutex> g(mx);
            replies[h.seq] = std::move(b);
            cv.notify_all();
        } else if (on_notify)
            on_notify(h, b);
    }
    blog("helper connection closed");
    std::lock_guard<std::mutex> g(mx);
    fd = -1;
    cv.notify_all();
}

bool Conn::post(uint32_t type, uint32_t inst, const void *p, uint32_t len, uint32_t s)
{
    tirb_msg h{type, inst, s, len};
    std::lock_guard<std::mutex> g(wr);
    return fd >= 0 && !io_all(fd, &h, sizeof h, true) && (!len || !io_all(fd, (void *)p, len, true));
}

bool Conn::call(uint32_t type, uint32_t inst, const void *p, uint32_t len, std::vector<uint8_t> *out, int timeout_s)
{
    uint32_t s;
    {
        std::lock_guard<std::mutex> g(mx);
        s = ++seq;
    }
    if (!post(type, inst, p, len, s))
        return false;
    std::unique_lock<std::mutex> l(mx);
    if (!cv.wait_for(l, std::chrono::seconds(timeout_s), [&] { return replies.count(s) || fd < 0; }) ||
        !replies.count(s))
        return false;
    if (out)
        *out = std::move(replies[s]);
    replies.erase(s);
    return true;
}

void Conn::stop()
{
    if (fd >= 0)
        shutdown(fd, SHUT_RDWR);
    if (reader)
        pthread_join(reader, nullptr);
    if (fd >= 0)
        close(fd);
    fd = -1;
}

// ------------------------------------------------------------------------------- instance
bool BridgeCore::init()
{
    conn.on_notify = [this](const tirb_msg &h, const std::vector<uint8_t> &b) { notification(h, b); };
    if (!conn.start())
        return false;
    uint32_t hello[2] = {TIRB_VERSION, (uint32_t)getpid()};
    std::vector<uint8_t> r;
    if (!conn.call(TIRB_HELLO, 0, hello, sizeof hello, &r) || r.size() < 4)
        return false;
    if (!conn.call(TIRB_CREATE, 0, nullptr, 0, &r, 60) || r.size() < sizeof layout) {
        blog("CREATE failed");
        return false;
    }
    memcpy(&layout, r.data(), sizeof layout);
    inst = (uint32_t)atoi(strrchr(layout.shm_name, '-') + 1); // "/tirb-<pid>-<instance>"
    int fd = shm_open(layout.shm_name, O_RDWR, 0600);
    if (fd < 0)
        return false;
    void *m = mmap(nullptr, sizeof(tirb_shm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (m == MAP_FAILED)
        return false;
    shm = (tirb_shm *)m;
    seq_ = shm->req_seq;
    for (uint32_t first = 0;;) { // parameters, in chunks
        uint32_t q[2] = {first, 512};
        if (!conn.call(TIRB_PARAM_INFO, inst, q, sizeof q, &r) || r.size() < 4)
            break;
        uint32_t total;
        memcpy(&total, r.data(), 4);
        size_t n = (r.size() - 4) / sizeof(tirb_param);
        const tirb_param *p = (const tirb_param *)(r.data() + 4);
        params.insert(params.end(), p, p + n);
        first += (uint32_t)n;
        if (!n || first >= total)
            break;
    }
    values = std::vector<std::atomic<double>>(params.size());
    for (size_t i = 0; i < params.size(); i++) {
        param_index[params[i].id] = (int32_t)i;
        values[i] = params[i].default_value;
    }
    if (conn.call(TIRB_MIDI_MAPPING, inst, nullptr, 0, &r) && r.size() >= 4) {
        uint32_t n;
        memcpy(&n, r.data(), 4);
        const tirb_midimap *mm = (const tirb_midimap *)(r.data() + 4);
        for (uint32_t i = 0; i < n && 4 + (i + 1) * sizeof(tirb_midimap) <= r.size(); i++)
            midimap[(uint32_t)mm[i].channel << 16 | (uint16_t)mm[i].controller] = mm[i].id;
    }
    ok = true;
    blog("instance %u: %u outputs, %zu parameters, %zu MIDI mappings", inst, layout.num_outputs, params.size(),
         midimap.size());
    return true;
}

void BridgeCore::term()
{
    if (ok)
        conn.call(TIRB_DESTROY, inst, nullptr, 0, nullptr, 10);
    conn.stop();
    if (shm)
        munmap(shm, sizeof(tirb_shm));
    shm = nullptr;
    ok = false;
    blog("instance %u terminated (%lu late blocks)", inst, misses_);
}

void BridgeCore::set_active(bool on)
{
    uint32_t v = on ? 1 : 0;
    std::vector<uint8_t> r;
    conn.call(TIRB_ACTIVE, inst, &v, 4, &r);
}

void BridgeCore::setup(double sample_rate, int32_t block, int32_t process_mode)
{
    rate = sample_rate;
    max_block = std::min<int32_t>(block, TIRB_MAX_BLOCK);
    tirb_setup su{sample_rate, max_block, process_mode};
    std::vector<uint8_t> r;
    conn.call(TIRB_SETUP, inst, &su, sizeof su, &r);
}

bool BridgeCore::get_state(std::vector<uint8_t> &out) { return conn.call(TIRB_GET_STATE, inst, nullptr, 0, &out, 60); }

bool BridgeCore::set_state(const void *data, size_t len)
{
    std::vector<uint8_t> r;
    if (!conn.call(TIRB_SET_STATE, inst, data, (uint32_t)len, &r, 60))
        return false;
    refresh_values();
    return true;
}

bool BridgeCore::run(uint32_t frames)
{
    tirb_shm *s = shm;
    if (!s || !ok)
        return false;
    s->frames = frames;
    // hand the block to the helper and wait for it, at most about one block duration (never stall the DAW)
    uint32_t want = ++seq_;
    __atomic_store_n(&s->req_seq, want, __ATOMIC_RELEASE);
    os_sync_wake_by_address_any(&s->req_seq, 4, OS_SYNC_WAKE_BY_ADDRESS_SHARED);
    uint64_t budget_ns = (uint64_t)(std::max<uint32_t>(frames, 64) / rate * 1e9) + 2000000;
    uint64_t until = clock_gettime_nsec_np(CLOCK_UPTIME_RAW) + budget_ns;
    uint32_t done;
    while ((done = __atomic_load_n(&s->done_seq, __ATOMIC_ACQUIRE)) != want) {
        uint64_t now = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        if (now >= until)
            break;
        os_sync_wait_on_address_with_timeout(&s->done_seq, done, 4, OS_SYNC_WAIT_ON_ADDRESS_SHARED,
                                             OS_CLOCK_MACH_ABSOLUTE_TIME, until - now);
    }
    if (done != want) {
        if (!(misses_++ % 100))
            blog("instance %u: block %u late (%lu so far)", inst, want, misses_);
        return false;
    }
    return true;
}

void BridgeCore::note_output_level(const float *const *chans, int nch, uint32_t frames)
{
    for (int c = 0; c < nch; c++)
        if (chans[c])
            for (uint32_t k = 0; k < frames; k++)
                diag_peak_ = std::max(diag_peak_, std::fabs(chans[c][k]));
    if ((diag_frames_ += frames) >= rate * 5) {
        blog("instance %u: output peak %.1f dBFS (5 s)", inst, diag_peak_ > 0 ? 20 * std::log10(diag_peak_) : -999.0);
        diag_frames_ = 0, diag_peak_ = 0;
    }
}

int32_t BridgeCore::index_of(uint32_t id) const
{
    auto it = param_index.find(id);
    return it == param_index.end() ? -1 : it->second;
}

void BridgeCore::set_value(uint32_t id, double v)
{
    int32_t i = index_of(id);
    if (i >= 0)
        values[i] = v;
}

double BridgeCore::value(uint32_t id) const
{
    int32_t i = index_of(id);
    return i < 0 ? 0 : values[i].load();
}

void BridgeCore::set_param_from_host(uint32_t id, double v)
{
    set_value(id, v);
    uint8_t q[12];
    memcpy(q, &id, 4);
    memcpy(q + 4, &v, 8);
    conn.post(TIRB_SET_PARAM, inst, q, sizeof q);
}

bool BridgeCore::param_string(uint32_t id, double v, std::string &out)
{
    uint8_t q[12];
    memcpy(q, &id, 4);
    memcpy(q + 4, &v, 8);
    std::vector<uint8_t> r;
    if (!conn.call(TIRB_PARAM_STRING, inst, q, sizeof q, &r, 5) || r.empty() || !r[0])
        return false;
    r.push_back(0);
    out = (const char *)r.data();
    return true;
}

void BridgeCore::refresh_values() // after a state load: pull the current values (once per preset load)
{
    for (size_t i = 0; i < params.size(); i++) {
        if (params[i].flags & Steinberg::Vst::ParameterInfo::kIsHidden)
            continue;
        std::vector<uint8_t> r;
        if (conn.call(TIRB_GET_PARAM, inst, &params[i].id, 4, &r, 5) && r.size() >= 8) {
            double v;
            memcpy(&v, r.data(), 8);
            values[i] = v;
        }
    }
}

// ------------------------------------------------------------------------------- editor link
bool BridgeCore::editor_open(double x, double y, double scale, tirb_editor_reply &rep)
{
    double q[3] = {x, y, scale};
    std::vector<uint8_t> r;
    if (!conn.call(TIRB_EDITOR_OPEN, inst, q, sizeof q, &r, 30) || r.size() < sizeof rep) {
        blog("instance %u: EDITOR_OPEN failed", inst);
        return false;
    }
    memcpy(&rep, r.data(), sizeof rep);
    blog("instance %u: editor %dx%d %s", inst, rep.width, rep.height, rep.pixels_shm);
    return rep.width > 0 && rep.pixels_shm[0];
}

void BridgeCore::editor_close() { conn.call(TIRB_EDITOR_CLOSE, inst, nullptr, 0, nullptr, 10); }

void BridgeCore::editor_move(double x, double y)
{
    double q[2] = {x, y};
    conn.post(TIRB_EDITOR_MOVE, inst, q, sizeof q);
}

void BridgeCore::editor_input(const tirb_input &in) { conn.post(TIRB_EDITOR_EVENT, inst, &in, sizeof in); }

void BridgeCore::editor_menu_result(const tirb_menu_result &r) { conn.post(TIRB_MENU_RESULT, inst, &r, sizeof r); }

// ------------------------------------------------------------------------------- notifications
void BridgeCore::notification(const tirb_msg &h, const std::vector<uint8_t> &b)
{
    if (h.type == TIRB_N_PARAM_EDIT && b.size() >= sizeof(tirb_param_edit)) {
        tirb_param_edit e;
        memcpy(&e, b.data(), sizeof e);
        if (e.what == 1)
            set_value(e.id, e.value);
        hold();
        auto *ctx = new std::pair<BridgeCore *, tirb_param_edit>(this, e);
        dispatch_async_f(dispatch_get_main_queue(), ctx, [](void *p) { // the DAW expects edits on its UI thread
            auto *c = (std::pair<BridgeCore *, tirb_param_edit> *)p;
            c->first->on_param_edit(c->second);
            c->first->unhold();
            delete c;
        });
    } else if (h.type == TIRB_N_EDITOR_RESIZE && b.size() >= sizeof(tirb_editor_reply)) {
        tirb_editor_reply rep;
        memcpy(&rep, b.data(), sizeof rep);
        if (view)
            bridge_view_resized(view, rep);
    } else if (h.type == TIRB_N_MENU) {
        if (view)
            bridge_view_menu(view, b.data(), b.size());
        else { // no view (closed meanwhile): cancel so the original does not wait
            tirb_menu_result r{};
            if (b.size() >= sizeof(tirb_menu_hdr))
                r.menu_id = ((const tirb_menu_hdr *)b.data())->menu_id;
            conn.post(TIRB_MENU_RESULT, inst, &r, sizeof r);
        }
    } else if (h.type == TIRB_N_KEY_FOCUS && b.size() >= 4) {
        int32_t on;
        memcpy(&on, b.data(), 4);
        if (view)
            bridge_view_key_focus(view, on != 0);
    } else if (h.type == TIRB_N_RESTART && b.size() >= 4) {
        int32_t flags;
        memcpy(&flags, b.data(), 4);
        hold();
        auto *ctx = new std::pair<BridgeCore *, int32_t>(this, flags);
        dispatch_async_f(dispatch_get_main_queue(), ctx, [](void *p) {
            auto *c = (std::pair<BridgeCore *, int32_t> *)p;
            c->first->on_restart(c->second);
            c->first->unhold();
            delete c;
        });
    }
}
