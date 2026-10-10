# Inventory

## Installer
| File | SHA256 | Size | Notes |
|------|--------|------|-------|
| Virus TI Software Suite 5.1.7.00 64-bit.msi | f9a6f5c9…0a466 | 47,767,552 | baseline (E001) |

## PE files
Signer for all PEs: Kemper GmbH, VeriSign Class 3 Code Signing 2010 CA (G5 root), Symantec timestamp (vtimidi: older VeriSign TSA). Full per-file data: work/pe/<name>.txt (scripts/pe_inventory.py). SHA256 shown as 16-hex prefix.

| File | Location | SHA256 | Arch | Link date | Version / company | Key imports | Exports | Role |
|---|---|---|---|---|---|---|---|---|
| VirusUSB.sys | Access Music/Virus TI/Usbdriver (MSI) = C:/Windows/System32/drivers (VM) | ea5426ac6aa5f361 | x64 kernel | 2014-08-05 | v2.9.66 "access" | ntoskrnl, HAL, USBD | 0 | function driver for whole composite device + child enumerator |
| vtimidi.sys | .../Mididriver | 18a959710bbfc779 | x64 kernel | 2009-04-22 | 1.0.64.0 "Kemper Digital Gmbh" | ntoskrnl, portcls, USBD | 0 | KS MIDI miniport on MI_03 child |
| vtiaudio.sys | .../Audiodriver | bf6a2afcc7e23576 | x64 kernel | 2014-08-05 | v2.9.66 "usb-audio.de" | NTOSKRNL, portcls | 0 | WaveRT/WDM audio on MEDIA\VTIAUDIOADAPTER |
| VirusUSB_X64.dll | .../Common (x64) | 2426d861339b2915 | x64 | 2014-08-05 | v2.9.66 "access" | SETUPAPI, WINMM(time), KERNEL32 (CreateFileA, DeviceIoControl)… | 5 (COM + pt_GetAsioDriverVersion) | ASIO driver |
| VirusUSB.dll | Program Files (x86)/.../Common | d19368d3a67a3790 | x86 | 2014-08-05 | v2.9.66 | same as x64 | 5 | ASIO driver (32-bit) |
| Virus Control.dll | .../Common (x64) | a622f9a23696f961 | x64 | 2017-09-29 | 5.1.7.00 Kemper GmbH | SETUPAPI, WINMM(time only), WS2_32, CreateFile, DeviceIoControl, LoadLibrary | 3 | plug-in core (13.98 MB) |
| Virus Control.dll (x86) | Program Files (x86)/.../Common | 3e844c3576ed74ea | x86 | - | - | not inventoried yet | - | 32-bit plug-in core |
| Virus Control Center.exe | .../Common (x64) | 32db1460b4375545 | x64 | 2017-09-28 | 5.1.7.00 | SETUPAPI, WINMM(time), CreateFile, DeviceIoControl | 0 | standalone host |
| Virus TI.vst3 = VSTPlugins/Virus TI(.Snow).dll | Common Files/VST3, VSTPlugins | 0cd8d04ea22bc4ef | x64 | 2017-09-28 | 5.1.7.00 | no SETUPAPI/DeviceIoControl | 5 | loader stub (168 KB) |
| Virus TI.dpm (= x86 Virus TI Snow.dll) | Digidesign/DAE/Plug-Ins | 0a05b16f023b6217 | x86 | 2017-09-28 | 5.1.7.00 | - | 37 | RTAS stub |
| InstallAssist.exe | Program Files (x86)/.../Common | 75dc066ee305cc75 | x86 | 2017-09-28 | - | GUI only | 0 | installer helper UI |
| FirmwareUpdater.exe | Program Files (x86)/.../Common | 42fb5d8a85da2176 | x86 | 2017-09-28 | 5.1.7.00 | SETUPAPI, DeviceIoControl… | 0 | DO NOT RUN |
| CAInstallerHelper | MSI Binary table | 0be855f3efb6ce15 | x64 | 2017-09-29 | 5.1.7.00 | msi, POWRPROF | 4 | immediate CAs |
| CAInstallerHelperDeferred | MSI Binary table | 5452ea540d10b291 | x64 | 2017-09-29 | 5.1.7.00 | msi, DIFXAPI, SETUPAPI… | 7 | driver install + UpdateTIOS |
| DIFxAPI.dll | MSI root | e70bbcee0d01658c | x64 | - | - | - | - | Microsoft DIFx (redistributable) |

Other: firmware.bin 8e3daea8552f9ecb (8,569,757 B, IFF) - hash/copy only. Ableton/patch/docs files not inventoried.

## INF wiring
```
USB\VID_133E&PID_0815 (composite, 5 ifaces)  -- VirusUSB.sys (service VIRUSUSB, virususb.inf/oem9, replaces usbccgp)  [E005]
 |-- USB\VID_133E&PID_0815&MI_03  (child PDO created by VirusUSB)  -- vtimidi.sys (VTIMIDEV01, vtimidi.inf/oem11)
 |      KS MIDI: "Virus TI MIDI" (VTIMIDI01), "Virus TI Synth" (VTIMIDI02)                                        [E006,E007]
 |-- MEDIA\VTIAUDIOADAPTER          (software child created by VirusUSB) -- vtiaudio.sys (VTIAUDIO, vtiaudio.inf/oem10)
 |      KS Wave + Topology "Virus TI Audio"                                                                        [E006]
 `-- (no devnode for MI_00..02 audio class or MI_04) -> handled inside VirusUSB.sys; user-mode reaches it via
      SetupDi device interface + DeviceIoControl (ASIO DLL, Virus Control.dll, Control Center)                     [E009,E010]
```
No filter drivers declared. All services demand-start, kernel drivers. VIRUSUSB\Parameters holds Ploytec-style audio config (Frequency 48000, In 6ch/16bit, Out 2ch/16bit, BufferSize 256, PerformanceMode 3, routing/mixer tables) [E012].

## MSI vs. VM installed: differences
- The 3 drivers in System32\drivers are byte-identical to the MSI (E008). Install tree in the VM matches MSI layout (originals/installed/vti/files.txt); additionally VSTPlugins\Access Music\Virus TI.dll exists (same 168,200 B stub, presumably a DuplicateFile entry).
- Driver store: oem9 (virususb), oem10 (vtiaudio), oem11 (vtimidi), all Kemper GmbH signed (originals/installed/vti/enum-drivers.txt).
