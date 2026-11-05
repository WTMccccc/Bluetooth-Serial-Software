# sign.ps1 - re-sign build\ble_uart.exe with the self-signed certificate.
#
# Usage: sign.bat
#        powershell -ExecutionPolicy Bypass -File tools\sign.ps1 -Exe build\ble_uart.exe

param(
    [string]$Exe = "build\ble_uart.exe",
    [string]$Pfx = "certs\WTMcccccccc.pfx",
    [string]$Password = "ZZ20020105"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

if (-not (Test-Path $Pfx)) {
    throw ("PFX not found: " + $Pfx + " (run tools\make_cert.ps1 first)")
}
if (-not (Test-Path $Exe)) {
    throw ("EXE not found: " + $Exe + " (run build.bat first)")
}

$kits = (Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots" -ErrorAction SilentlyContinue).KitsRoot10
$signtool = $null
if ($kits) {
    $signtool = Get-ChildItem (Join-Path $kits "bin") -Recurse -Filter "signtool.exe" -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match "\\x64\\" } |
        Sort-Object FullName -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $signtool) {
    $fallback = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin"
    $signtool = Get-ChildItem $fallback -Recurse -Filter "signtool.exe" -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match "\\x64\\" } |
        Sort-Object FullName -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $signtool) {
    throw "signtool.exe not found (install Windows 10 SDK)"
}

Write-Output ("Signing " + $Exe)
& $signtool sign /fd SHA256 /f $Pfx /p $Password $Exe
if ($LASTEXITCODE -ne 0) { throw ("signtool failed: " + $LASTEXITCODE) }

$sig = Get-AuthenticodeSignature (Join-Path $root $Exe)
Write-Output ("Status: " + $sig.Status)
if ($sig.SignerCertificate) {
    Write-Output ("Signer: " + $sig.SignerCertificate.Subject)
    Write-Output ("Thumbprint: " + $sig.SignerCertificate.Thumbprint)
}
