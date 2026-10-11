// vtiproto.h - framing between vtibroker.exe and its clients over \\.\pipe\vtibroker (byte-mode named pipe).
// Every message: vtimsg header + `len` payload bytes. All integers little-endian.
//
// client -> broker                          broker -> client
//   WRITE    {ep, data}                       RX       {ep, data}          (unsolicited, every IN transfer)
//   CONTROL  {seq, setup[8], data(OUT)}       CONTROL_REPLY {seq, result, data(IN)}
//                                             STATE    {connected}         (on connect and on change)
//   AUDIO    {on, event name}                 AUDIO_REPLY {result}         (protocol 2, vtiaudioshm.h)
#pragma once
#include <stdint.h>

#define VTI_PIPE_NAME "\\\\.\\pipe\\vtibroker"
#define VTI_PROTO_VERSION 2

enum {
    VTIMSG_HELLO = 1,         // client -> broker: u32 version
    VTIMSG_WRITE = 2,         // client -> broker: u8 ep, data...
    VTIMSG_CONTROL = 3,       // client -> broker: u32 seq, u8 setup[8], OUT data...
    VTIMSG_AUDIO = 4,         // client -> broker: u32 on (1 = start, 0 = stop), char event_name[] (on = 1)
    VTIMSG_RX = 0x81,         // broker -> client: u8 ep, data...
    VTIMSG_CONTROL_REPLY = 0x83, // broker -> client: u32 seq, i32 result (bytes or -1), IN data...
    VTIMSG_STATE = 0x84,      // broker -> client: u32 connected (1 = device open)
    VTIMSG_AUDIO_REPLY = 0x85, // broker -> client: i32 result (1 = streaming into VTI_AUDIO_SHM_NAME, 0 = no WinUSB
                               // audio function, -1 = error)
};

#pragma pack(push, 1)
typedef struct {
    uint16_t type;
    uint16_t len; // payload bytes
} vtimsg;
#pragma pack(pop)

#define VTIMSG_MAX 4096
