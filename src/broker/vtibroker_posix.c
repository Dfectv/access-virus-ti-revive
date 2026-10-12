// vtibroker_posix.c - the broker for macOS (and Linux): owns the Virus TI's vendor interfaces MI_03/MI_04 over libusb
// (vticore.c) and serves clients over a Unix domain socket with the same messages as on Windows (vtiproto.h).
// Started on demand (LaunchAgent / first client) and exits when no client has been connected for IDLE_EXIT_S
// seconds, like vtibroker.exe since 0.3.1 (E080). On macOS audio is not streamed here: the Virus' class-compliant
// audio function stays with Apple's USB audio driver (Core Audio), see findings/design-macos.md. On Linux the broker
// owns the whole device like on Windows (snd-usb-audio cannot share it, E113): VTIMSG_AUDIO streams the audio
// function on libusb (vtiusbaudio_libusb.c) into POSIX shared memory (vtiaudioshm.h), with VTIMSG_AUDIO_TICK to
// the audio client per block, see findings/design-linux.md.
//
// usage: vtibroker [-v] [-stay]     socket: /tmp/vtibroker-<uid>.sock (VTI_SOCK_FMT)
#include "vticore.h"
#include "vtiproto.h"
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include "vtimidi_mac.h"
#include <launch.h>
#endif
#ifdef __linux__
#include "vtiaudioshm.h"
#include "vtiusbaudio_libusb.h"
#include <fcntl.h>
#include <sys/mman.h>
#endif

#define VTI_SOCK_FMT "/tmp/vtibroker-%u.sock"
#ifdef MSG_NOSIGNAL
#define MSG_NOSIGNAL_OR_ZERO MSG_NOSIGNAL
#else
#define MSG_NOSIGNAL_OR_ZERO 0 // macOS: SIGPIPE is ignored in main()
#endif
#define MAX_CLIENTS 16
#define IDLE_EXIT_S 30

typedef struct {
    int fd;
    pthread_mutex_t wr; // one message at a time per client
    int audio;          // Linux: wants VTIMSG_AUDIO_TICK
} client;

static client *g_clients[MAX_CLIENTS];
static pthread_mutex_t g_cs_clients = PTHREAD_MUTEX_INITIALIZER, g_cs_dev = PTHREAD_MUTEX_INITIALIZER,
                       g_cs_log = PTHREAD_MUTEX_INITIALIZER;
static vti *g_dev;
static int g_verbose, g_stay;

static void blog(const char *fmt, ...)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);
    pthread_mutex_lock(&g_cs_log);
    fprintf(stderr, "%02d:%02d:%02d.%03ld ", tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    pthread_mutex_unlock(&g_cs_log);
}

static void on_vti_log(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    buf[strcspn(buf, "\n")] = 0;
    blog("%s", buf);
}

static int write_all(int fd, const void *p, size_t n)
{
    for (const uint8_t *b = p; n;) {
        ssize_t w = send(fd, b, n, MSG_NOSIGNAL_OR_ZERO);
        if (w < 0 && errno == EINTR)
            continue;
        if (w <= 0)
            return -1;
        b += w;
        n -= (size_t)w;
    }
    return 0;
}

static int read_all(int fd, void *p, size_t n)
{
    for (uint8_t *b = p; n;) {
        ssize_t r = recv(fd, b, n, 0);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return -1;
        b += r;
        n -= (size_t)r;
    }
    return 0;
}

static int send_msg(client *c, uint16_t type, const void *a, int alen, const void *b, int blen)
{
    vtimsg h = {type, (uint16_t)(alen + blen)};
    pthread_mutex_lock(&c->wr);
    int r = write_all(c->fd, &h, sizeof h) || (alen && write_all(c->fd, a, (size_t)alen)) ||
            (blen && write_all(c->fd, b, (size_t)blen));
    pthread_mutex_unlock(&c->wr);
    return r;
}

// One program at a time owns the Virus, as with the Access driver: the first connected client; the others are told
// "no device" until the owner disconnects, then the next client takes over. A second program (another DAW, auval,
// a test host) would otherwise initialise the Virus and restart its audio stream in the middle of the first one's
// session, which left the Virus without USB audio until it was power-cycled (E112).
static client *g_owner; // g_cs_clients

static int is_owner(client *c)
{
    pthread_mutex_lock(&g_cs_clients);
    int r = c == g_owner;
    pthread_mutex_unlock(&g_cs_clients);
    return r;
}

