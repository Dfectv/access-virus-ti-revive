// devicestatus.h - the editor's device status values, as the original sets them: its status routine (0x180071190)
// sets "Internal System Info/Audio|MIDI|USB Communication" to 1 (error) while no device is open, 0 when it works; the
// layout shows a page per "SystemStatus/Error" value (2 = VDM not Master: "Virus unavailable", 3 = No Device
// connected: "No Virus TI can be found"). Header only: the plug-in and the tests use it.
#pragma once
#include <string>

namespace vti {

struct DeviceStatus {
    int system_error = 0;                       // SystemStatus/Error
    int usb_comm = 0, midi_comm = 0, audio_comm = 0; // Internal System Info/... Communication (1 = error)
};

// present = the broker sees the Virus; elsewhere = another program's plug-in holds it (we wait)
inline DeviceStatus device_status(bool present, bool elsewhere)
{
    DeviceStatus s;
    if (elsewhere) {
        s.system_error = 2; // the other program talks to the Virus: no communication error here
        return s;
    }
    if (!present) {
        s.system_error = 3;
        s.usb_comm = s.midi_comm = s.audio_comm = 1;
    }
    return s;
}

// the value of one status parameter by its path, -1 if `path` is none of them
inline int device_status_value(const DeviceStatus &s, const std::string &path)
{
    if (path == "SystemStatus/Error")
        return s.system_error;
    if (path == "Internal System Info/USB Communication")
        return s.usb_comm;
    if (path == "Internal System Info/MIDI Communication")
        return s.midi_comm;
    if (path == "Internal System Info/Audio Communication")
        return s.audio_comm;
    return -1;
}

} // namespace vti
