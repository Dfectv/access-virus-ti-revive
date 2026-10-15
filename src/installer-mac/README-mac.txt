TI Revive for macOS - Access Virus TI Total Integration without the old Access drivers
=====================================================================================

Requirements
- macOS 14.4 or newer. The plug-in "TI Revive Virus Control" (VST3) is universal: native on Apple Silicon and Intel,
  no Rosetta. Only the Audio Unit for Logic Pro / GarageBand runs the original plug-in and needs Rosetta 2
  ("Install Access Virus TI software.command" checks it and offers to install it; or in Terminal:
  softwareupdate --install-rosetta --agree-to-license)
- Your own Access "Virus TI Software Suite" installer (.pkg, e.g. 5.1.7.00). TI Revive contains no Access software.
- A DAW that loads VST3 plug-ins (tested: Ableton Live 12) or Audio Units (tested: GarageBand). The DAW does NOT need
  to run under Rosetta.

Install
1. Double-click "TI Revive.pkg" (not signed: if macOS refuses, right-click > Open, or System Settings > Privacy &
   Security > "Open Anyway").
2. Put your Access "Virus TI Software Suite ... .pkg" into this folder and double-click
   "Install Access Virus TI software.command". It installs only the Access plug-ins and Virus Control, without the
   Access driver packages (they cannot run on Apple Silicon; TI Revive replaces them).
   If you installed the Access software before: that is fine, its drivers simply never load on Apple Silicon. Run
   TI Revive.pkg again afterwards, though, so TI Revive's libVirusUSB.dylib is the one in place.
3. Connect the Virus TI by USB. Start your DAW and allow it to use the microphone when macOS asks
   (System Settings > Privacy & Security > Microphone): macOS only gives the Virus' audio to apps with that
   permission; without it the plug-in stays silent.
4. Rescan plug-ins and use the VST3 "TI Revive Virus Control" (vendor "TI Revive"): a native plug-in (Apple Silicon
   and Intel) that looks and works like the original Virus Control, no Rosetta needed. Projects saved with the
   VST3 "Virus TI (TI Revive)" of earlier versions open with it. Logic Pro / GarageBand: the Audio Unit
   "Virus TI (TI Revive)" (runs the original plug-in under Rosetta, see below).

How it works
"TI Revive Virus Control" reads the skin, the parameter descriptions and the ROM banks from your Access
installation and talks to the Virus through TI Revive's broker. The Audio Unit for Logic works the old way:
The original Access plug-in is Intel-only and needs the Access kernel driver, which cannot load on Apple Silicon.
the Audio Unit "Virus TI (TI Revive)" runs the original in a helper process under Rosetta and shows its
editor in your DAW. A small background service (vtibroker, started on demand by the plug-in, ends 30 s after the
last use) talks to the Virus over USB, and TI Revive's own libVirusUSB.dylib takes the place of Access' driver
library. Audio comes from macOS' built-in USB
audio driver (the "Virus TI" audio device, 44.1 kHz).

Uninstall
Double-click "Uninstall TI Revive.command" (also in /Library/Application Support/TI Revive). The Access plug-ins are
removed with Access' own uninstaller (/Applications/Access Music/Virus TI).

Logs (for bug reports): /tmp/tirevive-bridge.log, /tmp/tirevive-helper.log, /tmp/tirevive-libvirususb.log,
/tmp/tirevive-vtibroker.log

Not affiliated with Access Music or Kemper. https://github.com/derpsn/access-virus-ti-revive
