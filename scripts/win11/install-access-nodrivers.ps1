# install-access-nodrivers.ps1 <path-to-Access-MSI> - install the user's own "Virus TI Software Suite" MSI
# WITHOUT its kernel drivers and WITHOUT the firmware updater (findings E003; Win11 blocks VirusUSB.sys anyway).
# A local copy of the MSI is patched with Windows' Installer COM API: the driver/firmware custom actions get the
# condition "0", so plugins, Virus Control Center and shared files install normally. The original file is not
# modified and nothing is redistributed. Run elevated, with the Virus NOT attached to this machine.
param([Parameter(Mandatory)][string]$Msi)
$ErrorActionPreference = 'Stop'
$work = Join-Path $env:TEMP 'vti-access-msi'
New-Item -ItemType Directory -Force $work | Out-Null
$copy = Join-Path $work 'VirusTI-nodrivers.msi'
Copy-Item $Msi $copy -Force
(Get-Item $copy).IsReadOnly = $false

$skip = 'ConnectDevicesDialog', 'PrepareDriverInstall', 'InstallDrivers', 'FinishDriverInstall', 'InstallRunOnceEntry',
        'ApplyOptimization', 'UpdateFirmware'
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
Select-String -Path $log -Pattern 'InstallDrivers|UpdateFirmware|Installation success|Installation failed|return value 3' |
    Select-Object -Last 8 | ForEach-Object { $_.Line }
