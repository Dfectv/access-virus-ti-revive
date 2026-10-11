// vtiaudio.c - plugin-integrated audio for the IOCTL emulation (milestone M4, findings E051-E055, spec §11.3/11.4).
//
// With VirusUSB.sys the plugin gets the Virus audio from a driver-filled "AudioSM" region that the driver maps
// into the process (IOCTL 0x220010), and its audio thread is clocked by a buffer-switch event (0x2200E4,
// "threaded" variant). Without the Access driver the audio interfaces are driven by Windows' inbox USB audio
// class driver (E045), so here:
//   - 0x220010 returns a region we allocate with the driver's layout,
//   - a capture thread reads the "Virus TI" recording endpoint with WASAPI (shared mode, Windows converts to
//     44.1 kHz / 16-bit / stereo) and writes the frames as int16 << 16 into channels 0/1, advancing the counter,
//   - after every packet it consumes the plugin's output ring (+0x810BC = +0x81028) and signals the plugin's
//     buffer-switch event (the driver did this in its isochronous completion).
// If no Virus recording endpoint exists, a 5 ms timer provides the clock with silence.
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

void vtiemu_log(const char *fmt, ...); // vtiemu.c

#define SM_SAMPLES 0x20000
#define IN_REGION 0x810C0
#define IN_CHANNELS 6
#define RATE 44100

static uint8_t *g_in;               // input region B (spec §11.4)
static volatile uint8_t *g_out_sm;  // plugin's output AudioSM (0x220030 buffer)
static volatile HANDLE g_bs_event;  // plugin's buffer-switch event (0x2200E4, threaded)
static volatile LONG g_running;
static HANDLE g_thread;

#define S(p) ((p) + 8) // AudioSM struct inside the input region
#define U32(p, off) (*(volatile uint32_t *)((p) + (off)))

static void init_region(uint8_t *b)
{
    memset(b, 0, IN_REGION);
    *(uint64_t *)b = 0x001FFDD8; // magic, = IOCTL 0x22004C value (E055)
    uint8_t *s = S(b);
    U32(s, 0x80000) = IN_CHANNELS;
    U32(s, 0x80004) = SM_SAMPLES / IN_CHANNELS;
    U32(s, 0x81030) = RATE;
    U32(s, 0x810B0) = RATE / 4;
    U32(s, 0x810B4) = 1;
    // B+0x81020 owner = 0 and B+0x81028 exclusive = 0: every plugin instance outputs the audio
}

// Append n frames (stereo int16, or silence if pcm == NULL) and run the clock for the plugin.
static void push_frames(const int16_t *pcm, uint32_t n)
{
    uint8_t *s = S(g_in);
    uint32_t maxf = U32(s, 0x80004), wr = U32(s, 0x81028);
    int32_t *ring = (int32_t *)s;
    for (uint32_t i = 0; i < n; i++) {
        int32_t *f = ring + ((wr + i) % maxf) * IN_CHANNELS;
        f[0] = pcm ? (int32_t)pcm[2 * i] << 16 : 0;
        f[1] = pcm ? (int32_t)pcm[2 * i + 1] << 16 : 0;
        f[2] = f[3] = f[4] = f[5] = 0;
    }
    U32(s, 0x81028) = wr + n;
    volatile uint8_t *o = g_out_sm;
    if (o) // the device "consumed" whatever the plugin produced towards it
        U32(o, 0x810BC) = U32(o, 0x81028);
    HANDLE ev = g_bs_event;
    if (ev)
        SetEvent(ev);
}

// GUIDs spelled out: mingw headers declare but do not always define them.
static const CLSID CLSID_MMDevEnum = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
static const IID IID_MMDevEnum = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
static const IID IID_AudioClient = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
static const IID IID_AudioCapture = {0xC8ADBD64, 0xE71E, 0x48A0, {0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17}};

