// The editor's device status values (the original's status routine, 0x180071190, and the layout's SystemStatus/Error
// pages): which page the editor shows for a missing Virus, a Virus held by another program, and a connected Virus
#include "check.h"
#include "model/devicestatus.h"

using namespace vti;

TEST(devicestatus_missing_virus)
{
    // no Virus: the "No Virus TI can be found" page (SystemStatus/Error 3) and all three communication flags set
    DeviceStatus s = device_status(false, false);
    CHECK(s.system_error == 3);
    CHECK(s.usb_comm == 1 && s.midi_comm == 1 && s.audio_comm == 1);
}

TEST(devicestatus_connected_and_elsewhere)
{
    DeviceStatus ok = device_status(true, false);
    CHECK(ok.system_error == 0 && ok.usb_comm == 0 && ok.midi_comm == 0 && ok.audio_comm == 0);
    // another program's plug-in has the Virus: "Virus unavailable" (VDM not Master, 2), even while it is absent here
    CHECK(device_status(true, true).system_error == 2);
    CHECK(device_status(false, true).system_error == 2);
    CHECK(device_status(false, true).usb_comm == 0); // not a communication error: the other program talks to it
}

TEST(devicestatus_values_by_path)
{
    // the editor reads them by parameter path
    DeviceStatus s = device_status(false, false);
    CHECK(device_status_value(s, "SystemStatus/Error") == 3);
    CHECK(device_status_value(s, "Internal System Info/USB Communication") == 1);
    CHECK(device_status_value(s, "Internal System Info/MIDI Communication") == 1);
    CHECK(device_status_value(s, "Internal System Info/Audio Communication") == 1);
    CHECK(device_status_value(s, "Filter 1/Cutoff") == -1); // not a status value
}
