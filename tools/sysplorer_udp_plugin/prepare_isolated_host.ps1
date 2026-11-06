param([string]$SysplorerHome='C:\Program Files\MWORKS\Sysplorer 2026a',
      [string]$Destination=(Join-Path $env:TEMP 'BatteryConsoleRuntime'))
$ErrorActionPreference='Stop'
$isolatedHome=[IO.Path]::GetFullPath($Destination)
if($isolatedHome -match '[^\x00-\x7F]'){throw 'Sysplorer runtime must use an ASCII path'}
$isolatedBin=Join-Path $isolatedHome 'Bin64'
New-Item -ItemType Directory -Path $isolatedBin -Force|Out-Null
# Copy executables and plugins; no files in the installed application are changed.
Get-ChildItem -LiteralPath (Join-Path $SysplorerHome 'Bin64') -File|ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $isolatedBin $_.Name) -Force
}
Get-ChildItem -LiteralPath (Join-Path $SysplorerHome 'Bin64') -Directory|ForEach-Object {
    $dest=Join-Path $isolatedBin $_.Name
    if($_.Name -eq 'addins') {Copy-Item -LiteralPath $_.FullName -Destination $isolatedBin -Recurse -Force}
    elseif(-not(Test-Path -LiteralPath $dest)){New-Item -ItemType Junction -Path $dest -Target $_.FullName|Out-Null}
}
Get-ChildItem -LiteralPath $SysplorerHome -Directory|Where-Object Name -ne 'Bin64'|ForEach-Object {
    $dest=Join-Path $isolatedHome $_.Name
    if($_.Name -eq 'Setting'){Copy-Item -LiteralPath $_.FullName -Destination $isolatedHome -Recurse -Force}
    elseif(-not(Test-Path -LiteralPath $dest)){New-Item -ItemType Junction -Path $dest -Target $_.FullName|Out-Null}
}
$plugin=Join-Path $isolatedBin 'addins\BatteryUdpControl\battery_udp_control.dll'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'release\battery_udp_control.dll') -Destination $plugin -Force
if((Get-FileHash -LiteralPath $plugin).Hash -ne (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'release\battery_udp_control.dll')).Hash){throw 'Isolated plugin hash mismatch'}
Write-Output $isolatedHome
