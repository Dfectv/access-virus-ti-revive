TI Revive @VERSION@ for Linux (EXPERIMENTAL)
==========================================

Brings back the Access Virus TI's "Total Integration" plug-in on Linux, without the old Access drivers: your own
Access Virus TI plug-in (Windows version) runs through Wine + yabridge in your Linux DAW, and the TI Revive broker
(a small native Linux program) talks to the Virus over USB - MIDI, patch sync and the Virus' USB audio.

You need
- an Access Virus TI on USB (tested: TI2 Desktop)
- Wine (e.g. "sudo apt install wine") and yabridge (https://github.com/robbert-vdh/yabridge)
- a Linux DAW that hosts VST3 plug-ins (REAPER, Bitwig Studio, Ardour, ...)
- your own copy of the Access installer "Virus TI Software Suite 5.1.7.00 64-bit.msi" (not included)

Install
1. Unpack this folder and open a terminal in it.
2. ./install.sh "/path/to/Virus TI Software Suite 5.1.7.00 64-bit.msi"
   (it asks for your password once to allow USB access to the Virus; the Access software goes into your Wine
   prefix, ~/.wine or $WINEPREFIX, WITHOUT its old drivers and firmware updater)
3. In your DAW, rescan plug-ins and load the VST3 "Virus TI (TI Revive)" (Snow: "Virus TI Snow (TI Revive)").
   The first start syncs the Virus' patch memory, which takes about 7 minutes (the Virus sends about one patch per
   second, as with the original driver); later starts use the cache and play after a few seconds.

Good to know
- While TI Revive uses the Virus, Linux cannot use it as a normal sound card; it is handed back automatically when
  the plug-in is closed (about 30 seconds later).
- One program at a time uses the Virus; a second one shows "no Virus TI connected".
- Uninstall: ./uninstall.sh

Logs: broker "journalctl --user -u tirevive-broker", plug-in "$WINEPREFIX/drive_c/users/$USER/AppData/Local/Temp/vtiemu-*.log".
TI Revive is not affiliated with Access Music. MIT license, https://github.com/derpsn/access-virus-ti-revive
