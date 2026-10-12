// vtiusbaudio_libusb.h - Virus TI USB audio on libusb (Linux broker), see vtiusbaudio_libusb.c
#pragma once
#include <stdint.h>

#define VLA_RATE 44100

struct libusb_context;
struct libusb_device_handle;
typedef void (*vla_in_fn)(const int16_t *pcm, uint32_t frames, void *user); // 6 ch interleaved, from the synth
typedef void (*vla_out_fn)(int16_t *pcm, uint32_t frames, void *user);      // 2 ch interleaved, to the synth

// Takes interfaces 0..2 (detaching snd-usb-audio) and streams until vla_stop(). Callbacks run on the libusb
// event thread. Returns 0 when streaming.
int vla_start(struct libusb_context *ctx, struct libusb_device_handle *h, vla_in_fn in, vla_out_fn out, void *user);
void vla_stop(void);
int vla_running(void);
