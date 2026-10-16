// audioin.cpp - see audioin.h.
#include "audioin.h"
extern "C" {
#include "vtiaudioshm.h"
}
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX // windows.h's min/max macros break std::min / std::max
#endif
#include <windows.h>
#endif
#ifdef _MSC_VER
#include <intrin.h>
#endif
#ifdef __linux__
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <CoreAudio/CoreAudio.h>
#include <string>
#include <vector>
#endif

namespace vti {

#define shm_ ((vti_audio_shm *)map_)

// the broker publishes in_frames after the data (release); read it with acquire semantics
static inline int64_t load_acquire(volatile int64_t *p)
{
#ifdef _MSC_VER
    int64_t v = *p; // aligned 64-bit loads are atomic on x64; the barrier keeps later reads after it
    _ReadWriteBarrier();
    return v;
#else
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
#endif
}

AudioIn::~AudioIn() { close(); }

#ifdef __APPLE__
// macOS: the Virus' audio function is USB class compliant, so Core Audio offers it as an input device. The broker only
// carries MIDI there; this reader fills the same ring (vti_audio_shm, here in our own memory) from an IOProc, so the
// rest of AudioIn (drift handling, read_at) is shared with Linux and Windows.
struct MacAudio {
    AudioObjectID dev = kAudioObjectUnknown;
    AudioDeviceIOProcID proc = nullptr;
    vti_audio_shm *ring = nullptr;
};

static std::string cf_string(AudioObjectID id, AudioObjectPropertySelector sel)
{
    AudioObjectPropertyAddress a{sel, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    CFStringRef cs = nullptr;
    UInt32 sz = sizeof cs;
    if (AudioObjectGetPropertyData(id, &a, 0, nullptr, &sz, &cs) || !cs)
        return {};
    char buf[256] = {};
    CFStringGetCString(cs, buf, sizeof buf, kCFStringEncodingUTF8);
    CFRelease(cs);
    return buf;
}

static int input_channels(AudioObjectID id)
{
    AudioObjectPropertyAddress a{kAudioDevicePropertyStreamConfiguration, kAudioObjectPropertyScopeInput,
                                 kAudioObjectPropertyElementMain};
    UInt32 sz = 0;
    if (AudioObjectGetPropertyDataSize(id, &a, 0, nullptr, &sz) || !sz)
        return 0;
    std::vector<uint8_t> buf(sz);
    auto *bl = (AudioBufferList *)buf.data();
    if (AudioObjectGetPropertyData(id, &a, 0, nullptr, &sz, bl))
        return 0;
    int n = 0;
    for (UInt32 i = 0; i < bl->mNumberBuffers; i++)
        n += (int)bl->mBuffers[i].mNumberChannels;
    return n;
}

// the Virus as Core Audio input device: name contains "Virus", at least 6 input channels (E082 order)
static AudioObjectID find_virus()
{
    AudioObjectPropertyAddress a{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                                 kAudioObjectPropertyElementMain};
    UInt32 sz = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, nullptr, &sz))
        return kAudioObjectUnknown;
    std::vector<AudioObjectID> ids(sz / sizeof(AudioObjectID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &sz, ids.data()))
        return kAudioObjectUnknown;
    for (AudioObjectID id : ids)
        if (cf_string(id, kAudioObjectPropertyName).find("Virus") != std::string::npos &&
            input_channels(id) >= VTI_AUDIO_IN_CH)
            return id;
    return kAudioObjectUnknown;
}

// Core Audio delivers Float32 (interleaved or one buffer per stream); the ring holds int16 frames of 6 channels
static OSStatus io_proc(AudioObjectID, const AudioTimeStamp *, const AudioBufferList *in, const AudioTimeStamp *,
                        AudioBufferList *, const AudioTimeStamp *, void *user)
{
    auto *r = (vti_audio_shm *)user;
    if (!in || !in->mNumberBuffers)
        return noErr;
    UInt32 frames = in->mBuffers[0].mNumberChannels
                        ? in->mBuffers[0].mDataByteSize / (sizeof(float) * in->mBuffers[0].mNumberChannels)
                        : 0;
    int64_t base = r->in_frames;
    for (UInt32 f = 0; f < frames; f++) {
        int16_t *dst = r->in + ((base + f) & (VTI_AUDIO_RING - 1)) * VTI_AUDIO_IN_CH;
        int ch = 0;
        for (UInt32 b = 0; b < in->mNumberBuffers && ch < VTI_AUDIO_IN_CH; b++) {
            const AudioBuffer &ab = in->mBuffers[b];
            const float *src = (const float *)ab.mData + f * ab.mNumberChannels;
            for (UInt32 c = 0; c < ab.mNumberChannels && ch < VTI_AUDIO_IN_CH; c++, ch++) {
                float v = src[c] * 32768.0f;
                dst[ch] = (int16_t)(v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : v);
            }
        }
    }
    __atomic_store_n(&r->in_frames, base + frames, __ATOMIC_RELEASE);
    return noErr;
}
#endif

