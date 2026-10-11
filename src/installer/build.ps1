# build.ps1 -Root <dir with bin\ and src\> - build "TI Revive.msi" with WiX 5 (Windows build VM, M6).
# Expects: <Root>\bin\{vtibroker.exe,vtilaunch.exe,vtiemu-pipe.dll,VirusTI_WinUSB.vst3}, <Root>\src\{inf,installer}.
param([string]$Root = 'C:\build\tirevive')
$ErrorActionPreference = 'Stop'
$env:DOTNET_ROOT = 'C:\build\dotnet'
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
$env:PATH = "C:\build\dotnet;C:\build\wix;$env:PATH"
$out = Join-Path $Root 'out'
New-Item -ItemType Directory -Force $out | Out-Null
wix build (Join-Path $Root 'src\installer\TIRevive.wxs') -arch x64 `
    -ext WixToolset.UI.wixext -ext WixToolset.Util.wixext `
    -d "Bin=$(Join-Path $Root 'bin')" -d "Src=$(Join-Path $Root 'src')" `
    -o (Join-Path $out 'TI Revive.msi')
if ($LASTEXITCODE) { throw "wix build failed ($LASTEXITCODE)" }
Get-Item (Join-Path $out 'TI Revive.msi') | Select-Object Name, Length
