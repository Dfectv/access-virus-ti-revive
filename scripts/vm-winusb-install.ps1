# vm-winusb-install.ps1 - switch the Virus TI in this Windows machine from the Access driver to WinUSB.
# Run elevated from the folder that holds vti_winusb.inf. Steps:
#   1. self-signed code-signing cert (reused if present), trusted in LocalMachine Root + TrustedPublisher
#   2. catalog for the INF (New-FileCatalog) signed with that cert
#   3. remove the Access driver packages (VirusUSB / vtimidi / vtiaudio) so usbccgp enumerates MI_00..04
#   4. pnputil /add-driver vti_winusb.inf /install, rescan
# Revert in the lab VM: vm.sh revert pre-winusb
param([switch]$KeepAccessDrivers)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $here
$subject = 'CN=virus-ti-re WinUSB (local)'

$cert = Get-ChildItem Cert:\LocalMachine\My | Where-Object { $_.Subject -eq $subject } | Select-Object -First 1
if (-not $cert) {
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $subject -CertStoreLocation Cert:\LocalMachine\My -NotAfter (Get-Date).AddYears(10)
}
foreach ($store in 'Root', 'TrustedPublisher') {
    $s = New-Object System.Security.Cryptography.X509Certificates.X509Store($store, 'LocalMachine')
    $s.Open('ReadWrite'); $s.Add($cert); $s.Close()
}
"cert: $($cert.Thumbprint)"

Remove-Item -ErrorAction SilentlyContinue vti_winusb.cat
New-Item -ItemType Directory -Force pkg | Out-Null
Copy-Item vti_winusb.inf pkg\ -Force
New-FileCatalog -Path pkg -CatalogFilePath pkg\vti_winusb.cat -CatalogVersion 2.0 | Out-Null
$sig = Set-AuthenticodeSignature -FilePath pkg\vti_winusb.cat -Certificate $cert -HashAlgorithm SHA256
"catalog signature: $($sig.Status)"

if (-not $KeepAccessDrivers) {
    $drivers = pnputil /enum-drivers | Out-String
    $blocks = $drivers -split "(\r?\n){2,}"
    foreach ($b in $blocks) {
        if ($b -match '(oem\d+\.inf)') { # first oemNN.inf in the block = published name (pnputil output is localized)
            $oem = $Matches[1]
            if ($b -match '(?i)virususb\.inf|vtimidi\.inf|vtiaudio\.inf') {
                "removing Access package $oem"
                pnputil /delete-driver $oem /uninstall /force | Out-String
            }
        }
    }
}

pnputil /add-driver pkg\vti_winusb.inf /install | Out-String
pnputil /scan-devices | Out-Null
Start-Sleep 5
Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -match 'VID_133E' } |
    Select-Object Status, Class, FriendlyName, InstanceId | Format-Table -AutoSize | Out-String -Width 220
