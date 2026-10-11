# build.ps1 - build "TI Revive.msi" with WiX 5.
#   -Bin     folder with vtibroker.exe, vtilaunch.exe, vtiemu-pipe.dll, VirusTI_WinUSB.vst3
#   -Src     repository src\ folder (inf\, installer\)
#   -Out     output folder
#   -Version MSI ProductVersion (major.minor.build)
# Used by the GitHub workflow (.github/workflows/build.yml) and by scripts/win11/build-msi.sh (build VM, where
# .NET and WiX live in C:\build\dotnet and C:\build\wix).
param([string]$Root = 'C:\build\tirevive', [string]$Bin, [string]$Src, [string]$Out, [string]$Version = '0.1.1')
$ErrorActionPreference = 'Stop'
if (-not $Bin) { $Bin = Join-Path $Root 'bin' }
if (-not $Src) { $Src = Join-Path $Root 'src' }
if (-not $Out) { $Out = Join-Path $Root 'out' }
if (Test-Path 'C:\build\dotnet') { # build VM layout
    $env:DOTNET_ROOT = 'C:\build\dotnet'
    $env:PATH = "C:\build\dotnet;C:\build\wix;$env:PATH"
}
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
New-Item -ItemType Directory -Force $Out | Out-Null
wix build (Join-Path $Src 'installer\TIRevive.wxs') -arch x64 `
    -ext WixToolset.UI.wixext -ext WixToolset.Util.wixext `
    -d "Bin=$Bin" -d "Src=$Src" -d "Version=$Version" `
    -o (Join-Path $Out 'TI Revive.msi')
if ($LASTEXITCODE) { throw "wix build failed ($LASTEXITCODE)" }
Get-Item (Join-Path $Out 'TI Revive.msi') | Select-Object Name, Length
