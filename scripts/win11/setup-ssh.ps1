# setup-ssh.ps1 - first-logon step of the Windows 11 test VM (autounattend.xml): OpenSSH server with the
# virus-ti-re key, so scripts/vm.sh (VM=win11) can drive the VM like the Win10 one. Log: C:\setup-ssh.log
$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
"setup-ssh start $(Get-Date)"
$cap = Get-WindowsCapability -Online -Name 'OpenSSH.Server*'
"capability: $($cap.Name) $($cap.State)"
if ($cap.State -ne 'Installed') {
    for ($i = 0; $i -lt 30; $i++) { # needs Windows Update / network: retry while the network comes up
        try { Add-WindowsCapability -Online -Name $cap.Name -ErrorAction Stop | Out-String; break }
        catch { "retry $i : $_"; Start-Sleep 20 }
    }
}
Set-Service sshd -StartupType Automatic
Start-Service sshd
if (-not (Get-NetFirewallRule -Name 'OpenSSH-Server-In-TCP' -ErrorAction SilentlyContinue)) {
    New-NetFirewallRule -Name 'OpenSSH-Server-In-TCP' -DisplayName 'OpenSSH Server (sshd)' -Enabled True -Direction Inbound -Protocol TCP -Action Allow -LocalPort 22 | Out-Null
}
# vti is an administrator: sshd reads administrators_authorized_keys, which must only be writable by SYSTEM/Admins
$keys = 'C:\ProgramData\ssh\administrators_authorized_keys'
Copy-Item (Join-Path $here 'vm_re.pub') $keys -Force
icacls $keys /inheritance:r /grant 'SYSTEM:F' /grant '*S-1-5-32-544:F' | Out-String
# PowerShell as the SSH shell, like on the Win10 VM
New-ItemProperty -Path 'HKLM:\SOFTWARE\OpenSSH' -Name DefaultShell -Value 'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe' -PropertyType String -Force | Out-Null
Restart-Service sshd
"sshd: $((Get-Service sshd).Status)"
"setup-ssh done $(Get-Date)"