bool AudioIn::open(const char *linux_name)
{
    (void)linux_name; // Linux only
#ifdef __linux__
    char name[64];
    snprintf(name, sizeof name, VTI_AUDIO_SHM_LINUX, (unsigned)getuid());
    int fd = shm_open(linux_name ? linux_name : name, O_RDWR, 0);
    if (fd < 0)
        return false;
    void *p = mmap(nullptr, sizeof(vti_audio_shm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ::close(fd);
    if (p == MAP_FAILED)
        return false;
    map_ = p;
    if (shm_->magic != VTI_AUDIO_MAGIC || shm_->ring_frames != VTI_AUDIO_RING || shm_->in_ch != VTI_AUDIO_IN_CH) {
        close();
        return false;
    }
    pos_ = -1;
    return true;
#elif defined(_WIN32)
    HANDLE m = OpenFileMappingA(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, VTI_AUDIO_SHM_NAME);
    if (!m)
        return false;
    void *p = MapViewOfFile(m, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(vti_audio_shm));
    CloseHandle(m); // the view keeps the mapping alive
    if (!p)
        return false;
    map_ = p;
    if (shm_->magic != VTI_AUDIO_MAGIC || shm_->ring_frames != VTI_AUDIO_RING || shm_->in_ch != VTI_AUDIO_IN_CH) {
        close();
        return false;
    }
    pos_ = -1;
    return true;
#elif defined(__APPLE__)
    auto *m = new MacAudio;
    m->dev = find_virus();
    if (m->dev == kAudioObjectUnknown) {
        delete m;
        return false;
    }
    // the Virus runs at 44.1 kHz like the plug-in's host (the clock master is the synth)
    AudioObjectPropertyAddress ra{kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal,
                                  kAudioObjectPropertyElementMain};
    Float64 rate = 44100;
    AudioObjectSetPropertyData(m->dev, &ra, 0, nullptr, sizeof rate, &rate);
    m->ring = new vti_audio_shm();
    m->ring->magic = VTI_AUDIO_MAGIC, m->ring->version = VTI_AUDIO_VERSION, m->ring->rate = 44100;
    m->ring->in_ch = VTI_AUDIO_IN_CH, m->ring->out_ch = VTI_AUDIO_OUT_CH, m->ring->ring_frames = VTI_AUDIO_RING;
    if (AudioDeviceCreateIOProcID(m->dev, io_proc, m->ring, &m->proc) || AudioDeviceStart(m->dev, m->proc)) {
        if (m->proc)
            AudioDeviceDestroyIOProcID(m->dev, m->proc);
        delete m->ring;
        delete m;
        return false;
    }
    m->ring->streaming = 1;
    mac_ = m;
    map_ = m->ring;
    pos_ = -1;
    return true;
#else
    return false;
#endif
}

void AudioIn::close()
{
#ifdef __linux__
    if (map_)
        munmap(map_, sizeof(vti_audio_shm));
#elif defined(_WIN32)
    if (map_)
        UnmapViewOfFile(map_);
#elif defined(__APPLE__)
    if (auto *m = (MacAudio *)mac_) {
        AudioDeviceStop(m->dev, m->proc);
        AudioDeviceDestroyIOProcID(m->dev, m->proc);
        delete m->ring;
        delete m;
        mac_ = nullptr;
    }
#endif
    map_ = nullptr;
}

bool AudioIn::streaming() const { return shm_ && shm_->streaming; }

int64_t AudioIn::newest_frame() const { return streaming() ? load_acquire(&shm_->in_frames) : -1; }

bool AudioIn::raw_peaks(int frames, int peak[6]) const
{
    for (int c = 0; c < 6; c++)
        peak[c] = 0;
    int64_t newest = newest_frame();
    if (newest < 0)
        return false;
    frames = std::min<int64_t>(frames, std::min<int64_t>(newest, VTI_AUDIO_RING - 1024));
    for (int64_t f = newest - frames; f < newest; f++) {
        const int16_t *fr = shm_->in + (f & (VTI_AUDIO_RING - 1)) * VTI_AUDIO_IN_CH;
        for (int c = 0; c < 6 && c < VTI_AUDIO_IN_CH; c++)
            peak[c] = std::max(peak[c], std::abs((int)fr[c]));
    }
    return true;
}

void AudioIn::read_at(float *l1, float *r1, float *l2, float *r2, int n, int64_t pos, float *l3, float *r3)
{
    int64_t newest = streaming() ? load_acquire(&shm_->in_frames) : -1;
    const float k = 1.0f / 32768.0f;
    const bool third = l3 && r3;
    for (int i = 0; i < n; i++) {
        int64_t f = pos + i;
        if (newest < 0 || f >= newest || f < newest - VTI_AUDIO_RING + 1024) {
            l1[i] = r1[i] = l2[i] = r2[i] = 0;
            if (third)
                l3[i] = r3[i] = 0;
            continue;
        }
        const int16_t *fr = shm_->in + (f & (VTI_AUDIO_RING - 1)) * VTI_AUDIO_IN_CH;
        l1[i] = fr[2] * k, r1[i] = fr[3] * k, l2[i] = fr[4] * k, r2[i] = fr[5] * k;
        if (third)
            l3[i] = fr[0] * k, r3[i] = fr[1] * k;
    }
}

void AudioIn::read(float *l1, float *r1, float *l2, float *r2, int n, int latency, float *l3, float *r3)
{
    const bool third = l3 && r3;
    auto silence = [&](int from) {
        for (int i = from; i < n; i++) {
            l1[i] = r1[i] = l2[i] = r2[i] = 0;
            if (third)
                l3[i] = r3[i] = 0;
        }
    };
    if (!streaming()) {
        silence(0);
        return;
    }
    int64_t newest = load_acquire(&shm_->in_frames);
    int64_t fill = newest - pos_;
    // (re)anchor when starting, after an underrun or when far behind (DAW stalled)
    if (pos_ < 0 || fill < n || fill > VTI_AUDIO_RING / 2) {
        if (pos_ >= 0)
            drops_++;
        pos_ = newest - latency;
        fill = latency;
    }
    // drift: one frame per block at most, keeps the distance within +-128 frames of the target
    int step_back = 0;
    if (fill > latency + 128)
        pos_++;
    else if (fill < latency - 128)
        step_back = 1;
    const float k = 1.0f / 32768.0f;
    const int16_t *in = shm_->in;
    for (int i = 0; i < n; i++) {
        int64_t f = pos_ + i - (step_back && i > 0 ? 1 : 0);
        const int16_t *fr = in + (f & (VTI_AUDIO_RING - 1)) * VTI_AUDIO_IN_CH;
        l1[i] = fr[2] * k, r1[i] = fr[3] * k, l2[i] = fr[4] * k, r2[i] = fr[5] * k;
        if (third)
            l3[i] = fr[0] * k, r3[i] = fr[1] * k;
    }
    pos_ += n - step_back;
}

void AudioIn::read_raw_at(int16_t *dst, int n, int64_t pos) const
{
    int64_t newest = newest_frame();
    for (int i = 0; i < n; i++) {
        int64_t f = pos + i;
        if (!map_ || newest < 0 || f >= newest || f < newest - VTI_AUDIO_RING || f < 0)
            memset(dst + i * VTI_AUDIO_IN_CH, 0, VTI_AUDIO_IN_CH * sizeof(int16_t));
        else
            memcpy(dst + i * VTI_AUDIO_IN_CH, shm_->in + (f & (VTI_AUDIO_RING - 1)) * VTI_AUDIO_IN_CH,
                   VTI_AUDIO_IN_CH * sizeof(int16_t));
    }
}

void AudioIn::write_out(const int16_t *stereo, int n)
{
    if (!map_ || shm_->version < 2)
        return;
    int64_t w = shm_->out_write;
    for (int i = 0; i < n; i++)
        memcpy(&shm_->out[((w + i) & (VTI_AUDIO_RING - 1)) * VTI_AUDIO_OUT_CH], stereo + i * VTI_AUDIO_OUT_CH,
               VTI_AUDIO_OUT_CH * sizeof(int16_t));
#ifdef _MSC_VER
    _ReadWriteBarrier();
    *(volatile int64_t *)&shm_->out_write = w + n; // x64: the data stores are visible before this one
#else
    __atomic_store_n(&shm_->out_write, w + n, __ATOMIC_RELEASE);
#endif
}

int64_t AudioIn::out_queued() const
{
    if (!map_)
        return 0;
    return load_acquire(&shm_->out_write) - load_acquire(&shm_->out_read);
}

} // namespace vti
