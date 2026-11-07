param([string]$PluginDll='')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$testRoot=Join-Path $root ('logs\connection-fix\update-'+[guid]::NewGuid().ToString('N'))
$package=Join-Path $testRoot 'package'
$fixtureHome=Join-Path $testRoot 'host'
$addins=Join-Path $fixtureHome 'Bin64\addins\BatteryUdpControl'
[IO.Directory]::CreateDirectory($package)|Out-Null
[IO.Directory]::CreateDirectory($addins)|Out-Null
$source=if($PluginDll){[IO.Path]::GetFullPath($PluginDll)}else{Join-Path $root 'dist\remote\BatteryConsole-Server-win64\battery_udp_control.dll'}
Copy-Item -LiteralPath $source -Destination (Join-Path $package 'battery_udp_control.dll')
$script=Join-Path $package 'Update-Plugin.ps1'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Update-Plugin.ps1') -Destination $script
$fixture=Join-Path $root 'app\ui\tests\build_startup_fixture\release\mworks.exe'
$exe=Join-Path $fixtureHome 'Bin64\mworks.exe'
Copy-Item -LiteralPath $fixture -Destination $exe
$target=Join-Path $addins 'battery_udp_control.dll'
[IO.File]::WriteAllBytes($target,[byte[]](0,1,2,3,255,128))
$oldHash=(Get-FileHash -LiteralPath $target).Hash
$newHash=(Get-FileHash -LiteralPath $source).Hash
$configuration=@((Join-Path $addins 'remote.ini'),(Join-Path $package 'access-key.txt'),(Join-Path $package 'deployment.json'))
foreach($path in $configuration){[IO.File]::WriteAllBytes($path,[Text.Encoding]::UTF8.GetBytes("existing-test-credential`r`nPort=39001`r`n"))}
$before=@{}
foreach($path in $configuration){$before[$path]=@{hash=(Get-FileHash -LiteralPath $path).Hash;acl=(Get-Acl -LiteralPath $path).Sddl}}
function Assert-That($condition,[string]$message){if(-not $condition){throw $message}}
function Assert-Rejected([scriptblock]$action,[string]$message){
    $rejected=$false
    try{& $action|Out-Null}catch{$rejected=$true}
    Assert-That $rejected $message
}
& $script -SysplorerHome $fixtureHome|Out-Null
Assert-That ((Get-FileHash -LiteralPath $target).Hash -eq $newHash) 'DLL copy/hash failed'
Assert-That ((Get-FileHash -LiteralPath (Join-Path $package "versions\$oldHash.dll")).Hash -eq $oldHash) 'Old DLL backup failed'
$firstWrite=(Get-Item -LiteralPath $target).LastWriteTimeUtc.Ticks
& $script -SysplorerHome $fixtureHome|Out-Null
Assert-That ((Get-Item -LiteralPath $target).LastWriteTimeUtc.Ticks -eq $firstWrite) 'Idempotent update rewrote the DLL'
$manifest=Join-Path $package 'manifest.json'
@{files=@(@{path='battery_udp_control.dll';sha256='invalid'})}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath $manifest -Encoding UTF8
Assert-Rejected {& $script -SysplorerHome $fixtureHome} 'Corrupt manifest was accepted'
Assert-That ((Get-FileHash -LiteralPath $target).Hash -eq $newHash) 'Rejected package changed target'
@{files=@(@{path='battery_udp_control.dll';sha256=$newHash})}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath $manifest -Encoding UTF8
$oldUdp=$env:BATTERY_FIXTURE_UDP
$oldDiscovery=$env:BATTERY_DISCOVERY_DIR
$oldMarker=$env:BATTERY_LAUNCH_SENTINEL
$ownedProcess=$null
try{
    $env:BATTERY_FIXTURE_UDP='1'
    $env:BATTERY_DISCOVERY_DIR='logs/connection-fix/update-discovery'
    $env:BATTERY_LAUNCH_SENTINEL=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes((Join-Path $testRoot 'fixture.jsonl')))
    $ownedProcess=Start-Process -FilePath $exe -WorkingDirectory $root -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 250
    Assert-That (-not $ownedProcess.HasExited) 'Owned process fixture did not start'
    Assert-Rejected {& $script -SysplorerHome $fixtureHome} 'Running target was accepted'
    Assert-That ((Get-Item -LiteralPath $target).LastWriteTimeUtc.Ticks -eq $firstWrite) 'Running target was modified'
}finally{
    if($ownedProcess -and -not $ownedProcess.HasExited){
        $ownedProcess.Refresh()
        Assert-That ([IO.Path]::GetFullPath($ownedProcess.Path) -eq [IO.Path]::GetFullPath($exe)) 'Refused to stop an unowned process'
        $ownedProcess.Kill();$ownedProcess.WaitForExit()
    }
    $env:BATTERY_FIXTURE_UDP=$oldUdp;$env:BATTERY_DISCOVERY_DIR=$oldDiscovery;$env:BATTERY_LAUNCH_SENTINEL=$oldMarker
}
foreach($path in $configuration){
    Assert-That ((Get-FileHash -LiteralPath $path).Hash -eq $before[$path].hash) "Configuration bytes changed: $path"
    Assert-That ((Get-Acl -LiteralPath $path).Sddl -eq $before[$path].acl) "Configuration ACL changed: $path"
}
Assert-Rejected {& $script -SysplorerHome (Join-Path $testRoot 'missing')} 'Missing host was accepted'
[ordered]@{passed=$true;checks=@('copy-and-hash','backup','idempotence','manifest-rejection','running-target-rejection','configuration-bytes','configuration-acls','missing-host');sourceSha256=$newHash;previousSha256=$oldHash;worktreeOnly=$true}|
    ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $testRoot 'result.json') -Encoding UTF8
Write-Output "PASS: $testRoot\result.json"