static void send_states(int dev_open) // g_cs_clients held
{
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i]) {
            uint32_t st = dev_open && g_clients[i] == g_owner;
            send_msg(g_clients[i], VTIMSG_STATE, &st, 4, NULL, 0);
        }
}

static void on_rx(void *user, uint8_t ep, const uint8_t *data, int len)
{
    (void)user;
#ifdef __APPLE__
    if (ep == VTI_EP_MI04_IN)
        vtimidi_rx(data, len);
#endif
    pthread_mutex_lock(&g_cs_clients);
    if (g_owner)
        send_msg(g_owner, VTIMSG_RX, &ep, 1, data, len);
    pthread_mutex_unlock(&g_cs_clients);
}

#ifdef __linux__
// ---- Linux: the Virus' audio function, streamed by the broker (port of vtibroker.c's audio ring) ----
static vti_audio_shm *g_shm;
static pthread_mutex_t g_cs_tick = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv_tick = PTHREAD_COND_INITIALIZER;
static int64_t g_tick_sent;

static int64_t mono_100ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (int64_t)ts.tv_sec * 10000000 + ts.tv_nsec / 100;
}

static int shm_create(void)
{
    if (g_shm)
        return 0;
    char name[64];
    snprintf(name, sizeof name, VTI_AUDIO_SHM_LINUX, (unsigned)getuid());
    int fd = shm_open(name, O_RDWR | O_CREAT, 0600);
    if (fd < 0 || ftruncate(fd, sizeof(vti_audio_shm))) {
        blog("audio: shared memory %s: %s", name, strerror(errno));
        if (fd >= 0)
            close(fd);
        return -1;
    }
    g_shm = mmap(NULL, sizeof(vti_audio_shm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (g_shm == MAP_FAILED) {
        g_shm = NULL;
        return -1;
    }
    memset(g_shm, 0, sizeof *g_shm);
    g_shm->magic = VTI_AUDIO_MAGIC;
    g_shm->version = VTI_AUDIO_VERSION;
    g_shm->rate = VLA_RATE;
    g_shm->in_ch = VTI_AUDIO_IN_CH;
    g_shm->out_ch = VTI_AUDIO_OUT_CH;
    g_shm->ring_frames = VTI_AUDIO_RING;
    return 0;
}

static void audio_in(const int16_t *pcm, uint32_t frames, void *user) // libusb event thread
{
    (void)user;
    int64_t w = g_shm->in_frames;
    for (uint32_t i = 0; i < frames; i++)
        memcpy(&g_shm->in[((w + i) & (VTI_AUDIO_RING - 1)) * VTI_AUDIO_IN_CH], pcm + i * VTI_AUDIO_IN_CH,
               VTI_AUDIO_IN_CH * sizeof(int16_t));
    g_shm->in_qpc = mono_100ns();
    __atomic_store_n(&g_shm->in_frames, w + frames, __ATOMIC_RELEASE); // publishes the data written above
    pthread_mutex_lock(&g_cs_tick);
    pthread_cond_signal(&g_cv_tick);
    pthread_mutex_unlock(&g_cs_tick);
}

// OUT packet source: as vtibroker.c - playback starts with OUT_PREFILL frames buffered, latency bounded by OUT_MAX
#define OUT_PREFILL 512
#define OUT_MAX 2048
static int g_out_playing;
static uint64_t g_out_underruns, g_out_dropped, g_out_frames;

static void audio_out(int16_t *dst, uint32_t frames, void *user)
{
    (void)user;
    int64_t w = __atomic_load_n(&g_shm->out_write, __ATOMIC_ACQUIRE), r = g_shm->out_read;
    int64_t avail = w - r;
    if (avail < 0 || avail > VTI_AUDIO_RING) { // writer restarted or far ahead: resynchronise
        r = w;
        avail = 0;
        g_out_playing = 0;
    }
    if (!g_out_playing && avail >= OUT_PREFILL)
        g_out_playing = 1;
    if (g_out_playing && avail > OUT_MAX) {
        g_out_dropped += (uint64_t)(avail - OUT_PREFILL);
        r = w - OUT_PREFILL;
        avail = OUT_PREFILL;
    }
    uint32_t n = 0;
    if (g_out_playing) {
        n = avail < frames ? (uint32_t)avail : frames;
        for (uint32_t i = 0; i < n; i++)
            memcpy(dst + i * VTI_AUDIO_OUT_CH, &g_shm->out[((r + i) & (VTI_AUDIO_RING - 1)) * VTI_AUDIO_OUT_CH],
                   VTI_AUDIO_OUT_CH * sizeof(int16_t));
        if (n < frames) { // ran dry: refill before playing again
            g_out_underruns++;
            g_out_playing = 0;
        }
    }
    memset(dst + n * VTI_AUDIO_OUT_CH, 0, (frames - n) * VTI_AUDIO_OUT_CH * sizeof(int16_t));
    g_shm->out_read = r + n;
    g_out_frames += n;
}

// Block notifications go out from here, not from the USB thread, so a slow client never delays the stream.
static void *tick_thread(void *unused)
{
    (void)unused;
    for (;;) {
        pthread_mutex_lock(&g_cs_tick);
        while (!g_shm || __atomic_load_n(&g_shm->in_frames, __ATOMIC_ACQUIRE) == g_tick_sent)
            pthread_cond_wait(&g_cv_tick, &g_cs_tick);
        int64_t f = g_tick_sent = g_shm->in_frames;
        pthread_mutex_unlock(&g_cs_tick);
        pthread_mutex_lock(&g_cs_clients);
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (g_clients[i] && g_clients[i]->audio)
                send_msg(g_clients[i], VTIMSG_AUDIO_TICK, &f, 8, NULL, 0);
        pthread_mutex_unlock(&g_cs_clients);
    }
    return NULL;
}

static int audio_start(void) // g_cs_dev held, device open
{
    if (vla_running())
        return 1;
    if (shm_create())
        return -1;
    struct libusb_context *ctx;
    struct libusb_device_handle *h;
    vti_libusb(g_dev, &ctx, &h);
    g_shm->in_frames = 0;
    g_tick_sent = 0;
    g_shm->out_read = g_shm->out_write;
    g_out_playing = 0;
    g_out_frames = g_out_underruns = g_out_dropped = 0;
    if (vla_start(ctx, h, audio_in, audio_out, NULL))
        return -1;
    g_shm->streaming = 1;
    return 1;
}

static void audio_stop(void) // g_cs_dev held
{
    if (!g_shm || !g_shm->streaming)
        return;
    vla_stop();
    g_shm->streaming = 0;
    if (g_out_frames)
        blog("audio out: %llu frames played, %llu underruns, %llu frames dropped", (unsigned long long)g_out_frames,
             (unsigned long long)g_out_underruns, (unsigned long long)g_out_dropped);
}
#else
static void audio_stop(void) {}
#endif

#ifdef __APPLE__
static int midi_write(const uint8_t *data, int len) // pump thread: plug-in channel packets from the MDW ring
{
    pthread_mutex_lock(&g_cs_dev);
    int r = g_dev ? vti_write(g_dev, VTI_EP_MI04_OUT, data, len) : -1;
    pthread_mutex_unlock(&g_cs_dev);
    return r;
}
#endif

static int client_count(void)
{
    int n = 0;
    pthread_mutex_lock(&g_cs_clients);
    for (int i = 0; i < MAX_CLIENTS; i++)
        n += g_clients[i] != NULL;
    pthread_mutex_unlock(&g_cs_clients);
    return n;
}

static void *device_thread(void *unused)
{
    (void)unused;
    time_t last_busy = time(NULL);
    for (;;) {
        if (client_count())
            last_busy = time(NULL);
        else if (!g_stay && time(NULL) - last_busy > IDLE_EXIT_S) {
            blog("no clients for %d s: exiting", IDLE_EXIT_S);
            pthread_mutex_lock(&g_cs_dev);
            audio_stop();
            if (g_dev)
                vti_close(g_dev);
            exit(0);
        }
        pthread_mutex_lock(&g_cs_dev);
        if (g_dev && vti_is_dead(g_dev)) {
            blog("device failed: closing");
            audio_stop();
            vti_close(g_dev);
            g_dev = NULL;
            pthread_mutex_lock(&g_cs_clients);
            send_states(0);
            pthread_mutex_unlock(&g_cs_clients);
        }
        if (!g_dev && (g_dev = vti_open(on_rx, NULL))) {
            blog("device open, handshake sent");
            pthread_mutex_lock(&g_cs_clients);
            send_states(1);
            pthread_mutex_unlock(&g_cs_clients);
        }
        int open = g_dev != NULL;
        pthread_mutex_unlock(&g_cs_dev);
        usleep(open ? 500000 : 2000000);
    }
    return NULL;
}

// SIGINT / SIGTERM (Ctrl+C, logout, systemd): close the device properly, so on Linux it is handed back to
// snd-usb-audio (vti_close resets the port) instead of staying claimed until it is replugged.
static void *signal_thread(void *arg)
{
    sigset_t *set = arg;
    int sig = 0;
    sigwait(set, &sig);
    blog("signal %d: exiting", sig);
    pthread_mutex_lock(&g_cs_dev);
    audio_stop();
    if (g_dev)
        vti_close(g_dev);
    exit(0);
    return NULL;
}

static void *client_thread(void *arg)
{
    client *c = arg;
    uint8_t buf[VTIMSG_MAX];
    pthread_mutex_lock(&g_cs_dev);
    pthread_mutex_lock(&g_cs_clients);
    if (!g_owner)
        g_owner = c;
    uint32_t st = g_dev != NULL && c == g_owner;
    blog("client connected%s", c == g_owner ? " (owner)" : " (waiting: another program uses the Virus)");
    pthread_mutex_unlock(&g_cs_clients);
    pthread_mutex_unlock(&g_cs_dev);
    send_msg(c, VTIMSG_STATE, &st, 4, NULL, 0);
    for (;;) {
        vtimsg h;
        if (read_all(c->fd, &h, sizeof h) || h.len > VTIMSG_MAX || read_all(c->fd, buf, h.len))
            break;
        switch (h.type) {
        case VTIMSG_HELLO:
            blog("client hello, protocol %u", h.len >= 4 ? *(uint32_t *)buf : 0);
            break;
        case VTIMSG_WRITE:
            if (h.len >= 1 && is_owner(c)) {
                pthread_mutex_lock(&g_cs_dev);
                int r = g_dev ? vti_write(g_dev, buf[0], buf + 1, h.len - 1) : -1;
                pthread_mutex_unlock(&g_cs_dev);
                if (g_verbose || r)
                    blog("write ep%02x %d bytes -> %d", buf[0], h.len - 1, r);
            }
            break;
        case VTIMSG_CONTROL:
            if (h.len >= 12) {
                uint32_t seq;
                memcpy(&seq, buf, 4);
                uint8_t *s = buf + 4, data[VTIMSG_MAX];
                uint16_t wl = (uint16_t)(s[6] | s[7] << 8);
                if (wl > VTIMSG_MAX - 16)
                    wl = VTIMSG_MAX - 16;
                if (!(s[0] & 0x80))
                    memcpy(data, buf + 12, h.len - 12 < wl ? h.len - 12 : wl);
                int owner = is_owner(c);
                pthread_mutex_lock(&g_cs_dev);
                int32_t r = g_dev && owner ? vti_control(g_dev, s[0], s[1], (uint16_t)(s[2] | s[3] << 8),
                                                (uint16_t)(s[4] | s[5] << 8), data, wl)
                                  : -1;
                pthread_mutex_unlock(&g_cs_dev);
                uint8_t hdr[8];
                memcpy(hdr, &seq, 4);
                memcpy(hdr + 4, &r, 4);
                send_msg(c, VTIMSG_CONTROL_REPLY, hdr, 8, data, (s[0] & 0x80) && r > 0 ? r : 0);
            }
            break;
        case VTIMSG_AUDIO: { // macOS: audio comes from Core Audio, not from the broker (reply 0)
            int32_t r = 0;
#ifdef __linux__
            uint32_t on = h.len >= 4 ? *(uint32_t *)buf : 0;
            int owner = is_owner(c);
            pthread_mutex_lock(&g_cs_dev);
            if (on)
                r = g_dev && owner ? audio_start() : -1;
            else if (owner)
                audio_stop();
            pthread_mutex_unlock(&g_cs_dev);
            pthread_mutex_lock(&g_cs_clients);
            c->audio = on && r == 1;
            pthread_mutex_unlock(&g_cs_clients);
            blog("audio %s -> %d", on ? "on" : "off", r);
#endif
            send_msg(c, VTIMSG_AUDIO_REPLY, &r, 4, NULL, 0);
            break;
        }
        default:
            blog("client: unknown message %u", h.type);
        }
    }
    blog("client disconnected");
    pthread_mutex_lock(&g_cs_dev);
    pthread_mutex_lock(&g_cs_clients);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i] == c)
            g_clients[i] = NULL;
    if (g_owner == c) { // hand the Virus to the next waiting program
        g_owner = NULL;
        audio_stop();
        for (int i = 0; i < MAX_CLIENTS && !g_owner; i++)
            g_owner = g_clients[i];
        if (g_owner) {
            uint32_t st = g_dev != NULL;
            blog("next client becomes the owner");
            send_msg(g_owner, VTIMSG_STATE, &st, 4, NULL, 0);
        }
    }
    pthread_mutex_unlock(&g_cs_clients);
    pthread_mutex_unlock(&g_cs_dev);
    close(c->fd);
    pthread_mutex_destroy(&c->wr);
    free(c);
    return NULL;
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        g_verbose |= !strcmp(argv[i], "-v");
        g_stay |= !strcmp(argv[i], "-stay");
    }
    signal(SIGPIPE, SIG_IGN);
    static sigset_t sigs; // handled by signal_thread; blocked in every thread created after this
    sigemptyset(&sigs);
    sigaddset(&sigs, SIGINT);
    sigaddset(&sigs, SIGTERM);
    sigaddset(&sigs, SIGHUP);
    pthread_sigmask(SIG_BLOCK, &sigs, NULL);
    vti_log = on_vti_log;
    struct sockaddr_un sa = {.sun_family = AF_UNIX};
    snprintf(sa.sun_path, sizeof sa.sun_path, VTI_SOCK_FMT, (unsigned)getuid());
    int s = -1;
