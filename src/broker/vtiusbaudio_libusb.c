// vtiusbaudio_libusb.c - Virus TI USB audio streaming on libusb (Linux), the port of src/wrapper/vtiusbaudio.c
// (WinUSB, E075) for the Linux broker (findings/design-linux.md). The broker owns the whole device: the audio function
// (interface 0 control, 1 = OUT EP 0x01, 2 = IN EP 0x82) is taken from snd-usb-audio and streamed here:
//   - IN: 6 ch x 16 bit, one packet per 1 ms frame (44 or 45 frames at 44.1 kHz, the synth's clock decides),
//   - OUT (adaptive): 2 ch x 16 bit, every IN packet's frame count mirrored ("frame pattern", the driver's
//     PGFramePatternObserver), so the synth stays the clock master and the frame counter is exactly the number of
//     frames the synth produced.
// libusb has no frame-number scheduling (Linux usbfs uses ISO_ASAP): NR transfers stay queued, and a stall longer
// than that is detected by time and filled with silence of the nominal length, so the counter never slips (E078).
#include "vtiusbaudio_libusb.h"
#include <libusb.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define EP_OUT 0x01
#define EP_IN 0x82
#define IN_CH 6
#define OUT_CH 2
#define IN_PKT 592  // wMaxPacketSize EP 0x82
#define OUT_PKT 200 // wMaxPacketSize EP 0x01
#define NP 10       // packets (ms) per transfer
#define NR 8        // IN transfers in flight (80 ms)
#define NW 16       // OUT transfers
#define OUT_LEAD 12 // OUT packets queued before the first IN packet

extern void (*vti_log)(const char *fmt, ...);

static struct {
    libusb_context *ctx;
    libusb_device_handle *h;
    vla_in_fn in;
    vla_out_fn out;
    void *user;
    volatile int running, in_flight, out_flight;
    struct libusb_transfer *rt[NR], *wt[NW];
    int wfree[NW];
    pthread_mutex_t mx;
    pthread_t ev;
    unsigned pattern;      // nominal 44/45 pattern position
    double t0_ms;          // monotonic time of the first IN completion
    uint64_t delivered_ms; // USB frames (ms) delivered so far (packets + filled gaps)
    uint64_t frames, errs, gaps, hist[3], next_log;
    int16_t pcm[(NP + 5000) * 45 * IN_CH]; // one transfer, or a filled gap of up to 5 s
} S;

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static void out_cb(struct libusb_transfer *t)
{
    pthread_mutex_lock(&S.mx);
    for (int i = 0; i < NW; i++)
        if (S.wt[i] == t)
            S.wfree[i] = 1;
    S.out_flight--;
    pthread_mutex_unlock(&S.mx);
}

// Queue one OUT transfer carrying `n` packets of the given frame counts (audio from the out source, or silence).
static void send_out(const unsigned *counts, int n)
{
    pthread_mutex_lock(&S.mx);
    int s = -1;
    for (int i = 0; i < NW && s < 0; i++)
        if (S.wfree[i])
            s = i;
    if (s < 0) { // all busy (should not happen: they complete every ms)
        pthread_mutex_unlock(&S.mx);
        return;
    }
    S.wfree[s] = 0;
    S.out_flight++;
    pthread_mutex_unlock(&S.mx);
    struct libusb_transfer *t = S.wt[s];
    t->num_iso_packets = n;
    int off = 0;
    for (int k = 0; k < n; k++) {
        unsigned len = counts[k] * OUT_CH * 2;
        t->iso_packet_desc[k].length = len;
        if (S.out)
            S.out((int16_t *)(t->buffer + off), counts[k], S.user);
        else
            memset(t->buffer + off, 0, len);
        off += (int)len;
    }
    t->length = off;
    if (libusb_submit_transfer(t)) {
        pthread_mutex_lock(&S.mx);
        S.wfree[s] = 1;
        S.out_flight--;
        pthread_mutex_unlock(&S.mx);
    }
}

static uint32_t fill_silence(uint64_t packets) // nominal pattern, delivered in chunks
{
    uint32_t total = 0;
    while (packets) {
        uint64_t k = packets < NP ? packets : NP;
        uint32_t frames = 0;
        unsigned counts[NP];
        for (uint64_t j = 0; j < k; j++)
            frames += counts[j] = S.pattern++ % 10 ? 44 : 45;
        memset(S.pcm, 0, frames * IN_CH * 2);
        S.in(S.pcm, frames, S.user);
        send_out(counts, (int)k);
        total += frames;
        packets -= k;
    }
    return total;
}

