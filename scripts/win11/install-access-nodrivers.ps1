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
$db = $installer.GetType().InvokeMember('OpenDatabase', 'InvokeMethod', $null, $installer, @($copy, 1)) # 1 = transact
foreach ($table in 'InstallExecuteSequence', 'InstallUISequence') {
    foreach ($a in $skip) {
        $sql = "UPDATE ``$table`` SET ``Condition`` = '0' WHERE ``Action`` = '$a'"
        $view = $db.GetType().InvokeMember('OpenView', 'InvokeMethod', $null, $db, @($sql))
        $view.GetType().InvokeMember('Execute', 'InvokeMethod', $null, $view, $null) | Out-Null
        $view.GetType().InvokeMember('Close', 'InvokeMethod', $null, $view, $null) | Out-Null
    }
}
$db.GetType().InvokeMember('Commit', 'InvokeMethod', $null, $db, $null) | Out-Null
[System.Runtime.InteropServices.Marshal]::ReleaseComObject($db) | Out-Null
"patched copy: $copy (custom actions disabled: $($skip -join ', '))"

$log = Join-Path $work 'install.log'
$p = Start-Process msiexec.exe -ArgumentList "/i `"$copy`" /qn /norestart /l*v `"$log`"" -Wait -PassThru
"msiexec exit code: $($p.ExitCode) (0 = ok, 3010 = ok/reboot)"
Select-String -Path $log -Pattern 'InstallDrivers|UpdateFirmware|Installation success|Installation failed|return value 3' |
    Select-Object -Last 8 | ForEach-Object { $_.Line }