#ifdef __APPLE__
    // Started by launchd socket activation (LaunchAgent with a "Listeners" socket, scripts/mac-install-broker.sh):
    // launchd owns the socket and starts us on the first connect; after the idle exit it starts us again.
    int *fds = NULL;
    size_t nfds = 0;
    if (launch_activate_socket("Listeners", &fds, &nfds) == 0 && nfds > 0) {
        s = fds[0];
        free(fds);
        blog("socket from launchd");
    }
#endif
#ifdef __linux__
    // Started by systemd socket activation (tirevive-broker.socket, installed by the Linux installer): the socket
    // arrives as fd 3; systemd starts us again on the next connect after the idle exit.
    const char *lpid = getenv("LISTEN_PID"), *lfds = getenv("LISTEN_FDS");
    if (lpid && lfds && (pid_t)atol(lpid) == getpid() && atoi(lfds) >= 1) {
        s = 3;
        blog("socket from systemd");
    }
#endif
    if (s < 0) {
        s = socket(AF_UNIX, SOCK_STREAM, 0);
        // single instance: if a broker answers on the socket, leave; else remove a stale socket file
        if (connect(s, (struct sockaddr *)&sa, sizeof sa) == 0) {
            blog("another vtibroker is running, exiting");
            return 0;
        }
        close(s);
        unlink(sa.sun_path);
        s = socket(AF_UNIX, SOCK_STREAM, 0);
        if (bind(s, (struct sockaddr *)&sa, sizeof sa) || chmod(sa.sun_path, 0600) || listen(s, 8)) {
            blog("socket %s: %s", sa.sun_path, strerror(errno));
            return 1;
        }
    }
    blog("vtibroker starting (protocol %d, %s)", VTI_PROTO_VERSION, sa.sun_path);