static void in_cb(struct libusb_transfer *t)
{
    if (!S.running || t->status == LIBUSB_TRANSFER_CANCELLED || t->status == LIBUSB_TRANSFER_NO_DEVICE) {
        pthread_mutex_lock(&S.mx);
        S.in_flight--;
        pthread_mutex_unlock(&S.mx);
        if (t->status == LIBUSB_TRANSFER_NO_DEVICE)
            S.running = 0;
        return;
    }
    double now = now_ms();
    if (!S.t0_ms)
        S.t0_ms = now - NP; // this transfer covered the last NP ms
    // stall: more ms passed than were delivered (transfers ran dry and the kernel restarted ASAP) -> fill silence
    double behind = (now - S.t0_ms) - (double)(S.delivered_ms + NP);
    if (behind > 3 * NP) {
        uint64_t g = (uint64_t)behind;
        if (g > 5000)
            g = 5000;
        S.gaps += g;
        if (S.gaps <= 2000)
            vti_log("usbaudio: stream stalled, %llu ms filled with silence", (unsigned long long)g);
        S.frames += fill_silence(g);
        S.delivered_ms += g;
    }
    uint32_t frames = 0;
    unsigned counts[NP];
    for (int k = 0; k < t->num_iso_packets; k++) {
        struct libusb_iso_packet_descriptor *p = &t->iso_packet_desc[k];
        uint32_t f = p->status == LIBUSB_TRANSFER_COMPLETED ? p->actual_length / (IN_CH * 2) : 0;
        unsigned nominal = S.pattern++ % 10 ? 44 : 45;
        if (f < 43 || f > 46) { // lost packet, or still 48 kHz right after SET_CUR: nominal silence ("repair")
            if (p->status != LIBUSB_TRANSFER_COMPLETED)
                S.errs++;
            f = nominal;
            memset(S.pcm + frames * IN_CH, 0, f * IN_CH * 2);
        } else {
            memcpy(S.pcm + frames * IN_CH, libusb_get_iso_packet_buffer_simple(t, (unsigned)k), f * IN_CH * 2);
            if (f <= 45)
                S.hist[f - 43]++;
        }
        counts[k] = f;
        frames += f;
    }
    send_out(counts, t->num_iso_packets); // mirror the synth's frame counts towards it
    S.in(S.pcm, frames, S.user);
    S.frames += frames;
    S.delivered_ms += (uint64_t)t->num_iso_packets;
    if (S.frames >= S.next_log) {
        vti_log("usbaudio: %llu frames, packets 43/44/45: %llu/%llu/%llu, errors %llu, stalled %llu ms",
                (unsigned long long)S.frames, (unsigned long long)S.hist[0], (unsigned long long)S.hist[1],
                (unsigned long long)S.hist[2], (unsigned long long)S.errs, (unsigned long long)S.gaps);
        S.next_log += 44100ull * 60;
    }
    if (libusb_submit_transfer(t)) {
        vti_log("usbaudio: IN resubmit failed");
        pthread_mutex_lock(&S.mx);
        S.in_flight--;
        pthread_mutex_unlock(&S.mx);
    }
}

static void *event_thread(void *unused)
{
    (void)unused;
    struct sched_param sp = {.sched_priority = 10};
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp); // best effort (needs rtprio; fine without)
    while (S.running || S.in_flight > 0 || S.out_flight > 0) {
        struct timeval tv = {0, 100000};
        libusb_handle_events_timeout_completed(S.ctx, &tv, NULL);
    }
    return NULL;
}

// UAC1 SET_CUR SAMPLING_FREQ_CONTROL on an isochronous endpoint (class request to the endpoint)
static int set_rate(uint8_t ep, uint32_t rate)
{
    uint8_t d[3] = {(uint8_t)rate, (uint8_t)(rate >> 8), (uint8_t)(rate >> 16)};
    int r = libusb_control_transfer(S.h, 0x22, 0x01, 0x0100, ep, d, 3, 1000);
    if (r != 3)
        vti_log("usbaudio: SET_CUR %u Hz on EP 0x%02x: %s", rate, ep, r < 0 ? libusb_error_name(r) : "short");
    return r == 3 ? 0 : -1;
}

