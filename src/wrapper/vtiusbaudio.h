// vtiusbaudio.h - Virus TI USB audio streaming on WinUSB (vtiusbaudio.c, findings E075).
#pragma once
#include <windows.h>
#include <stdint.h>

#define VUA_RATE 44100

// Called once per completed IN read with `frames` interleaved 6-channel int16 frames (device channel order).
typedef void (*vua_in_fn)(const int16_t *pcm, uint32_t frames, void *user);
typedef void (*vua_log_fn)(const char *fmt, ...);

// Source of the audio sent to the synth (EP 0x01): fill `frames` interleaved 2-channel int16 frames at dst (called
// once per OUT packet from the streaming thread). Not set (default): silence.
typedef void (*vua_out_fn)(int16_t *dst, uint32_t frames, void *user);
void vua_set_out(vua_out_fn fn, void *user);

// Stream until *running becomes 0. Returns -1 at once if the WinUSB audio function (MI_00) is not present or
// cannot be opened (the caller then uses WASAPI), 0 after a normal stop, 1 after a setup/stream error.
int vua_run(volatile LONG *running, vua_in_fn in, void *user, vua_log_fn log);
