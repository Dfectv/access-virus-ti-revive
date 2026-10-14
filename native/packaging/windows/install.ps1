# install.ps1 - test install of the native Virus TI plug-in and ASIO driver (run through install.cmd, as administrator)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
function Say($t) { Write-Host $t }
function Warn($t) { Write-Host "WARNING: $t" -ForegroundColor Yellow }

# prerequisites (warnings only: the plug-in reports what is missing as well)
$tir = (Get-ItemProperty 'HKLM:\SOFTWARE\TI Revive' -ErrorAction SilentlyContinue).InstallDir
if (-not $tir) { $tir = "$env:ProgramW6432\TI Revive" }
if (Test-Path "$tir\vtibroker.exe") { Say "TI Revive found: $tir" } else { Warn "TI Revive is not installed (it provides the USB driver and the broker). Install TI Revive 0.7.4 first." }
$ctl = "$env:ProgramW6432\Access Music\Virus TI\Common\Virus Control.dll"
if (Test-Path $ctl) { Say "Access Virus TI files found" } else { Warn "Access 'Virus TI' software not found ($ctl). The plug-in loads its graphics and firmware from there." }
if (Get-Service VIRUSUSB -ErrorAction SilentlyContinue) { Warn "the old Access USB driver (service VIRUSUSB) is installed; if the Virus is not found, see the TI Revive README (remove the Access driver)." }

Get-ChildItem $here -Recurse | Unblock-File

# VST3 plug-in
$vst = "$env:CommonProgramW6432\VST3\VirusTINative.vst3"
if (Test-Path $vst) { Remove-Item $vst -Recurse -Force }
Copy-Item "$here\VirusTINative.vst3" $vst -Recurse
Say "plug-in installed: $vst"

# ASIO driver
$dir = "$env:ProgramW6432\virus-ti-native"
New-Item -ItemType Directory -Force $dir | Out-Null
$dll = "$dir\VirusTINative_ASIO.dll"
function Reg32([string[]]$a) { (Start-Process regsvr32.exe -ArgumentList $a -Wait -PassThru).ExitCode }
if (Test-Path $dll) { Reg32 @('/s', '/u', "`"$dll`"") | Out-Null }
try { Copy-Item "$here\VirusTINative_ASIO.dll" $dll -Force }
catch { # loaded by a running DAW: move the old file aside, it is deleted on the next install
    Get-ChildItem "$dir\*.old" -ErrorAction SilentlyContinue | Remove-Item -ErrorAction SilentlyContinue
    Rename-Item $dll ("VirusTINative_ASIO.dll." + (Get-Date -f yyyyMMddHHmmss) + ".old")
    Copy-Item "$here\VirusTINative_ASIO.dll" $dll }
$rc = Reg32 @('/s', "`"$dll`"")
if ($rc -ne 0) { Warn "regsvr32 failed ($rc)" } else { Say "ASIO driver registered: 'Virus TI (native)'" }
Copy-Item "$here\uninstall.ps1", "$here\uninstall.cmd" $dir -Force

# diagnostics on for this test build: logs in %LOCALAPPDATA%\virus-ti-native
$diag = "$env:LOCALAPPDATA\virus-ti-native"
New-Item -ItemType Directory -Force $diag | Out-Null
New-Item -ItemType File -Force "$diag\diag.on" | Out-Null
New-Item -ItemType File -Force "$diag\klog.on" | Out-Null # per-note timing in diag.log (test builds)
Say "diagnostic logs on: $diag"
Say ""
Say "Done. Close and reopen your DAW, then rescan plug-ins."
