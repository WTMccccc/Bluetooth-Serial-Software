param([switch]$Unpair)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Runtime.WindowsRuntime

$asTask = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
    $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
})[0]

function Await($op, $type, $timeoutMs) {
    $task = $asTask.MakeGenericMethod($type).Invoke($null, @($op))
    if (-not $task.Wait($timeoutMs)) { return $null }
    return $task.Result
}

$addr = [uint64]0xC42507090CC7
$btType = [Windows.Devices.Bluetooth.BluetoothLEDevice, Windows.Devices.Bluetooth, ContentType = WindowsRuntime]
$unpairType = [Windows.Devices.Enumeration.DeviceUnpairingResult, Windows.Devices.Enumeration, ContentType = WindowsRuntime]

$dev = Await ($btType::FromBluetoothAddressAsync($addr)) $btType 12000
if ($null -eq $dev) { Write-Output "device = null"; exit 2 }

$di = $dev.DeviceInformation
Write-Output ("name:   " + $di.Name)
Write-Output ("id:     " + $di.Id)
Write-Output ("paired: " + $di.Pairing.IsPaired + "  canPair: " + $di.Pairing.CanPair)

if ($Unpair) {
    if (-not $di.Pairing.IsPaired) { Write-Output "not paired, nothing to do"; exit 0 }
    Write-Output "UnpairAsync..."
    $res = Await ($di.Pairing.UnpairAsync()) $unpairType 20000
    if ($null -eq $res) { Write-Output "UnpairAsync timeout"; exit 3 }
    Write-Output ("unpair status: " + $res.Status)
    Start-Sleep -Seconds 1
    $dev2 = Await ($btType::FromBluetoothAddressAsync($addr)) $btType 12000
    if ($dev2) { Write-Output ("paired now: " + $dev2.DeviceInformation.Pairing.IsPaired) }
}
