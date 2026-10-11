# tirevive-driver.ps1 install|uninstall - driver part of the TI Revive MSI (deferred custom action, SYSTEM).
# install:   bind Windows' own WinUSB driver to the Virus TI MIDI interfaces (vti_winusb.inf, no kernel code of ours):
#            self-signed code-signing cert -> catalog for the INF -> signature -> cert's PUBLIC part trusted in
#            LocalMachine Root + TrustedPublisher -> the private key is deleted (cannot sign anything else) ->
#            pnputil /add-driver /install. Removes Access driver packages (blocked on Win11 anyway).
# uninstall: pnputil /delete-driver for our package, remove the trusted cert.
# State: HKLM\SOFTWARE\TI Revive (DriverPackage, CertThumbprint). Log: %ProgramData%\TI Revive\driver.log
param([ValidateSet('install', 'uninstall')][string]$Action = 'install')
$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$logDir = Join-Path $env:ProgramData 'TI Revive'
New-Item -ItemType Directory -Force $logDir | Out-Null
Start-Transcript -Path (Join-Path $logDir 'driver.log') -Append | Out-Null
$reg = 'HKLM:\SOFTWARE\TI Revive'
New-Item -Path $reg -Force | Out-Null

function Trust-Store([string]$name, $cert, [switch]$Remove) {
    $s = New-Object System.Security.Cryptography.X509Certificates.X509Store($name, 'LocalMachine')
    $s.Open('ReadWrite')
    if ($Remove) { $s.Certificates | Where-Object Thumbprint -eq $cert | ForEach-Object { $s.Remove($_) } }
    else { $s.Add($cert) }
    $s.Close()
}

if ($Action -eq 'install') {
    # Access driver packages would be preferred for the device and are blocked by Code Integrity on Win11.
    $blocks = (pnputil /enum-drivers | Out-String) -split "(\r?\n){2,}"
    foreach ($b in $blocks) {
        if ($b -match '(oem\d+\.inf)' -and $b -match '(?i)virususb\.inf|vtimidi\.inf|vtiaudio\.inf') {
            "removing Access driver package $($Matches[1])"
            pnputil /delete-driver $Matches[1] /uninstall /force | Out-String
        }
    }
    $stage = Join-Path $env:TEMP ('tirevive-' + [guid]::NewGuid())
    New-Item -ItemType Directory $stage | Out-Null
    Copy-Item (Join-Path $here 'vti_winusb.inf') $stage
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=TI Revive local driver package' `
        -CertStoreLocation Cert:\LocalMachine\My -NotAfter (Get-Date).AddYears(20) -KeyExportPolicy NonExportable
    # trust the public part first, so the signature status below is "Valid" (not "UnknownError")
    $pub = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2(, $cert.Export('Cert'))
    Trust-Store 'Root' $pub
    Trust-Store 'TrustedPublisher' $pub
    New-FileCatalog -Path $stage -CatalogFilePath (Join-Path $stage 'vti_winusb.cat') -CatalogVersion 2.0 | Out-Null
    $sig = Set-AuthenticodeSignature -FilePath (Join-Path $stage 'vti_winusb.cat') -Certificate $cert -HashAlgorithm SHA256
    "catalog signature: $($sig.Status)"
    Remove-Item "Cert:\LocalMachine\My\$($cert.Thumbprint)" -DeleteKey # private key gone: signs nothing else
    Set-ItemProperty $reg CertThumbprint $cert.Thumbprint
    $out = pnputil /add-driver (Join-Path $stage 'vti_winusb.inf') /install | Out-String
    $out
    if ($out -match '(oem\d+\.inf)') { Set-ItemProperty $reg DriverPackage $Matches[1] }
    pnputil /scan-devices | Out-Null
    Remove-Item $stage -Recurse -Force
} else {
    $p = Get-ItemProperty $reg -ErrorAction SilentlyContinue
    Get-Process vtibroker -ErrorAction SilentlyContinue | Stop-Process -Force
    if ($p.DriverPackage) { pnputil /delete-driver $p.DriverPackage /uninstall /force | Out-String }
    if ($p.CertThumbprint) {
        Trust-Store 'Root' $p.CertThumbprint -Remove
        Trust-Store 'TrustedPublisher' $p.CertThumbprint -Remove
    }
    Remove-Item $reg -Recurse -Force -ErrorAction SilentlyContinue
}
Stop-Transcript | Out-Null
exit 0
