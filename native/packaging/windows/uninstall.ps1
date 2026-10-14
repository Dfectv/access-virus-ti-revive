# uninstall.ps1 - remove the native Virus TI plug-in and ASIO driver (run through uninstall.cmd, as administrator)
$dir = "$env:ProgramW6432\virus-ti-native"
if (Test-Path "$dir\VirusTINative_ASIO.dll") { Start-Process regsvr32.exe -ArgumentList '/s', '/u', "`"$dir\VirusTINative_ASIO.dll`"" -Wait }
Remove-Item "$env:CommonProgramW6432\VST3\VirusTINative.vst3" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item "$dir\VirusTINative_ASIO.dll*" -Force -ErrorAction SilentlyContinue
Remove-Item "$env:LOCALAPPDATA\virus-ti-native\diag.on" -Force -ErrorAction SilentlyContinue
Write-Host "Removed. (Logs and cache stay in $env:LOCALAPPDATA\virus-ti-native; TI Revive is not touched.)"