#ifdef __APPLE__
    if (vtimidi_start(midi_write)) // stands in for Access' MIDI driver towards Virus Control (E100)
        blog("MIDI slot memory could not be created");
#endif
    pthread_t t;
    pthread_create(&t, NULL, signal_thread, &sigs);
    pthread_detach(t);
    pthread_create(&t, NULL, device_thread, NULL);
    pthread_detach(t);
#ifdef __linux__
    pthread_create(&t, NULL, tick_thread, NULL);
    pthread_detach(t);
#endif
    for (;;) {
        int fd = accept(s, NULL, NULL);
        if (fd < 0) {
            if (errno != EINTR)
                blog("accept: %s", strerror(errno));
            continue;
        }
        client *c = calloc(1, sizeof *c);
        c->fd = fd;
        pthread_mutex_init(&c->wr, NULL);
        int slot = -1;
        pthread_mutex_lock(&g_cs_clients);
        for (int i = 0; i < MAX_CLIENTS && slot < 0; i++)
            if (!g_clients[i]) {
                g_clients[i] = c;
                slot = i;
            }
        pthread_mutex_unlock(&g_cs_clients);
        if (slot < 0) {
            blog("too many clients");
            close(fd);
            pthread_mutex_destroy(&c->wr);
            free(c);
            continue;
        }
        pthread_create(&t, NULL, client_thread, c);
        pthread_detach(t);
    }
}
