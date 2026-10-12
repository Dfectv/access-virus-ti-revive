// vtiaudiotest_posix.c - Linux test client of the broker audio (protocol 3): connects to vtibroker, starts the
// audio stream, follows VTIMSG_AUDIO_TICK and reports the frame rate and per-channel peaks; writes the 6 input
// channels to a WAV file, with -sine plays a 440 Hz tone into the output ring (towards the Virus), with -note plays
// a C4 every second (USB-MIDI on EP 0x06, cable 2, as the plug-in does, E056).
//
// usage: vtiaudiotest [-sine] [-note] [seconds] [out.wav]
#include "vtiaudioshm.h"
#include "vtiproto.h"
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static int rd(int fd, void *p, size_t n)
{
    for (uint8_t *b = p; n;) {
        ssize_t r = recv(fd, b, n, 0);
        if (r <= 0)
            return -1;
        b += r;
        n -= (size_t)r;
    }
    return 0;
}

static int msg(int fd, uint16_t type, const void *p, uint16_t len)
{
    vtimsg h = {type, len};
    return send(fd, &h, sizeof h, 0) != sizeof h || (len && send(fd, p, len, 0) != len);
}

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void wav_header(FILE *f, uint32_t frames)
{
    uint32_t data = frames * VTI_AUDIO_IN_CH * 2, v32;
    uint16_t v16;
    fwrite("RIFF", 1, 4, f), v32 = 36 + data, fwrite(&v32, 4, 1, f), fwrite("WAVEfmt ", 1, 8, f);
    v32 = 16, fwrite(&v32, 4, 1, f), v16 = 1, fwrite(&v16, 2, 1, f), v16 = VTI_AUDIO_IN_CH, fwrite(&v16, 2, 1, f);
    v32 = 44100, fwrite(&v32, 4, 1, f), v32 = 44100 * VTI_AUDIO_IN_CH * 2, fwrite(&v32, 4, 1, f);
    v16 = VTI_AUDIO_IN_CH * 2, fwrite(&v16, 2, 1, f), v16 = 16, fwrite(&v16, 2, 1, f);
    fwrite("data", 1, 4, f), fwrite(&data, 4, 1, f);
}

