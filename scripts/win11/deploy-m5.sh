#!/usr/bin/env bash
# deploy-m5.sh - set up the Windows 11 test VM for milestone M5 (run with the Virus attached to win10, not win11):
#   1. copy our build + scripts + the user's Access MSI + Reaper installer to C:\vti
#   2. Access software without drivers/firmware (install-access-nodrivers.ps1)
#   3. WinUSB INF with self-signed catalog (vm-winusb-install.ps1)  <- the Win11 signing question
#   4. broker as logon task, wrapper VST3, Control Center launcher task
#   5. Reaper (silent), after checking its Authenticode signature
set -euo pipefail
cd "$(dirname "$0")/../.."
export VM=win11
vm() { scripts/vm.sh "$@"; }
B=work/build
vm ps 'New-Item -ItemType Directory -Force C:\vti\winusb | Out-Null' >/dev/null
for f in $B/vtibroker.exe $B/VirusTI_WinUSB.vst3 $B/vtilaunch.exe $B/vtiemu-pipe.dll $B/vtiprobe.exe $B/vtiprobe-pipe.exe \
         $B/vtisend.exe $B/vtimem.exe $B/vtipeek.exe scripts/vm-start-cc.cmd scripts/win11/install-access-nodrivers.ps1 \
         work/win11/dl/reaper*_x64-install.exe; do
  vm push "$f" 'C:/vti/'
done
vm push "originals/Virus TI Software Suite 5.1.7.00 64-bit.msi" 'C:/vti/VirusTI-5.1.7-x64.msi'
for f in src/inf/vti_winusb.inf scripts/vm-winusb-install.ps1; do vm push "$f" 'C:/vti/winusb/'; done

echo "== Access software (no drivers, no firmware updater)"
vm ps '& C:\vti\install-access-nodrivers.ps1 -Msi C:\vti\VirusTI-5.1.7-x64.msi 2>&1 | Out-String -Width 200'
echo "== WinUSB INF"
vm ps 'powershell -ExecutionPolicy Bypass -File C:\vti\winusb\vm-winusb-install.ps1 2>&1 | Out-String -Width 200'
echo "== broker task, wrapper, Control Center task"
vm ps 'Copy-Item C:\vti\vm-start-cc.cmd C:\vti\start-cc.cmd -Force;
       schtasks /Create /F /TN vtibroker /TR "C:\vti\vtibroker.exe" /SC ONLOGON /RU vti /IT /RL LIMITED | Out-String;
       schtasks /Create /F /TN vticc /TR "C:\vti\start-cc.cmd" /SC ONCE /ST 00:00 /RU vti /IT /RL LIMITED 2>$null | Out-String;
       schtasks /Run /TN vtibroker | Out-String;
       New-Item -ItemType Directory -Force "C:\Program Files\Common Files\VST3" | Out-Null;
       Copy-Item C:\vti\VirusTI_WinUSB.vst3 "C:\Program Files\Common Files\VST3\VirusTI_WinUSB.vst3" -Force;
       Get-ChildItem "C:\Program Files\Common Files\VST3" | % { $_.Name }'
echo "== Reaper"
vm ps '$r = Get-ChildItem C:\vti\reaper*_x64-install.exe | select -First 1; $s = Get-AuthenticodeSignature $r.FullName;
       "signature: " + $s.Status + " " + $s.SignerCertificate.Subject;
       if ($s.Status -eq "Valid") { Start-Process $r.FullName -ArgumentList "/S" -Wait; Test-Path "C:\Program Files\REAPER (x64)\reaper.exe" } else { "NOT installed: bad signature" }'
