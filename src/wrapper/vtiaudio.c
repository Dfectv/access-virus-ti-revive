// vtiaudio.c - plugin-integrated audio for the IOCTL emulation (milestone M4, findings E051-E055, spec §11.3/11.4).
//
// With VirusUSB.sys the plugin gets the Virus audio from a driver-filled "AudioSM" region that the driver maps
// into the process (IOCTL 0x220010), and its audio thread is clocked by a buffer-switch event (0x2200E4,
// "threaded" variant). Without the Access driver the audio interfaces are driven by Windows' inbox USB audio
// class driver (E045), so here:
//   - 0x220010 returns a region we allocate with the driver's layout,
//   - a capture thread reads the "Virus TI" recording endpoint with WASAPI (shared mode, all endpoint channels,
//     Windows converts to 44.1 kHz / 16-bit) and writes them as int16 << 16 in device order, advancing the counter,
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

// Append n frames of `ch` interleaved int16 channels (or silence if pcm == NULL) in device channel order and
// run the clock for the plugin. The Virus sends 6 channels (USB1-3 L/R); the plugin's "USB1" is device channel
// pair 2/3 (E056), so all channels are copied 1:1 like the driver did.
static void push_frames(const int16_t *pcm, uint32_t ch, uint32_t n)
{
    uint8_t *s = S(g_in);
    uint32_t maxf = U32(s, 0x80004), wr = U32(s, 0x81028);
    int32_t *ring = (int32_t *)s;
    for (uint32_t i = 0; i < n; i++) {
        int32_t *f = ring + ((wr + i) % maxf) * IN_CHANNELS;
        for (uint32_t c = 0; c < IN_CHANNELS; c++)
            f[c] = pcm && c < ch ? (int32_t)pcm[ch * i + c] << 16 : 0;
    }
    U32(s, 0x81028) = wr + n;
    // Buffer switch like VirusUSB.sys (FUN_f1025370, E051): once per +0x810B8 frames of device time (the plugin's
    // buffer size: 64..2048), not once per capture packet; the first one stamps +0x810C8 with the QPC time
    // ("mTriggerTime", which the plugin's AudioSM code checks).
    volatile uint8_t *o = g_out_sm;
    HANDLE ev = g_bs_event;
    uint32_t thr = o ? U32(o, 0x810B8) : 0;
    if (!thr || thr > 0x8000) { // no output ring (yet): one switch per packet
        if (o)
            U32(o, 0x810BC) = U32(o, 0x81028);
        if (ev)
            SetEvent(ev);
        return;
    }
    static uint32_t since;
    for (since += n; since >= thr; since -= thr) {
        U32(o, 0x810BC) = U32(o, 0x81028); // the device "consumed" what the plugin produced towards it
        if (!*(volatile uint64_t *)(o + 0x810C8)) {
            LARGE_INTEGER q;
            QueryPerformanceCounter(&q);
            *(volatile uint64_t *)(o + 0x810C8) = (uint64_t)q.QuadPart;
        }
        if (ev)
            SetEvent(ev);
    }
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
    while (g_running) { // ~5 ms periods
        push_frames(NULL, 0, 221);
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
    // Same channel count and mask as the endpoint (so Windows does not remix), 16-bit PCM at 44.1 kHz
    // (Windows resamples from the device rate, 48 kHz with the inbox driver, E056).
    WAVEFORMATEXTENSIBLE fx = {0};
    WAVEFORMATEX *fmt = &fx.Format, *mix = NULL;
    HRESULT hr = E_FAIL;
    uint32_t ch = 2;
    if (dev && SUCCEEDED(hr = dev->lpVtbl->Activate(dev, &IID_AudioClient, CLSCTX_ALL, NULL, (void **)&ac)) &&
        SUCCEEDED(hr = ac->lpVtbl->GetMixFormat(ac, &mix))) {
        ch = mix->nChannels ? mix->nChannels : 2;
        DWORD mask = mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE ? ((WAVEFORMATEXTENSIBLE *)mix)->dwChannelMask : 0;
        vtiemu_log("audio: endpoint mix format %u ch, %lu Hz, %u bit, mask 0x%lx", ch, (unsigned long)mix->nSamplesPerSec,
                   mix->wBitsPerSample, (unsigned long)mask);
        CoTaskMemFree(mix);
        fx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        fx.Format.nChannels = (WORD)ch;
        fx.Format.nSamplesPerSec = RATE;
        fx.Format.wBitsPerSample = 16;
        fx.Format.nBlockAlign = (WORD)(2 * ch);
        fx.Format.nAvgBytesPerSec = RATE * 2 * ch;
        fx.Format.cbSize = sizeof fx - sizeof fx.Format;
        fx.Samples.wValidBitsPerSample = 16;
        fx.dwChannelMask = mask;
        fx.SubFormat = (GUID){0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}}; // PCM
    }
    // Exclusive mode first: the device itself then runs at 44.1 kHz like with the Access driver (the inbox driver
    // picks 48 kHz in shared mode, and the 48k->44.1k conversion plus the 48 kHz device clock made tempo sync and
    // audio timing drift, E067); no resampling and a short, device-clocked period. Shared mode is the fallback.
    int exclusive = 0;
    if (SUCCEEDED(hr) && ac->lpVtbl->IsFormatSupported(ac, AUDCLNT_SHAREMODE_EXCLUSIVE, fmt, NULL) == S_OK) {
        REFERENCE_TIME def = 0, min = 0;
        ac->lpVtbl->GetDevicePeriod(ac, &def, &min);
        HRESULT ex = ac->lpVtbl->Initialize(ac, AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, def,
                                            def, fmt, NULL);
        if (ex == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) { // retry with the aligned buffer size
            UINT32 frames = 0;
            ac->lpVtbl->GetBufferSize(ac, &frames);
            ac->lpVtbl->Release(ac);
            ac = NULL;
            def = (REFERENCE_TIME)(10000.0 * 1000 / RATE * frames + 0.5);
            if (SUCCEEDED(dev->lpVtbl->Activate(dev, &IID_AudioClient, CLSCTX_ALL, NULL, (void **)&ac)))
                ex = ac->lpVtbl->Initialize(ac, AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, def,
                                            def, fmt, NULL);
        }
        if (SUCCEEDED(ex)) {
            exclusive = 1;
            vtiemu_log("audio: exclusive mode, period %.2f ms", def / 10000.0);
        } else {
            vtiemu_log("audio: exclusive mode failed (hr=0x%08lx): shared mode", (unsigned long)ex);
            if (ac)
                ac->lpVtbl->Release(ac);
            ac = NULL;
            hr = dev->lpVtbl->Activate(dev, &IID_AudioClient, CLSCTX_ALL, NULL, (void **)&ac);
        }
    } else if (SUCCEEDED(hr)) {
        vtiemu_log("audio: 44.1 kHz %u ch not available in exclusive mode: shared mode (resampled)", ch);
    }
    if (SUCCEEDED(hr) && !exclusive)
        hr = ac->lpVtbl->Initialize(ac, AUDCLNT_SHAREMODE_SHARED,
                                    AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                        AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                    100000 /* 10 ms */, 0, fmt, NULL);
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
        vtiemu_log("audio: capturing 44.1 kHz 16-bit %u ch from the Virus TI endpoint (%s)", ch, exclusive ? "exclusive" : "shared");
        uint64_t total = 0;
        while (g_running) {
            if (WaitForSingleObject(ev, 200) != WAIT_OBJECT_0)
                continue;
            BYTE *data;
            UINT32 n;
            DWORD flags;
            while (SUCCEEDED(cc->lpVtbl->GetBuffer(cc, &data, &n, &flags, NULL, NULL)) && n) {
                push_frames((flags & AUDCLNT_BUFFERFLAGS_SILENT) ? NULL : (const int16_t *)data, ch, n);
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
