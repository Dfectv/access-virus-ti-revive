// audioin.cpp - see audioin.h.
#include "audioin.h"
extern "C" {
#include "vtiaudioshm.h"
}
#include <cstdio>
#include <cstring>
#ifdef __linux__
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace vti {

#define shm_ ((vti_audio_shm *)map_)

AudioIn::~AudioIn() { close(); }

bool AudioIn::open()
{
#ifdef __linux__
    char name[64];
    snprintf(name, sizeof name, VTI_AUDIO_SHM_LINUX, (unsigned)getuid());
    int fd = shm_open(name, O_RDWR, 0);
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
#else
    return false; // macOS: Core Audio device; Windows: named mapping (R6)
#endif
}

void AudioIn::close()
{
#ifdef __linux__
    if (shm_)
        munmap(map_, sizeof(vti_audio_shm));
#endif
    map_ = nullptr;
}

bool AudioIn::streaming() const { return shm_ && shm_->streaming; }

int64_t AudioIn::newest_frame() const { return streaming() ? __atomic_load_n(&shm_->in_frames, __ATOMIC_ACQUIRE) : -1; }

void AudioIn::read_at(float *l1, float *r1, float *l2, float *r2, int n, int64_t pos)
{
    int64_t newest = streaming() ? __atomic_load_n(&shm_->in_frames, __ATOMIC_ACQUIRE) : -1;
    const float k = 1.0f / 32768.0f;
    for (int i = 0; i < n; i++) {
        int64_t f = pos + i;
        if (newest < 0 || f >= newest || f < newest - VTI_AUDIO_RING + 1024) {
            l1[i] = r1[i] = l2[i] = r2[i] = 0;
            continue;
        }
        const int16_t *fr = shm_->in + (f & (VTI_AUDIO_RING - 1)) * VTI_AUDIO_IN_CH;
        l1[i] = fr[2] * k, r1[i] = fr[3] * k, l2[i] = fr[4] * k, r2[i] = fr[5] * k;
    }
}

void AudioIn::read(float *l1, float *r1, float *l2, float *r2, int n, int latency)
{
    auto silence = [&](int from) {
        for (int i = from; i < n; i++)
            l1[i] = r1[i] = l2[i] = r2[i] = 0;
    };
    if (!streaming()) {
        silence(0);
        return;
    }
    int64_t newest = __atomic_load_n(&shm_->in_frames, __ATOMIC_ACQUIRE);
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
    }
    pos_ += n - step_back;
}

} // namespace vti
