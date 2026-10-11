// vtiaudioshm.h - Virus audio shared between vtibroker.exe and its audio clients (plug-in, later ASIO driver).
//
// The broker owns the USB audio function (MI_00) and streams it on WinUSB (vtiusbaudio.c, synth = clock master).
// Every received block of frames is appended to the input ring below; `in_frames` is the total number of frames
// the synth produced since the stream started (gaps from stalls are filled with silence, so it never slips), and
// each registered client's event is signalled. Clients read from their own position, so several of them (one
// plug-in per DAW, an ASIO driver) follow the same synth clock. Created by the broker as a named mapping in the
// session namespace; clients open it read/write after VTIMSG_AUDIO returned 1.
#pragma once
#include <stdint.h>

#define VTI_AUDIO_SHM_NAME "Local\\vtibroker_audio"
#define VTI_AUDIO_MAGIC 0x41415456 // "VTAA"
#define VTI_AUDIO_VERSION 1
#define VTI_AUDIO_RING 16384 // frames per ring (power of 2, ~370 ms at 44.1 kHz)
#define VTI_AUDIO_IN_CH 6    // device order: USB 1-3 L/R
#define VTI_AUDIO_OUT_CH 2

typedef struct {
    uint32_t magic, version, rate, in_ch, out_ch, ring_frames;
    volatile int64_t in_frames; // frames written to `in` since the stream started (written after the data)
    volatile int64_t in_qpc;    // QueryPerformanceCounter at the last in_frames update
    volatile int32_t streaming; // 1 while the broker streams
    uint32_t reserved[17];
    int16_t in[VTI_AUDIO_RING * VTI_AUDIO_IN_CH]; // frame f at (f & (VTI_AUDIO_RING - 1)) * VTI_AUDIO_IN_CH
} vti_audio_shm;