// First active capture endpoint whose friendly name contains "Virus TI".
static IMMDevice *find_virus_capture(void)
{
    IMMDeviceEnumerator *en = NULL;
    IMMDeviceCollection *col = NULL;
    IMMDevice *found = NULL;
    if (FAILED(CoCreateInstance(&CLSID_MMDevEnum, NULL, CLSCTX_ALL, &IID_MMDevEnum, (void **)&en)))
        return NULL;
    if (SUCCEEDED(en->lpVtbl->EnumAudioEndpoints(en, eCapture, DEVICE_STATE_ACTIVE, &col))) {
        UINT n = 0;
        col->lpVtbl->GetCount(col, &n);
        for (UINT i = 0; i < n && !found; i++) {
            IMMDevice *d = NULL;
            IPropertyStore *ps = NULL;
            if (FAILED(col->lpVtbl->Item(col, i, &d)))
                continue;
            if (SUCCEEDED(d->lpVtbl->OpenPropertyStore(d, STGM_READ, &ps))) {
                PROPVARIANT v;
                PropVariantInit(&v);
                if (SUCCEEDED(ps->lpVtbl->GetValue(ps, &PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR &&
                    wcsstr(v.pwszVal, L"Virus TI")) {
                    char name[256];
                    WideCharToMultiByte(CP_UTF8, 0, v.pwszVal, -1, name, sizeof name, NULL, NULL);
                    vtiemu_log("audio: capture endpoint \"%s\"", name);
                    found = d;
                    d = NULL;
                }
                PropVariantClear(&v);
                ps->lpVtbl->Release(ps);
            }
            if (d)
                d->lpVtbl->Release(d);
        }
        col->lpVtbl->Release(col);
    }
    en->lpVtbl->Release(en);
    return found;
}

static void silence_clock(void)
{
    int16_t zero[2 * 221] = {0};
    while (g_running) { // ~5 ms periods
        push_frames(zero, 221);
        Sleep(5);
    }
}

static DWORD WINAPI capture_thread(LPVOID unused)
{
    (void)unused;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    IMMDevice *dev = find_virus_capture();
    IAudioClient *ac = NULL;
    IAudioCaptureClient *cc = NULL;
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    WAVEFORMATEX fmt = {WAVE_FORMAT_PCM, 2, RATE, RATE * 4, 4, 16, 0};
    HRESULT hr = E_FAIL;
    if (dev && SUCCEEDED(hr = dev->lpVtbl->Activate(dev, &IID_AudioClient, CLSCTX_ALL, NULL, (void **)&ac)))
        hr = ac->lpVtbl->Initialize(ac, AUDCLNT_SHAREMODE_SHARED,
                                    AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                        AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                    100000 /* 10 ms */, 0, &fmt, NULL);
    if (SUCCEEDED(hr))
        hr = ac->lpVtbl->SetEventHandle(ac, ev);
    if (SUCCEEDED(hr))
        hr = ac->lpVtbl->GetService(ac, &IID_AudioCapture, (void **)&cc);
    if (SUCCEEDED(hr))
        hr = ac->lpVtbl->Start(ac);
    if (FAILED(hr)) {
        vtiemu_log("audio: no Virus TI capture stream (hr=0x%08lx, endpoint %s): silence clock", (unsigned long)hr,
                   dev ? "found" : "missing");
        silence_clock();
    } else {
        vtiemu_log("audio: capturing 44.1 kHz 16-bit stereo from the Virus TI endpoint");
        uint64_t total = 0;
        while (g_running) {
            if (WaitForSingleObject(ev, 200) != WAIT_OBJECT_0)
                continue;
            BYTE *data;
            UINT32 n;
            DWORD flags;
            while (SUCCEEDED(cc->lpVtbl->GetBuffer(cc, &data, &n, &flags, NULL, NULL)) && n) {
                push_frames((flags & AUDCLNT_BUFFERFLAGS_SILENT) ? NULL : (const int16_t *)data, n);
                total += n;
                cc->lpVtbl->ReleaseBuffer(cc, n);
            }
            if (total && total % (RATE * 30) < 512) // a sign of life every ~30 s
                vtiemu_log("audio: %llu frames captured", (unsigned long long)total);
        }
        ac->lpVtbl->Stop(ac);
    }
    if (cc)
        cc->lpVtbl->Release(cc);
    if (ac)
        ac->lpVtbl->Release(ac);
    if (dev)
        dev->lpVtbl->Release(dev);
    CloseHandle(ev);
    CoUninitialize();
    return 0;
}

static void ensure_running(void)
{
    if (!InterlockedExchange(&g_running, 1))
        g_thread = CreateThread(NULL, 0, capture_thread, NULL, 0, NULL);
}

// IOCTL 0x220010: map the input region (allocated once per process).
void *vtiaudio_input_region(void)
{
    if (!g_in) {
        g_in = VirtualAlloc(NULL, IN_REGION, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (g_in)
            init_region(g_in);
        vtiemu_log("audio: input region %p", (void *)g_in);
    }
    return g_in;
}

// IOCTL 0x220030 {u8 start, pad, void *buf}: the plugin's output AudioSM (towards the device).
void vtiaudio_output_ring(int start, void *buf)
{
    g_out_sm = start ? buf : NULL;
    vtiemu_log("audio: plugin stream %s (ring %p)", start ? "start" : "stop", buf);
    if (start && g_in)
        ensure_running();
}

// IOCTL 0x2200E4 {routine, thread, event}: the threaded buffer-switch registration (all zero = unregister).
void vtiaudio_buffer_switch(const uint64_t reg[3])
{
    if (!reg[0] && reg[1] && reg[2]) {
        g_bs_event = (HANDLE)(uintptr_t)reg[2];
        vtiemu_log("audio: buffer-switch event %p", g_bs_event);
    } else if (!reg[0] && !reg[1] && !reg[2]) {
        g_bs_event = NULL;
    } else {
        vtiemu_log("audio: APC buffer-switch registration (routine %p) not supported", (void *)(uintptr_t)reg[0]);
    }
}