int vla_start(libusb_context *ctx, libusb_device_handle *h, vla_in_fn in, vla_out_fn out, void *user)
{
    if (S.running)
        return 0;
    memset(&S, 0, sizeof S);
    pthread_mutex_init(&S.mx, NULL);
    S.ctx = ctx, S.h = h, S.in = in, S.out = out, S.user = user;
    S.next_log = 44100ull * 60;
    for (int i = 0; i <= 2; i++) { // the audio function: taken from snd-usb-audio (auto-detach is on)
        int r = libusb_claim_interface(h, i);
        if (r) {
            vti_log("usbaudio: claim interface %d: %s", i, libusb_error_name(r));
            for (int k = 0; k < i; k++)
                libusb_release_interface(h, k);
            return -1;
        }
    }
    if (libusb_set_interface_alt_setting(h, 1, 1) || libusb_set_interface_alt_setting(h, 2, 1)) {
        vti_log("usbaudio: alternate setting 1 failed");
        vla_stop();
        return -1;
    }
    set_rate(EP_OUT, VLA_RATE);
    set_rate(EP_IN, VLA_RATE);
    S.running = 1;
    for (int i = 0; i < NW; i++) {
        S.wt[i] = libusb_alloc_transfer(NP);
        libusb_fill_iso_transfer(S.wt[i], h, EP_OUT, malloc(NP * OUT_PKT), NP * OUT_PKT, NP, out_cb, NULL, 1000);
        S.wfree[i] = 1;
    }
    pthread_create(&S.ev, NULL, event_thread, NULL);
    unsigned lead[NP];
    for (int done = 0; done < OUT_LEAD;) { // OUT lead-in with the nominal 44.1 kHz pattern, then mirror
        int n = OUT_LEAD - done < NP ? OUT_LEAD - done : NP;
        for (int k = 0; k < n; k++)
            lead[k] = S.pattern++ % 10 ? 44 : 45;
        send_out(lead, n);
        done += n;
    }
    for (int i = 0; i < NR; i++) {
        S.rt[i] = libusb_alloc_transfer(NP);
        libusb_fill_iso_transfer(S.rt[i], h, EP_IN, malloc(NP * IN_PKT), NP * IN_PKT, NP, in_cb, NULL, 1000);
        libusb_set_iso_packet_lengths(S.rt[i], IN_PKT);
        pthread_mutex_lock(&S.mx);
        S.in_flight++;
        pthread_mutex_unlock(&S.mx);
        if (libusb_submit_transfer(S.rt[i])) {
            vti_log("usbaudio: IN submit failed");
            pthread_mutex_lock(&S.mx);
            S.in_flight--;
            pthread_mutex_unlock(&S.mx);
        }
    }
    vti_log("usbaudio: streaming 44.1 kHz on libusb (IN EP 0x82 %d ch, OUT EP 0x01 %d ch, frame pattern mirrored)",
            IN_CH, OUT_CH);
    return 0;
}

void vla_stop(void)
{
    if (!S.h)
        return;
    if (S.running) {
        S.running = 0;
        for (int i = 0; i < NR; i++)
            if (S.rt[i])
                libusb_cancel_transfer(S.rt[i]);
        for (int i = 0; i < NW; i++)
            if (S.wt[i] && !S.wfree[i])
                libusb_cancel_transfer(S.wt[i]);
        pthread_join(S.ev, NULL);
    }
    for (int i = 0; i < NR; i++)
        if (S.rt[i]) {
            free(S.rt[i]->buffer);
            libusb_free_transfer(S.rt[i]);
        }
    for (int i = 0; i < NW; i++)
        if (S.wt[i]) {
            free(S.wt[i]->buffer);
            libusb_free_transfer(S.wt[i]);
        }
    libusb_set_interface_alt_setting(S.h, 1, 0);
    libusb_set_interface_alt_setting(S.h, 2, 0);
    for (int i = 0; i <= 2; i++)
        libusb_release_interface(S.h, i);
    vti_log("usbaudio: stopped after %llu frames (errors %llu, stalled %llu ms)", (unsigned long long)S.frames,
            (unsigned long long)S.errs, (unsigned long long)S.gaps);
    S.h = NULL;
}

int vla_running(void) { return S.running; }
