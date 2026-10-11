// vtiasiohost.c - minimal ASIO host to test an ASIO driver without a DAW: loads it by CLSID, prints its name,
// channels, latencies, buffer sizes and sample rate, creates buffers for every channel, runs N seconds and reports
// buffer switches per second, sample position progress and the input peak per channel.
// usage: vtiasiohost [seconds] [{CLSID}|-] [tone]   (default driver: Virus TI (TI Revive); tone: 440 Hz on the
// outputs at -12 dBFS instead of silence)
// The IASIO interface is declared here from the ASIO ABI (COM-style vtable, x64 calling convention).
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

typedef long ASIOBool;
typedef long ASIOError;
typedef long ASIOSampleType;
typedef struct { unsigned long hi, lo; } ASIOSamples, ASIOTimeStamp;
typedef struct { long index, associatedChannel, associatedGroup; ASIOBool isCurrentSource; char name[32]; } ASIOClockSource;
typedef struct { long channel; ASIOBool isInput, isActive; long channelGroup; ASIOSampleType type; char name[32]; } ASIOChannelInfo;
typedef struct { ASIOBool isInput; long channelNum; void *buffers[2]; } ASIOBufferInfo;
typedef struct {
    void (*bufferSwitch)(long index, ASIOBool directProcess);
    void (*sampleRateDidChange)(double rate);
    long (*asioMessage)(long selector, long value, void *message, double *opt);
    void *(*bufferSwitchTimeInfo)(void *params, long index, ASIOBool directProcess);
} ASIOCallbacks;

typedef struct IASIO IASIO;
typedef struct {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IASIO *, REFIID, void **);
    ULONG(STDMETHODCALLTYPE *AddRef)(IASIO *);
    ULONG(STDMETHODCALLTYPE *Release)(IASIO *);
    ASIOBool (*init)(IASIO *, void *sysHandle);
    void (*getDriverName)(IASIO *, char *name);
    long (*getDriverVersion)(IASIO *);
    void (*getErrorMessage)(IASIO *, char *string);
    ASIOError (*start)(IASIO *);
    ASIOError (*stop)(IASIO *);
    ASIOError (*getChannels)(IASIO *, long *in, long *out);
    ASIOError (*getLatencies)(IASIO *, long *in, long *out);
    ASIOError (*getBufferSize)(IASIO *, long *min, long *max, long *pref, long *gran);
    ASIOError (*canSampleRate)(IASIO *, double rate);
    ASIOError (*getSampleRate)(IASIO *, double *rate);
    ASIOError (*setSampleRate)(IASIO *, double rate);
    ASIOError (*getClockSources)(IASIO *, ASIOClockSource *clocks, long *num);
    ASIOError (*setClockSource)(IASIO *, long ref);
    ASIOError (*getSamplePosition)(IASIO *, ASIOSamples *pos, ASIOTimeStamp *ts);
    ASIOError (*getChannelInfo)(IASIO *, ASIOChannelInfo *info);
    ASIOError (*createBuffers)(IASIO *, ASIOBufferInfo *infos, long num, long size, ASIOCallbacks *cb);
    ASIOError (*disposeBuffers)(IASIO *);
    ASIOError (*controlPanel)(IASIO *);
    ASIOError (*future)(IASIO *, long selector, void *opt);
    ASIOError (*outputReady)(IASIO *);
} IASIOVtbl;
struct IASIO { const IASIOVtbl *v; };

#define MAXCH 32
static ASIOBufferInfo g_bi[MAXCH];
static ASIOChannelInfo g_ci[MAXCH];
static long g_nbuf, g_size;
static volatile LONG g_switches;
static double g_peak[MAXCH];
static int g_tone;
static double g_phase;

static double sample_abs(ASIOSampleType t, const void *buf, long i)
{
    switch (t) {
    case 16: return abs(((const short *)buf)[i]) / 32768.0;          // Int16LSB
    case 18: return fabs(((const int *)buf)[i] / 2147483648.0);      // Int32LSB
    case 19: return fabs(((const float *)buf)[i]);                   // Float32LSB
    case 17: { const unsigned char *b = (const unsigned char *)buf + 3 * i; // Int24LSB
        int v = b[0] | b[1] << 8 | (signed char)b[2] << 16; return abs(v) / 8388608.0; }
    default: return 0;
    }
}

static void cb_switch(long index, ASIOBool direct)
{
    (void)direct;
    InterlockedIncrement(&g_switches);
    static long last_index = -1;
    if (last_index >= 0 && g_tone)
        g_phase = fmod(g_phase + 2 * 3.14159265358979 * 440.0 * g_size / 44100.0, 2 * 3.14159265358979);
    last_index = index;
    for (long c = 0; c < g_nbuf; c++) {
        if (!g_bi[c].isInput) {
            memset(g_bi[c].buffers[index], 0, (size_t)g_size * 4); // silence (4 bytes covers every type)
            if (g_tone && g_ci[c].type == 18) // Int32LSB
                for (long i = 0; i < g_size; i++)
                    ((int *)g_bi[c].buffers[index])[i] =
                        (int)(0.25 * 2147483647.0 * sin(g_phase + 2 * 3.14159265358979 * 440.0 * i / 44100.0));
            continue;
        }
        for (long i = 0; i < g_size; i++) {
            double a = sample_abs(g_ci[c].type, g_bi[c].buffers[index], i);
            if (a > g_peak[c])
                g_peak[c] = a;
        }
    }
}
static void cb_rate(double r) { printf("  sampleRateDidChange %.0f\n", r); }
static long cb_msg(long sel, long val, void *m, double *o)
{
    (void)m;
    (void)o;
    printf("  asioMessage %ld %ld\n", sel, val);
    return sel == 1 && (val == 2 || val == 3) ? 1 : 0; // supported: kAsioResetRequest, kAsioBufferSizeChange?
}
static void *cb_ti(void *p, long index, ASIOBool d) { cb_switch(index, d); return p; }

