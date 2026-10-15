// AudioIn on a ring of its own (POSIX shm with the broker's layout, vtiaudioshm.h; never the broker's ring): header
// check, stamp-aligned reads, drift correction and re-anchoring, raw reads for the ASIO driver, the output ring.
// Linux only (macOS reads Core Audio, Windows a named mapping).
#include "check.h"
#include "device/audioin.h"
#ifdef __linux__
#include "../../src/broker/vtiaudioshm.h"
#include <fcntl.h>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

using namespace vti;

namespace {
// frame f carries recognisable samples: channel c = (f * 8 + c) & 0x7FFF
struct Ring {
    std::string name = "/vtitests-audio-" + std::to_string(getpid());
    vti_audio_shm *shm = nullptr;
    explicit Ring(uint32_t magic = VTI_AUDIO_MAGIC, uint32_t version = VTI_AUDIO_VERSION)
    {
        shm_unlink(name.c_str());
        int fd = shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd < 0 || ftruncate(fd, sizeof(vti_audio_shm)))
            return;
        void *p = mmap(nullptr, sizeof(vti_audio_shm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        ::close(fd);
        if (p == MAP_FAILED)
            return;
        shm = (vti_audio_shm *)p;
        shm->magic = magic, shm->version = version, shm->rate = 44100;
        shm->in_ch = VTI_AUDIO_IN_CH, shm->out_ch = VTI_AUDIO_OUT_CH, shm->ring_frames = VTI_AUDIO_RING;
    }
    ~Ring()
    {
        if (shm)
            munmap(shm, sizeof(vti_audio_shm));
        shm_unlink(name.c_str());
    }
    static int16_t sample(int64_t f, int c) { return (int16_t)((f * 8 + c) & 0x7FFF); }
    void produce(int64_t upto) // the synth delivered frames up to `upto` (exclusive)
    {
        for (int64_t f = shm->in_frames; f < upto; f++)
            for (int c = 0; c < VTI_AUDIO_IN_CH; c++)
                shm->in[(f & (VTI_AUDIO_RING - 1)) * VTI_AUDIO_IN_CH + c] = sample(f, c);
        shm->in_frames = upto;
        shm->streaming = 1;
    }
};
const float K = 1.0f / 32768.0f;
} // namespace

TEST(audioin_open_checks_the_ring)
{
    AudioIn a;
    CHECK(!a.open("/vtitests-audio-missing"));
    {
        Ring bad(0x12345678); // not a TI Revive ring
        CHECK(bad.shm && !a.open(bad.name.c_str()));
    }
    Ring r;
    CHECK(r.shm && a.open(r.name.c_str()));
    CHECK(!a.streaming() && a.newest_frame() == -1); // the broker has not started the stream yet
    float l1[4], r1[4], l2[4], r2[4];
    a.read(l1, r1, l2, r2, 4, 100);
    CHECK(l1[0] == 0 && r2[3] == 0); // silence until it streams
    a.close();
    a.close(); // twice is harmless
}

TEST(audioin_read_at_maps_usb1_usb2)
{
    Ring r;
    AudioIn a;
    CHECK(r.shm && a.open(r.name.c_str()));
    r.produce(5000);
    CHECK_EQ(a.newest_frame(), 5000);
    float l1[8], r1[8], l2[8], r2[8];
    a.read_at(l1, r1, l2, r2, 8, 4996); // 4 frames there, 4 not yet
    CHECK(l1[0] == Ring::sample(4996, 2) * K && r1[0] == Ring::sample(4996, 3) * K); // USB 1 = ring channels 2/3
    CHECK(l2[3] == Ring::sample(4999, 4) * K && r2[3] == Ring::sample(4999, 5) * K); // USB 2 = ring channels 4/5
    CHECK(l1[4] == 0 && r2[7] == 0);                                                  // the future is silence
    r.produce(40000);
    a.read_at(l1, r1, l2, r2, 1, 40000 - VTI_AUDIO_RING); // overwritten long ago (outside the safe window)
    CHECK(l1[0] == 0);
    int16_t raw[3 * VTI_AUDIO_IN_CH];
    a.read_raw_at(raw, 3, 39998); // two frames, then the future
    CHECK(raw[0] == Ring::sample(39998, 0) && raw[VTI_AUDIO_IN_CH + 5] == Ring::sample(39999, 5));
    CHECK(raw[2 * VTI_AUDIO_IN_CH] == 0);
    a.read_raw_at(raw, 1, -5);
    CHECK(raw[0] == 0);
}

TEST(audioin_read_follows_the_synth_clock)
{
    Ring r;
    AudioIn a;
    CHECK(r.shm && a.open(r.name.c_str()));
    r.produce(10000);
    float l1[512], r1[512], l2[512], r2[512];
    a.read(l1, r1, l2, r2, 512, 1024); // first read: anchored `latency` behind the newest frame
    CHECK(l1[0] == Ring::sample(10000 - 1024, 2) * K);
    CHECK_EQ(a.position(), 10000 - 1024 + 512);
    CHECK_EQ(a.drops(), 0);
    // the synth runs ahead (distance 1024 + 300): one frame dropped per block, no jump
    r.produce(10000 + 512 + 300);
    int64_t before = a.position();
    a.read(l1, r1, l2, r2, 512, 1024);
    CHECK_EQ(a.position(), before + 513);
    // the synth falls behind (distance 1024 - 300): one frame repeated per block
    r.produce(a.position() + 1024 - 300);
    before = a.position();
    a.read(l1, r1, l2, r2, 512, 1024);
    CHECK_EQ(a.position(), before + 511);
    CHECK(l1[0] == l1[1]); // the repeated frame
    // underrun (the reader caught up with the synth): re-anchored and counted
    a.read(l1, r1, l2, r2, 512, 1024);
    a.read(l1, r1, l2, r2, 512, 1024);
    CHECK_EQ(a.drops(), 1);
    // a DAW stall: far behind -> re-anchored too
    r.produce(r.shm->in_frames + VTI_AUDIO_RING);
    a.read(l1, r1, l2, r2, 512, 1024);
    CHECK_EQ(a.drops(), 2);
    CHECK_EQ(a.position(), r.shm->in_frames - 1024 + 512);
}

TEST(audioin_output_ring)
{
    Ring r;
    AudioIn a;
    CHECK(r.shm && a.open(r.name.c_str()));
    int16_t st[2 * 3] = {1, -1, 2, -2, 3, -3};
    a.write_out(st, 3);
    CHECK_EQ(a.out_queued(), 3);
    CHECK(r.shm->out[0] == 1 && r.shm->out[5] == -3);
    r.shm->out_read = 2; // the broker played two frames
    CHECK_EQ(a.out_queued(), 1);
    a.close();
    CHECK_EQ(a.out_queued(), 0);
    Ring v1(VTI_AUDIO_MAGIC, 1); // a version 1 ring has no output part
    AudioIn b;
    CHECK(v1.shm && b.open(v1.name.c_str()));
    b.write_out(st, 3);
    CHECK_EQ(b.out_queued(), 0);
}
#else
TEST(audioin_ring)
{
    SKIP("the test ring is POSIX shared memory (Linux)");
}
#endif
