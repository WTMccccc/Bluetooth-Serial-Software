# make_cert.ps1 - create a self-signed code-signing certificate, export it,
# sign build\ble_uart.exe and trust it for the current user.
#
# Usage:  powershell -ExecutionPolicy Bypass -File tools\make_cert.ps1
#         powershell -ExecutionPolicy Bypass -File tools\make_cert.ps1 -Name MyName -Password MyPass
#
# Note: writes to the current-user certificate store, so it must run outside
# the DSH file sandbox (danger-full-access).

param(
    [string]$Name = "WTMcccccccc",
    [string]$Password = "ZZ20020105",
    [string]$Exe = "build\ble_uart.exe",
    [int]$Years = 5
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$certs = Join-Path $root "certs"
New-Item -ItemType Directory -Force -Path $certs | Out-Null

$today = (Get-Date).Date
$notBefore = $today
$notAfter = $today.AddYears($Years).AddDays(1).AddSeconds(-1)

Write-Output ("Creating certificate CN=" + $Name)
$cert = New-SelfSignedCertificate `
    -Type CodeSigningCert `
    -Subject ("CN=" + $Name) `
    -FriendlyName ($Name + " Code Signing") `
    -NotBefore $notBefore `
    -NotAfter $notAfter `
    -CertStoreLocation "Cert:\CurrentUser\My" `
    -KeyExportPolicy Exportable `
    -KeyLength 2048 `
    -HashAlgorithm SHA256

$pfx = Join-Path $certs ($Name + ".pfx")
$cer = Join-Path $certs ($Name + ".cer")
$secure = ConvertTo-SecureString -String $Password -Force -AsPlainText
Export-PfxCertificate -Cert $cert -FilePath $pfx -Password $secure | Out-Null
Export-Certificate -Cert $cert -FilePath $cer | Out-Null
Write-Output ("Exported: " + $pfx)
Write-Output ("Exported: " + $cer)

# --- sign ---------------------------------------------------------------
$signtool = Get-ChildItem "D:\Windows Kits\10\bin" -Recurse -Filter "signtool.exe" -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match "\\x64\\" } |
    Sort-Object FullName -Descending |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $signtool) { throw "signtool.exe not found in Windows Kits" }

Write-Output ("Signing " + $Exe + " with " + $signtool)
& $signtool sign /fd SHA256 /f $pfx /p $Password $Exe
if ($LASTEXITCODE -ne 0) { throw ("signtool failed: " + $LASTEXITCODE) }

# --- trust for this user -------------------------------------------------
Import-Certificate -FilePath $cer -CertStoreLocation "Cert:\CurrentUser\TrustedPublisher" | Out-Null
Write-Output "Installed into CurrentUser\TrustedPublisher"

$rootOk = $true
try {
    Import-Certificate -FilePath $cer -CertStoreLocation "Cert:\CurrentUser\Root" | Out-Null
    Write-Output "Installed into CurrentUser\Root"
} catch {
    $rootOk = $false
    Write-Output ("ROOT_IMPORT_FAILED: " + $_.Exception.Message)
}

# --- verify --------------------------------------------------------------
$sig = Get-AuthenticodeSignature (Join-Path $root $Exe)
Write-Output ("SIGNATURE_STATUS=" + $sig.Status)
if ($sig.SignerCertificate) {
    Write-Output ("SIGNER=" + $sig.SignerCertificate.Subject)
}
Write-Output ("CERT_THUMBPRINT=" + $cert.Thumbprint)
Write-Output ("CERT_NOTBEFORE=" + $cert.NotBefore.ToString("yyyy-MM-dd HH:mm:ss"))
Write-Output ("CERT_NOTAFTER=" + $cert.NotAfter.ToString("yyyy-MM-dd HH:mm:ss"))
Write-Output ("PFX_PASSWORD=" + $Password)
Write-Output ("ROOT_TRUSTED=" + $rootOk)
