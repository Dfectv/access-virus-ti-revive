# tirevive-access.ps1 - provide the files of the user's own Access "Virus TI Software Suite" that TI Revive's plug-in
# reads, WITHOUT installing the Access software (launched from the TI Revive exit dialog / Start menu):
#   Virus Control.dll (skin + device description) and firmware.bin (ROM banks) -> Program Files\Access Music\Virus TI\
#   Common (where the plug-in looks), the manuals -> ...\Virus TI\Documentation, the factory patch libraries ->
#   Documents\Access Music\Virus TI\Patches (only files that are not there yet; the user's own libraries stay).
# The .msi is unpacked (administrative extraction: no custom actions, no drivers, nothing registered). Nothing of Access
# is shipped with TI Revive. A full Access installation (TI Revive 0.7 and older installed it: original plug-in, Access
# ASIO and USB drivers, Control Center) is offered for removal; the files above stay.
# -Quiet: no dialogs (tests): remove a full Access installation, report on stdout.
param([string]$Msi, [string]$SourceDir, [string]$FromMsi, [switch]$Quiet,
      [switch]$Elevated, [string]$Extracted, [switch]$RemoveAccess) # -Elevated: the admin part (one UAC prompt)
# from the setup.exe the folder ends with '\', which escapes the closing quote on the command line: C:\dir"
if ($SourceDir) { $SourceDir = $SourceDir.Trim().Trim('"').TrimEnd('\') }
if ($FromMsi -and -not $SourceDir) { $SourceDir = Split-Path -Parent $FromMsi }
$ErrorActionPreference = 'Stop'
$title = 'TI Revive'
$target = Join-Path $env:ProgramW6432 'Access Music\Virus TI'
$needed = 'Virus Control.dll', 'firmware.bin' # in ...\Common
$optional = 'Virus Control Center.exe'      # ...\Common: the TI Revive Control Center takes its look from it
function Say([string]$text, [string]$icon = 'Information') {
    if ($Quiet) { Write-Host "$icon`: $text"; return }
    Add-Type -AssemblyName System.Windows.Forms
    [System.Windows.Forms.MessageBox]::Show($text, $title, 'OK', $icon) | Out-Null
}
function Ask([string]$text) {
    if ($Quiet) { Write-Host "Question (yes): $text"; return $true }
    Add-Type -AssemblyName System.Windows.Forms
    return [System.Windows.Forms.MessageBox]::Show($text, $title, 'YesNo', 'Question') -eq 'Yes'
}
function AccessProduct {
    Get-ItemProperty HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*,
                     HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\* -ErrorAction SilentlyContinue |
        Where-Object { $_.DisplayName -match '^Virus TI Software Suite' -and $_.Publisher -match 'Access' } | Select-Object -First 1
}
function HaveFiles { -not ($needed | Where-Object { -not (Test-Path (Join-Path $target "Common\$_")) }) }

# ---- the admin part: remove the Access installation (keeping the files), copy the extracted files -----------------
if ($Elevated) {
    $keep = Join-Path $env:TEMP 'tirevive-access-keep'
    if ($RemoveAccess -and ($p = AccessProduct)) {
        Remove-Item -Recurse -Force $keep -ErrorAction SilentlyContinue
        New-Item -ItemType Directory -Force "$keep\Common" | Out-Null
        foreach ($f in $needed + $optional) { Copy-Item (Join-Path $target "Common\$f") "$keep\Common\" -ErrorAction SilentlyContinue }
        if (Test-Path "$target\Documentation") { Copy-Item -Recurse "$target\Documentation" $keep }
        $r = Start-Process msiexec.exe -ArgumentList "/x $($p.PSChildName) /qb /norestart" -Wait -PassThru
        Write-Host "removing the Access installation: $($r.ExitCode)"
        New-Item -ItemType Directory -Force "$target\Common" | Out-Null
        Copy-Item "$keep\Common\*" "$target\Common\" -Force -ErrorAction SilentlyContinue
        if (Test-Path "$keep\Documentation") { Copy-Item -Recurse -Force "$keep\Documentation" $target }
        Remove-Item -Recurse -Force $keep -ErrorAction SilentlyContinue
    }
    # Access' uninstaller leaves its ASIO entry behind: a dead "Virus USB ASIO driver" in every DAW's device list
    $asio = 'HKLM:\SOFTWARE\ASIO\Virus USB ASIO driver'
    if (Test-Path $asio) {
        $clsid = (Get-ItemProperty $asio -ErrorAction SilentlyContinue).CLSID
        $dll = if ($clsid) { (Get-ItemProperty "HKLM:\SOFTWARE\Classes\CLSID\$clsid\InprocServer32" -ErrorAction SilentlyContinue).'(default)' }
        if (-not $dll -or -not (Test-Path $dll)) { Remove-Item -Recurse -Force $asio; Write-Host 'removed the dead Access ASIO entry' }
    }
    if ($Extracted) {
        $src = Join-Path $Extracted 'Access Music\Virus TI'
        New-Item -ItemType Directory -Force "$target\Common" | Out-Null
        foreach ($f in $needed) { Copy-Item (Join-Path $src "Common\$f") "$target\Common\" -Force }
        foreach ($f in $optional) { Copy-Item (Join-Path $src "Common\$f") "$target\Common\" -Force -ErrorAction SilentlyContinue }
        if (Test-Path "$src\Documentation") { Copy-Item -Recurse -Force "$src\Documentation" $target }
    }
    exit 0
}

# ---- the user part --------------------------------------------------------------------------------------------------
$product = AccessProduct
$remove = $false
if ($product) {
    $remove = Ask ("The full Access Virus TI software is installed (from an earlier TI Revive): the original plug-in " +
        "`"Virus TI`", the Access ASIO and USB drivers and the Control Center. TI Revive no longer uses them.`n`n" +
        "Remove it? The files TI Revive's plug-in needs (skin, ROM banks, manuals) and your patch libraries stay.")
}
$extracted = $null
# set up before 0.8.2, the Control Center's look was never copied (only the needed files decide): unpack the Access
# .msi again when one is at hand, without asking for it (E184)
$needed_missing = -not (HaveFiles)
$look_missing = -not ($optional | Where-Object { Test-Path (Join-Path $target "Common\$_") })
if ($needed_missing -or $look_missing) {
    if (-not $Msi) {
        $dirs = @($SourceDir, (Join-Path $env:USERPROFILE 'Downloads'), (Join-Path $env:USERPROFILE 'Desktop')) | Where-Object { $_ }
        $Msi = $dirs | ForEach-Object { Get-ChildItem $_ -Filter '*Virus TI*.msi' -ErrorAction SilentlyContinue } |
            Where-Object Name -NotMatch 'TI Revive' | Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName
    }
    if (-not $Msi -and -not $Quiet -and $needed_missing) {
        Add-Type -AssemblyName System.Windows.Forms
        $dlg = New-Object System.Windows.Forms.OpenFileDialog
        $dlg.Title = 'Select the Access "Virus TI Software Suite" installer (64-bit .msi)'
        $dlg.Filter = 'Windows Installer (*.msi)|*.msi'
        if ($dlg.ShowDialog() -eq 'OK') { $Msi = $dlg.FileName }
    }
    if (-not $Msi -and $needed_missing) { Say 'No Access "Virus TI Software Suite" .msi selected: nothing was set up.' 'Warning'; exit 1 }
}
if ($Msi -and ($needed_missing -or $look_missing)) {
    $extracted = Join-Path $env:TEMP 'tirevive-access-extract'
    Remove-Item -Recurse -Force $extracted -ErrorAction SilentlyContinue
    for ($i = 0; $i -lt 60; $i++) { # launched from the TI Revive exit dialog: that installation may still hold the mutex
        $p = Start-Process msiexec.exe -ArgumentList "/a `"$Msi`" /qn TARGETDIR=`"$extracted`"" -Wait -PassThru
        if ($p.ExitCode -ne 1618) { break }
        Start-Sleep 2
    }
    if ($p.ExitCode -ne 0 -or -not (Test-Path (Join-Path $extracted 'Access Music\Virus TI\Common\Virus Control.dll'))) {
        Say "Unpacking the Access installer failed (code $($p.ExitCode)). Is it the 64-bit `"Virus TI Software Suite`" .msi?" 'Error'
        exit 1
    }
    # the factory patch libraries into the user's Documents (no admin needed; existing files stay untouched)
    $docs = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Access Music\Virus TI\Patches'
    $lib = Join-Path $extracted 'PersonalFiles\Access Music\Virus TI\Patches'
    if (Test-Path $lib) {
        Get-ChildItem $lib -Recurse -File | ForEach-Object {
            $dst = Join-Path $docs $_.FullName.Substring($lib.Length).TrimStart('\')
            if (-not (Test-Path $dst)) {
                New-Item -ItemType Directory -Force (Split-Path -Parent $dst) | Out-Null
                Copy-Item $_.FullName $dst
            }
        }
    }
}
# the patch libraries in Documents survive the removal of the Access software (it would take its factory files along)
$docs = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Access Music\Virus TI\Patches'
$backup = Join-Path $env:TEMP 'tirevive-access-patches'
if ($remove -and (Test-Path $docs)) {
    Remove-Item -Recurse -Force $backup -ErrorAction SilentlyContinue
    Copy-Item -Recurse $docs $backup
}
if ($remove -or $extracted) { # one UAC prompt for both
    $argline = "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$PSCommandPath`" -Elevated"
    if ($remove) { $argline += ' -RemoveAccess' }
    if ($extracted) { $argline += " -Extracted `"$extracted`"" }
    if ($Quiet) { & $PSCommandPath -Elevated -RemoveAccess:$remove -Extracted $extracted }
    else { Start-Process powershell.exe -Verb RunAs -Wait -WindowStyle Hidden -ArgumentList $argline }
}
if (Test-Path $backup) {
    Get-ChildItem $backup -Recurse -File | ForEach-Object {
        $dst = Join-Path $docs $_.FullName.Substring($backup.Length).TrimStart('\')
        if (-not (Test-Path $dst)) {
            New-Item -ItemType Directory -Force (Split-Path -Parent $dst) | Out-Null
            Copy-Item $_.FullName $dst
        }
    }
    Remove-Item -Recurse -Force $backup -ErrorAction SilentlyContinue
}
if ($extracted) { Remove-Item -Recurse -Force $extracted -ErrorAction SilentlyContinue }
if (HaveFiles) {
    $msg = 'The Access Virus TI files are set up. Use the plug-in "TI Revive Virus Control" in your DAW'
    if ($remove -and -not (AccessProduct)) { $msg += '; the old Access software was removed' }
    Say "$msg."
    exit 0
}
Say 'The Access Virus TI files are still missing (was the admin prompt declined?). Run "Set up Access Virus TI files" from the Start menu again.' 'Warning'
exit 1
