# install-access-reference.ps1 <path-to-Access-MSI> - install the user's own "Virus TI Software Suite" MSI on the
# Windows 10 reference VM WITH its kernel drivers but WITHOUT the firmware updater (E003: UpdateFirmware =
# CAInstallerHelperDeferred!UpdateTIOS runs on every install/repair; E199: after a driver install the updater came
# up at the next boot). Like scripts/win11/install-access-nodrivers.ps1, a local copy of the MSI is patched with
# Windows' Installer COM API: the updater, its RunOnce entry, the optimisation step and the "connect your devices"
# dialog get the condition "0"; PrepareDriverInstall / InstallDrivers / FinishDriverInstall stay. The original file
# is not modified and nothing is redistributed. Run elevated, with the Virus NOT attached to this machine.
param([Parameter(Mandatory)][string]$Msi)
$ErrorActionPreference = 'Stop'
$work = Join-Path $env:TEMP 'vti-access-msi'
New-Item -ItemType Directory -Force $work | Out-Null
$copy = Join-Path $work 'VirusTI-reference.msi'
Copy-Item $Msi $copy -Force
(Get-Item $copy).IsReadOnly = $false

$skip = 'ConnectDevicesDialog', 'InstallRunOnceEntry', 'ApplyOptimization', 'UpdateFirmware'
$installer = New-Object -ComObject WindowsInstaller.Installer
# explicit [string]/[int]: PowerShell otherwise passes wrapped objects (DISP_E_TYPEMISMATCH)
$db = $installer.GetType().InvokeMember('OpenDatabase', 'InvokeMethod', $null, $installer, @([string]$copy, [int]1)) # 1 = transact
foreach ($table in 'InstallExecuteSequence', 'InstallUISequence') {
    foreach ($a in $skip) {
        $sql = "UPDATE ``$table`` SET ``Condition`` = '0' WHERE ``Action`` = '$a'"
        $view = $db.GetType().InvokeMember('OpenView', 'InvokeMethod', $null, $db, @([string]$sql))
        $view.GetType().InvokeMember('Execute', 'InvokeMethod', $null, $view, $null) | Out-Null
        $view.GetType().InvokeMember('Close', 'InvokeMethod', $null, $view, $null) | Out-Null
        [System.Runtime.InteropServices.Marshal]::ReleaseComObject($view) | Out-Null
    }
}
$db.GetType().InvokeMember('Commit', 'InvokeMethod', $null, $db, $null) | Out-Null
[System.Runtime.InteropServices.Marshal]::ReleaseComObject($db) | Out-Null
[System.Runtime.InteropServices.Marshal]::ReleaseComObject($installer) | Out-Null
# all COM handles must be gone, else the patched MSI stays open and msiexec fails with 1619
$db = $null; $installer = $null; [GC]::Collect(); [GC]::WaitForPendingFinalizers()
"patched copy: $copy (custom actions disabled: $($skip -join ', '))"

$log = Join-Path $work 'install.log'
$p = Start-Process msiexec.exe -ArgumentList "/i `"$copy`" /qn /norestart /l*v `"$log`"" -Wait -PassThru
"msiexec exit code: $($p.ExitCode) (0 = ok, 3010 = ok/reboot)"
Select-String -Path $log -Pattern 'InstallDrivers|UpdateFirmware|InstallRunOnceEntry|Installation success|Installation failed|return value 3' |
    Select-Object -Last 10 | ForEach-Object { $_.Line }
# The MSI's own driver step fails silently here (DIFx e0000242): /qn cannot show Windows' "install software from
# Kemper GmbH?" prompt. Trust the catalogs' signer (what that prompt's "always trust" does) and add the three
# drivers from the installed folders with pnputil. Checked on win10ref 2026-10-20: all three catalogs Valid.
$dir = 'C:\Program Files\Access Music\Virus TI'
$store = New-Object System.Security.Cryptography.X509Certificates.X509Store('TrustedPublisher', 'LocalMachine')
$store.Open('ReadWrite')
foreach ($d in 'Usbdriver\VirusUSB', 'Audiodriver\vtiaudio', 'Mididriver\vtimidi') {
    $sig = Get-AuthenticodeSignature "$dir\$d.cat"
    "$d.cat: $($sig.Status) $($sig.SignerCertificate.Subject)"
    if ($sig.Status -ne 'Valid') { throw "catalog $d.cat is not validly signed" }
    $store.Add($sig.SignerCertificate)
}
$store.Close()
foreach ($d in 'Usbdriver\VirusUSB', 'Audiodriver\vtiaudio', 'Mididriver\vtimidi') {
    pnputil /add-driver "$dir\$d.inf" /install | Out-String
}
# the RunOnce key must not start anything Access-related at the next logon
Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\RunOnce', 'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\RunOnce' -ErrorAction SilentlyContinue |
    Format-List | Out-String
