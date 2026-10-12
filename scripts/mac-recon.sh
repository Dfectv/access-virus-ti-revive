#!/usr/bin/env bash
# mac-recon.sh - first look at the macOS test machine (runs on the Mac, read-only): system, Rosetta, the Virus TI
# on USB, Core Audio / Core MIDI view, installed Access components, DAW. Used via `scripts/mac.sh recon`.
echo "== system"; sw_vers; uname -m; sysctl -n machdep.cpu.brand_string
echo "== Rosetta 2"; (arch -x86_64 /usr/bin/true && echo "installed") 2>/dev/null || echo "NOT installed (softwareupdate --install-rosetta)"
echo "== security"; csrutil status 2>/dev/null; spctl --status 2>/dev/null
echo "== Virus TI on USB (133e:0815)"
ioreg -p IOUSB -l -w0 2>/dev/null | grep -iE '"USB Product Name"|"idVendor"|"idProduct"|"kUSBProductString"' | paste - - - 2>/dev/null | grep -i -E "virus|4926|2069" || echo "not found in IOUSB plane"
system_profiler SPUSBDataType 2>/dev/null | grep -B2 -A10 -i "virus" | head -30
echo "== interfaces and drivers of the Virus"
ioreg -l -w0 -r -c IOUSBHostInterface 2>/dev/null | grep -E '"bInterfaceNumber"|"bInterfaceClass"|"CFBundleIdentifier"|"IOClass"' | head -40
echo "== Core Audio devices"; system_profiler SPAudioDataType 2>/dev/null | grep -E "^\s{8}[^ ].*:$|Input Channels|Output Channels|Current SampleRate|Manufacturer" | head -40
echo "== Core MIDI devices"; ls "/Library/Audio/MIDI Drivers" 2>/dev/null
echo "== Access Music components"
ls -la "/Library/Application Support/Access Music/Virus TI/Common" 2>/dev/null
ls -d /Library/Audio/Plug-Ins/{VST,VST3,Components}/*Virus* 2>/dev/null
ls -d /Library/Extensions/AccessVirusTI.kext /Library/Audio/Plug-Ins/HAL/de.access-music* 2>/dev/null
kmutil showloaded 2>/dev/null | grep -i access || echo "no Access kext loaded"
echo "== DAW"; ls -d /Applications/Ableton* 2>/dev/null
for a in /Applications/Ableton*/Contents/MacOS/*; do [ -f "$a" ] && file "$a"; done 2>/dev/null | head -3
echo "== tools"; xcode-select -p 2>/dev/null || echo "no Xcode command line tools"; ls /opt/homebrew/bin/brew 2>/dev/null || command -v brew || echo "no Homebrew"
