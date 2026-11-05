$ErrorActionPreference='Stop'
$source=Join-Path $PSScriptRoot 'release\battery_udp_host_probe.dll'
$destination='C:\Program Files\MWORKS\Sysplorer 2026a\Bin64\addins\BatteryUdpHostProbe'
$record=Join-Path $PSScriptRoot 'deploy_status.json'
try {
  if((Get-FileHash -LiteralPath $source).Hash -ne '4402FDDFA0E87DC575120E8F98BFF11F6C3ED521C41BFA4DC30F600091162F3C') {throw 'Source hash mismatch'}
  $target=Join-Path $destination 'battery_udp_host_probe.dll'
  if(Test-Path -LiteralPath $target) {throw 'Destination already exists; refusing overwrite'}
  New-Item -ItemType Directory -Path $destination -Force | Out-Null
  Copy-Item -LiteralPath $source -Destination $target -ErrorAction Stop
  if((Get-FileHash -LiteralPath $target).Hash -ne (Get-FileHash -LiteralPath $source).Hash) {throw 'Deployed hash mismatch'}
  @{ok=$true;target=$target;sha256=(Get-FileHash -LiteralPath $target).Hash}|ConvertTo-Json|Set-Content -LiteralPath $record -Encoding utf8
} catch {
  @{ok=$false;error=$_.Exception.Message}|ConvertTo-Json|Set-Content -LiteralPath $record -Encoding utf8
  exit 1
}
