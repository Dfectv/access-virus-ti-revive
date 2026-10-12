TI Revive @VERSION@ for Linux (EXPERIMENTAL)
==========================================

Brings back the Access Virus TI's "Total Integration" plug-in on Linux, without the old Access drivers: your own
Access Virus TI plug-in (Windows version) runs through Wine + yabridge in your Linux DAW, and the TI Revive broker
(a small native Linux program) talks to the Virus over USB - MIDI, patch sync and the Virus' USB audio.

You need
- an Access Virus TI on USB (tested: TI2 Desktop)
- Wine (e.g. "sudo apt install wine") and yabridge (https://github.com/robbert-vdh/yabridge).
  IMPORTANT: with Wine 9.22 or newer (Ubuntu 26.04 ships Wine 10) the official yabridge 5.1.1 release shows the
  plug-in window but ignores all mouse clicks (yabridge issue #382/#409). Use a newer yabridge build from the
  project's "Automated builds" (GitHub Actions, master branch) or a release newer than 5.1.1.
- For stable real-time audio, let your user use real-time priority (Ubuntu: sudo usermod -aG pipewire $USER, then
  log out and in again) - otherwise yabridge warns about "low memory locking".
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
- Audio system in your DAW: use ALSA or JACK (with PipeWire: the pipewire-jack package, PipeWire set to 44.1 kHz).
  NOT PulseAudio: it hands the plug-in irregular block sizes, the Virus then reports timing errors, playback
  sounds messy and notes can hang. Use a buffer size of 64, 128, 256 or 512 at 44.1 kHz.
- Close your DAW normally (File -> Quit). The plug-in saves the synced Virus memory only then; if the DAW is
  killed (e.g. Ctrl+C in a terminal), the next start syncs again (about 7 minutes).
- While TI Revive uses the Virus, Linux cannot use it as a normal sound card; it is handed back automatically when
  the plug-in is closed (about 30 seconds later).
- One program at a time uses the Virus; a second one shows "no Virus TI connected".
- Uninstall: ./uninstall.sh

Logs: broker "journalctl --user -u tirevive-broker", plug-in "$WINEPREFIX/drive_c/users/$USER/AppData/Local/Temp/vtiemu-*.log".
TI Revive is not affiliated with Access Music. MIT license, https://github.com/derpsn/access-virus-ti-revive
