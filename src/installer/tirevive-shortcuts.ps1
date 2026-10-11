# tirevive-shortcuts.ps1 install|uninstall - make the Access "Virus Control Center" shortcuts go through TI Revive.
# The Access MSI creates advertised shortcuts (target ...\Installer\{GUID}\CCIcon.exe) that start Control Center
# without the bridge ("no Virus found", findings E061); they cannot be retargeted, so they are replaced.
# install:   every all-users Start menu / public desktop "Virus Control Center*.lnk" (not ours) -> vtilaunch.exe
# uninstall: those shortcuts -> Virus Control Center.exe directly (as before TI Revive, minus advertising)
# Needs admin. Called by tirevive-driver.ps1 (MSI install/uninstall) and by tirevive-access.ps1 after the Access
# software has been installed.
param([ValidateSet('install', 'uninstall')][string]$Action = 'install')
$cc = Join-Path ${env:ProgramFiles} 'Access Music\Virus TI\Common\Virus Control Center.exe'
if (-not (Test-Path $cc)) { "Access Virus Control Center not installed: nothing to do"; exit 0 }
$launcher = Join-Path $PSScriptRoot 'vtilaunch.exe'
$ws = New-Object -ComObject WScript.Shell
$roots = (Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs'), (Join-Path $env:PUBLIC 'Desktop')
Get-ChildItem $roots -Recurse -Filter 'Virus Control Center*.lnk' -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -notmatch '\\TI Revive\\' } | ForEach-Object {
        $path = $_.FullName
        Remove-Item $path -Force
        $l = $ws.CreateShortcut($path)
        if ($Action -eq 'install') {
            $l.TargetPath = $launcher
            $l.Arguments = '"' + $cc + '"'
            $l.WorkingDirectory = $PSScriptRoot
            $l.Description = 'Virus Control Center through TI Revive'
        } else {
            $l.TargetPath = $cc
            $l.Arguments = ''
            $l.WorkingDirectory = Split-Path -Parent $cc
            $l.Description = 'Virus Control Center'
        }
        $l.IconLocation = "$cc,0"
        $l.Save()
        "$Action shortcut: $path"
    }
exit 0
