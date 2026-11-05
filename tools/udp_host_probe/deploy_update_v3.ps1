$ErrorActionPreference='Stop'
$source=Join-Path $PSScriptRoot 'release\battery_udp_host_probe.dll'
$target='C:\Program Files\MWORKS\Sysplorer 2026a\Bin64\addins\BatteryUdpHostProbe\battery_udp_host_probe.dll'
try {
 if((Get-FileHash -LiteralPath $source).Hash -ne '7CDE29D56AD2EC86527ADE3DD90C1535222DCD8658344949484F8134109F1E9D') {throw 'Source hash mismatch'}
 if((Get-FileHash -LiteralPath $target).Hash -ne '8F94A30C87904A5D98CB6EB92B33662617097620DF198F1F6EDB722F7006C47F') {throw 'Unexpected destination; refusing overwrite'}
 Copy-Item -LiteralPath $source -Destination $target -Force
 @{ok=$true;sha256=(Get-FileHash -LiteralPath $target).Hash}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $PSScriptRoot 'deploy_v3_status.json') -Encoding utf8
} catch { @{ok=$false;error=$_.Exception.Message}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $PSScriptRoot 'deploy_v3_status.json') -Encoding utf8; exit 1 }