int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 5;
    CLSID clsid = {0x6fd63640, 0x1e0a, 0x4ee0, {0x91, 0x58, 0x73, 0x2a, 0x10, 0x77, 0xc8, 0xb5}};
    for (int i = 2; i < argc; i++)
        g_tone |= !strcmp(argv[i], "tone");
    if (argc > 2 && argv[2][0] == '{') {
        wchar_t w[64];
        MultiByteToWideChar(CP_ACP, 0, argv[2], -1, w, 64);
        CLSIDFromString(w, &clsid);
    }
    CoInitialize(NULL);
    IASIO *a = NULL;
    HRESULT hr = CoCreateInstance(&clsid, NULL, CLSCTX_INPROC_SERVER, &clsid, (void **)&a); // ASIO: riid = clsid
    printf("CoCreateInstance -> 0x%08lx\n", (unsigned long)hr);
    if (FAILED(hr) || !a)
        return 1;
    ASIOBool ok = a->v->init(a, GetConsoleWindow());
    char name[128] = "", err[128] = "";
    a->v->getDriverName(a, name);
    a->v->getErrorMessage(a, err);
    printf("init -> %ld, driver \"%s\" version %ld, message \"%s\"\n", ok, name, a->v->getDriverVersion(a), err);
    if (!ok)
        return 1;
    long in = 0, out = 0, li = 0, lo = 0, mn = 0, mx = 0, pref = 0, gran = 0;
    double rate = 0;
    a->v->getChannels(a, &in, &out);
    a->v->getBufferSize(a, &mn, &mx, &pref, &gran);
    a->v->getSampleRate(a, &rate);
    printf("channels in %ld out %ld; buffer min %ld max %ld preferred %ld granularity %ld; rate %.0f (44100 ok: %ld)\n",
           in, out, mn, mx, pref, gran, rate, a->v->canSampleRate(a, 44100.0));
    for (long i = 0; i < in + out && g_nbuf < MAXCH; i++, g_nbuf++) {
        g_bi[g_nbuf].isInput = i < in;
        g_bi[g_nbuf].channelNum = i < in ? i : i - in;
        g_ci[g_nbuf].channel = g_bi[g_nbuf].channelNum;
        g_ci[g_nbuf].isInput = g_bi[g_nbuf].isInput;
        a->v->getChannelInfo(a, &g_ci[g_nbuf]);
        printf("  %s %ld \"%s\" type %ld group %ld\n", g_ci[g_nbuf].isInput ? "in " : "out", g_ci[g_nbuf].channel,
               g_ci[g_nbuf].name, g_ci[g_nbuf].type, g_ci[g_nbuf].channelGroup);
    }
    g_size = pref;
    static ASIOCallbacks cb = {cb_switch, cb_rate, cb_msg, cb_ti};
    ASIOError e = a->v->createBuffers(a, g_bi, g_nbuf, g_size, &cb);
    a->v->getLatencies(a, &li, &lo);
    printf("createBuffers(%ld ch, %ld frames) -> %ld; latency in %ld out %ld frames\n", g_nbuf, g_size, e, li, lo);
    if (e)
        return 1;
    e = a->v->start(a);
    printf("start -> %ld\n", e);
    ASIOSamples p0, p1;
    ASIOTimeStamp t0, t1;
    Sleep(500);
    a->v->getSamplePosition(a, &p0, &t0);
    LONG s0 = g_switches;
    DWORD c0 = GetTickCount();
    for (int s = 1; s <= secs; s++) {
        Sleep(1000);
        LONG sw = g_switches;
        a->v->getSamplePosition(a, &p1, &t1);
        double pos = ((double)p1.hi * 4294967296.0 + p1.lo) - ((double)p0.hi * 4294967296.0 + p0.lo);
        double el = (GetTickCount() - c0) / 1000.0;
        printf("%2d s: %.1f switches/s (= %.0f frames/s), position +%.0f (%.0f/s), peak in:", s, (sw - s0) / el,
               (sw - s0) / el * g_size, pos, pos / el);
        for (long c = 0; c < g_nbuf; c++)
            if (g_bi[c].isInput) {
                printf(" %.3f", g_peak[c]);
                g_peak[c] = 0;
            }
        printf("\n");
        fflush(stdout);
    }
    printf("stop -> %ld, disposeBuffers -> %ld\n", a->v->stop(a), a->v->disposeBuffers(a));
    a->v->Release(a);
    CoUninitialize();
    return 0;
}