int main(int argc, char **argv)
{
    int sine = 0, note = 0, secs = 10, sec_no = -1;
    const char *wav = NULL;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-sine"))
            sine = 1;
        else if (!strcmp(argv[i], "-note"))
            note = 1;
        else if (atoi(argv[i]) > 0)
            secs = atoi(argv[i]);
        else
            wav = argv[i];
    struct sockaddr_un sa = {.sun_family = AF_UNIX};
    snprintf(sa.sun_path, sizeof sa.sun_path, "/tmp/vtibroker-%u.sock", (unsigned)getuid());
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa)) {
        perror("connect (is vtibroker running?)");
        return 1;
    }
    uint32_t v = VTI_PROTO_VERSION;
    msg(fd, VTIMSG_HELLO, &v, 4);
    uint8_t buf[VTIMSG_MAX];
    vtimsg h;
    for (;;) { // wait for the device
        if (rd(fd, &h, sizeof h) || rd(fd, buf, h.len))
            return 1;
        if (h.type == VTIMSG_STATE) {
            printf("state %u\n", *(uint32_t *)buf);
            if (*(uint32_t *)buf)
                break;
        }
    }
    uint32_t on = 1;
    msg(fd, VTIMSG_AUDIO, &on, 4);
    int32_t res = -2;
    while (res == -2) {
        if (rd(fd, &h, sizeof h) || rd(fd, buf, h.len))
            return 1;
        if (h.type == VTIMSG_AUDIO_REPLY)
            res = *(int32_t *)buf;
    }
    printf("audio reply %d\n", res);
    if (res != 1)
        return 1;
    char name[64];
    snprintf(name, sizeof name, VTI_AUDIO_SHM_LINUX, (unsigned)getuid());
    int sfd = shm_open(name, O_RDWR, 0);
    vti_audio_shm *s = mmap(NULL, sizeof *s, PROT_READ | PROT_WRITE, MAP_SHARED, sfd, 0);
    if (sfd < 0 || s == MAP_FAILED) {
        perror(name);
        return 1;
    }
    printf("shm: magic %08x version %u rate %u in %u out %u ring %u\n", s->magic, s->version, s->rate, s->in_ch,
           s->out_ch, s->ring_frames);
    FILE *wf = wav ? fopen(wav, "wb") : NULL;
    if (wf)
        wav_header(wf, 0);
    int64_t pos = -1, first = 0, out_w = s->out_write, ticks = 0;
    uint32_t wav_frames = 0;
    double t0 = 0, t_last = now(), phase = 0;
    int peak[VTI_AUDIO_IN_CH] = {0};
    while (now() - t_last < secs + 1) {
        if (rd(fd, &h, sizeof h) || rd(fd, buf, h.len))
            break;
        if (h.type != VTIMSG_AUDIO_TICK)
            continue;
        int64_t f = *(int64_t *)buf;
        ticks++;
        static double tl, gmax;
        static int big;
        double tn = now();
        if (tl) {
            double g = (tn - tl) * 1000;
            gmax = g > gmax ? g : gmax;
            big += g > 15;
        }
        tl = tn;
        if (pos < 0) {
            pos = first = f;
            t0 = now();
            t_last = t0;
            continue;
        }
        if (f - pos > VTI_AUDIO_RING) {
            printf("overrun: %lld frames lost\n", (long long)(f - pos - VTI_AUDIO_RING));
            pos = f - VTI_AUDIO_RING / 2;
        }
        for (; pos < f; pos++) {
            int16_t *fr = &s->in[(pos & (VTI_AUDIO_RING - 1)) * VTI_AUDIO_IN_CH];
            for (int c = 0; c < VTI_AUDIO_IN_CH; c++)
                if (abs(fr[c]) > peak[c])
                    peak[c] = abs(fr[c]);
            if (wf)
                fwrite(fr, 2, VTI_AUDIO_IN_CH, wf), wav_frames++;
        }
        if (sine) // keep ~1024 frames ahead of the synth's clock
            for (; out_w < s->out_read + 1024 || out_w < f - first + 1024; out_w++) {
                int16_t x = (int16_t)(8000 * sin(phase));
                phase += 2 * M_PI * 440 / 44100;
                s->out[(out_w & (VTI_AUDIO_RING - 1)) * 2] = s->out[(out_w & (VTI_AUDIO_RING - 1)) * 2 + 1] = x;
                __atomic_store_n(&s->out_write, out_w + 1, __ATOMIC_RELEASE);
            }
        double t = now();
        if (note) { // note on in the first half of every second, off in the second half
            int want = t - t0 - (int)(t - t0) < 0.5;
            if (want != sec_no) {
                uint8_t cab = (uint8_t)(getenv("VTI_CABLE") ? atoi(getenv("VTI_CABLE")) : 2) << 4;
                uint8_t m[5] = {0x06, (uint8_t)(cab | (want ? 9 : 8)), want ? 0x90 : 0x80, 60, want ? 100 : 0};
                msg(fd, VTIMSG_WRITE, m, 5);
                sec_no = want;
            }
        }
        if (t - t_last >= 1) {
            printf("%5.1f s: %lld frames, %.1f Hz, ticks %lld, peaks", t - t0, (long long)(f - first),
                   (f - first) / (t - t0), (long long)ticks);
            for (int c = 0; c < VTI_AUDIO_IN_CH; c++)
                printf(" %d", peak[c]), peak[c] = 0;
            printf(", out read %lld, tick max gap %.1f ms, %d > 15 ms\n", (long long)s->out_read, gmax, big);
            gmax = 0, big = 0;
            fflush(stdout);
            t_last = t;
            if (t - t0 >= secs)
                break;
        }
    }
    if (wf) {
        rewind(wf);
        wav_header(wf, wav_frames);
        fclose(wf);
        printf("wrote %u frames to %s\n", wav_frames, wav);
    }
    on = 0;
    msg(fd, VTIMSG_AUDIO, &on, 4);
    close(fd);
    return 0;
}
