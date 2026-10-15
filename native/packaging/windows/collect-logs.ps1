# collect-logs.ps1 - zip the test logs to the desktop (no admin needed)
$out = "$([Environment]::GetFolderPath('Desktop'))\virus-ti-native-logs-$(Get-Date -f yyyyMMdd-HHmmss).zip"
$files = @(Get-ChildItem "$env:LOCALAPPDATA\virus-ti-native\*.log" -ErrorAction SilentlyContinue) +
         @(Get-ChildItem "$env:TEMP\vtibroker*.log" -ErrorAction SilentlyContinue)
if (-not $files) { Write-Host "no logs found"; exit 1 }
Compress-Archive -Path $files.FullName -DestinationPath $out
Write-Host "logs: $out"
