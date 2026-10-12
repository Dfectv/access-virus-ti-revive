# tirevive-access.ps1 - install the user's own Access "Virus TI Software Suite" MSI without its kernel drivers and
# without the firmware updater (launched from the TI Revive exit dialog / Start menu). The Access MSI is looked
# for next to the TI Revive MSI, in Downloads, or picked with a file dialog. A local copy is patched (driver and
# firmware custom actions get the condition "0") and installed; nothing of Access is shipped with TI Revive.
param([string]$Msi, [string]$SourceDir, [string]$FromMsi) # FromMsi: path of the TI Revive MSI ([OriginalDatabase])
# from the setup.exe the folder ends with '\', which escapes the closing quote on the command line: C:\dir"
if ($SourceDir) { $SourceDir = $SourceDir.Trim().Trim('"').TrimEnd('\') }
if ($FromMsi -and -not $SourceDir) { $SourceDir = Split-Path -Parent $FromMsi }
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
$title = 'TI Revive'
function Say([string]$text, [string]$icon = 'Information') {
    [System.Windows.Forms.MessageBox]::Show($text, $title, 'OK', $icon) | Out-Null
}

# start the broker for this session if it is not running yet (it is started on demand and exits when idle)
$broker = Join-Path $PSScriptRoot 'vtibroker.exe'
if (-not (Get-Process vtibroker -ErrorAction SilentlyContinue) -and (Test-Path $broker)) {
    Start-Process $broker -WindowStyle Hidden
}

if (-not $Msi) {
    $dirs = @($SourceDir, (Join-Path $env:USERPROFILE 'Downloads'), (Join-Path $env:USERPROFILE 'Desktop')) | Where-Object { $_ }
    $Msi = $dirs | ForEach-Object { Get-ChildItem $_ -Filter '*Virus TI*.msi' -ErrorAction SilentlyContinue } |
        Where-Object Name -NotMatch 'TI Revive' | Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $Msi) {
    $dlg = New-Object System.Windows.Forms.OpenFileDialog
    $dlg.Title = 'Select the Access "Virus TI Software Suite" installer (64-bit .msi)'
    $dlg.Filter = 'Windows Installer (*.msi)|*.msi'
    if ($dlg.ShowDialog() -ne 'OK') { exit 1 }
    $Msi = $dlg.FileName
}

$work = Join-Path $env:TEMP 'tirevive-access'
New-Item -ItemType Directory -Force $work | Out-Null
$copy = Join-Path $work 'VirusTI-without-drivers.msi'
Copy-Item $Msi $copy -Force
(Get-Item $copy).IsReadOnly = $false
$skip = 'ConnectDevicesDialog', 'PrepareDriverInstall', 'InstallDrivers', 'FinishDriverInstall', 'InstallRunOnceEntry',
        'ApplyOptimization', 'UpdateFirmware'
$installer = New-Object -ComObject WindowsInstaller.Installer
$db = $installer.GetType().InvokeMember('OpenDatabase', 'InvokeMethod', $null, $installer, @([string]$copy, [int]1))
foreach ($table in 'InstallExecuteSequence', 'InstallUISequence') {
    foreach ($a in $skip) {
        $view = $db.GetType().InvokeMember('OpenView', 'InvokeMethod', $null, $db,
            @([string]"UPDATE ``$table`` SET ``Condition`` = '0' WHERE ``Action`` = '$a'"))
        $view.GetType().InvokeMember('Execute', 'InvokeMethod', $null, $view, $null) | Out-Null
        $view.GetType().InvokeMember('Close', 'InvokeMethod', $null, $view, $null) | Out-Null
        [System.Runtime.InteropServices.Marshal]::ReleaseComObject($view) | Out-Null
    }
}
$db.GetType().InvokeMember('Commit', 'InvokeMethod', $null, $db, $null) | Out-Null
[System.Runtime.InteropServices.Marshal]::ReleaseComObject($db) | Out-Null
[System.Runtime.InteropServices.Marshal]::ReleaseComObject($installer) | Out-Null
$db = $null; $installer = $null; [GC]::Collect(); [GC]::WaitForPendingFinalizers() # else msiexec: 1619

# launched from the TI Revive exit dialog: that installation may still hold the installer mutex (1618)
$log = Join-Path $work 'install.log'
for ($i = 0; $i -lt 60; $i++) {
    $p = Start-Process msiexec.exe -ArgumentList "/i `"$copy`" /qb /norestart /l*v `"$log`"" -Wait -PassThru
    if ($p.ExitCode -ne 1618) { break }
    Start-Sleep 2
}
# Access' own Control Center shortcuts start it without the bridge: replace them (tirevive-shortcuts.ps1, admin,
# one UAC prompt).
if ($p.ExitCode -in 0, 3010) {
    $sc = Join-Path $PSScriptRoot 'tirevive-shortcuts.ps1'
    Start-Process powershell.exe -Verb RunAs -WindowStyle Hidden -Wait -ErrorAction SilentlyContinue `
        -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$sc`" install"
}
switch ($p.ExitCode) {
    0 { Say 'The Access Virus TI software is installed (without its old drivers). Use the plug-in "Virus TI (TI Revive)" in your DAW.' }
    3010 { Say 'The Access Virus TI software is installed. Please restart Windows.' }
    1602 { Say 'Installation of the Access software was cancelled.' 'Warning' }
    default { Say "Installing the Access software failed (code $($p.ExitCode)). Log: $log" 'Error' }
}
exit $p.ExitCode
