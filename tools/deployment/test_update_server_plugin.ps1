param(
    [Parameter(Mandatory=$true)][string]$PatchDirectory,
    [Parameter(Mandatory=$true)][string]$PreviousDll,
    [string]$VersionedHostExe='C:\Program Files\MWORKS\Sysplorer 2026a\Bin64\mworks.exe'
)
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -ErrorAction Stop
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$testRoot=Join-Path $root ('logs\remote-vm\patch-update-'+[guid]::NewGuid().ToString('N'))
$package=Join-Path $testRoot 'package'
$fixtureHome=Join-Path $testRoot 'host'
$addins=Join-Path $fixtureHome 'Bin64\addins\BatteryUdpControl'
[IO.Directory]::CreateDirectory($package)|Out-Null
[IO.Directory]::CreateDirectory($addins)|Out-Null
foreach($name in @('Update-ServerPlugin.ps1','patch-manifest.json','battery_udp_control.dll')){
    Copy-Item -LiteralPath (Join-Path $PatchDirectory $name) -Destination (Join-Path $package $name)
}
$script=Join-Path $package 'Update-ServerPlugin.ps1'
$manifestPath=Join-Path $package 'patch-manifest.json'
$manifest=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8|ConvertFrom-Json
$originalManifest=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8
$exe=Join-Path $fixtureHome 'Bin64\mworks.exe'
Copy-Item -LiteralPath $VersionedHostExe -Destination $exe
$version=(Get-Item -LiteralPath $exe).VersionInfo.FileVersion
$target=Join-Path $addins 'battery_udp_control.dll'
Copy-Item -LiteralPath $PreviousDll -Destination $target
$oldHash=(Get-FileHash -LiteralPath $target).Hash
$newHash=(Get-FileHash -LiteralPath (Join-Path $package 'battery_udp_control.dll')).Hash
$configuration=@((Join-Path $addins 'remote.ini'),(Join-Path $package 'access-key.txt'),(Join-Path $package 'deployment.json'))
$before=@{}
foreach($path in $configuration){
    [IO.File]::WriteAllBytes($path,[Text.Encoding]::UTF8.GetBytes("public-test-only-credential`r`nPort=39001`r`n"))
    $before[$path]=@{hash=(Get-FileHash -LiteralPath $path).Hash;acl=(Get-Acl -LiteralPath $path).Sddl}
}
function Assert-That($condition,[string]$message){if(-not $condition){throw $message}}
function Assert-Rejected([scriptblock]$action,[string]$expected){
    $reason=''
    try{& $action|Out-Null}catch{$reason=$_.Exception.Message}
    Assert-That ($reason -like "*$expected*") "Expected rejection '$expected'; got '$reason'"
}
Assert-That ($oldHash -in @($manifest.allowedPreviousSha256)) 'Test source is not a recognized production patch predecessor'
# The production package is 2026b-only. Verify refusal on the copied 2026a EXE,
# then explicitly allow that EXE in this disposable test package only.
Assert-That ($version -notin @($manifest.allowedHostVersions)) 'Use a non-target host EXE to test production version rejection'
Assert-Rejected {& $script -SysplorerHome $fixtureHome} 'not an allowed patch target'
Assert-That ((Get-FileHash -LiteralPath $target).Hash -eq $oldHash) 'Version rejection modified target'
$manifest.allowedHostVersions=@($version)
$manifest|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $manifestPath -Encoding UTF8
& $script -SysplorerHome $fixtureHome|Out-Null
Assert-That ((Get-FileHash -LiteralPath $target).Hash -eq $newHash) 'DLL copy/hash failed'
$backups=@(Get-ChildItem -LiteralPath $package -Filter '*.dll.bak')
Assert-That ($backups.Count -eq 1) 'Expected one old DLL backup'
Assert-That ((Get-FileHash -LiteralPath $backups[0].FullName).Hash -eq $oldHash) 'Backup hash failed'
$firstWrite=(Get-Item -LiteralPath $target).LastWriteTimeUtc.Ticks
& $script -SysplorerHome $fixtureHome|Out-Null
Assert-That ((Get-Item -LiteralPath $target).LastWriteTimeUtc.Ticks -eq $firstWrite) 'Idempotent update rewrote target'
$manifest.newPluginSha256='invalid'
$manifest|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $manifestPath -Encoding UTF8
Assert-Rejected {& $script -SysplorerHome $fixtureHome} 'does not match the patch manifest'
Assert-That ((Get-FileHash -LiteralPath $target).Hash -eq $newHash) 'Corrupt manifest changed target'
$manifest.newPluginSha256=$newHash
$manifest|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $manifestPath -Encoding UTF8
[IO.File]::WriteAllBytes($target,[byte[]](0,1,2,3,255))
$unknownHash=(Get-FileHash -LiteralPath $target).Hash
Assert-Rejected {& $script -SysplorerHome $fixtureHome} 'not a recognized patch target'
Assert-That ((Get-FileHash -LiteralPath $target).Hash -eq $unknownHash) 'Unknown DLL changed'
Copy-Item -LiteralPath (Join-Path $package 'battery_udp_control.dll') -Destination $target -Force
# Only our startup fixture runs. The target already contains the new DLL, so
# this also proves process checks precede the already-installed early return.
$runningHome=Join-Path $testRoot 'running-host'
$runningAddins=Join-Path $runningHome 'Bin64\addins\BatteryUdpControl'
[IO.Directory]::CreateDirectory($runningAddins)|Out-Null
$runningExe=Join-Path $runningHome 'Bin64\mworks.exe'
Copy-Item -LiteralPath (Join-Path $root 'app\ui\tests\build_startup_fixture\release\mworks.exe') -Destination $runningExe
Copy-Item -LiteralPath $target -Destination (Join-Path $runningAddins 'battery_udp_control.dll')
Copy-Item -LiteralPath $configuration[0] -Destination (Join-Path $runningAddins 'remote.ini')
$saved=@{}
foreach($name in @('PATH','BATTERY_FIXTURE_UDP','BATTERY_DISCOVERY_DIR','BATTERY_LAUNCH_SENTINEL')){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
$ownedProcess=$null
try{
    $env:PATH='C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\bin;'+$env:PATH
    $env:BATTERY_FIXTURE_UDP='1'
    $env:BATTERY_DISCOVERY_DIR='logs/remote-vm/patch-discovery'
    $env:BATTERY_LAUNCH_SENTINEL=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes((Join-Path $testRoot 'fixture.jsonl')))
    $ownedProcess=Start-Process -FilePath $runningExe -WorkingDirectory $root -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 350
    Assert-That (-not $ownedProcess.HasExited) 'Owned startup fixture did not start'
    Assert-Rejected {& $script -SysplorerHome $runningHome} 'close the target Sysplorer'
    Assert-That ((Get-FileHash -LiteralPath (Join-Path $runningAddins 'battery_udp_control.dll')).Hash -eq $newHash) 'Running target changed'
}finally{
    if($ownedProcess -and -not $ownedProcess.HasExited){
        $ownedProcess.Refresh()
        Assert-That ([IO.Path]::GetFullPath($ownedProcess.Path) -eq [IO.Path]::GetFullPath($runningExe)) 'Refused to stop an unowned process'
        $ownedProcess.Kill();$ownedProcess.WaitForExit()
    }
    foreach($name in $saved.Keys){
        if($null -eq $saved[$name]){Remove-Item "Env:$name" -ErrorAction SilentlyContinue}
        else{[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}
    }
}
foreach($path in $configuration){
    Assert-That ((Get-FileHash -LiteralPath $path).Hash -eq $before[$path].hash) "Configuration bytes changed: $path"
    Assert-That ((Get-Acl -LiteralPath $path).Sddl -eq $before[$path].acl) "Configuration ACL changed: $path"
}
Assert-Rejected {& $script -SysplorerHome (Join-Path $testRoot 'missing')} 'Existing host, plugin and remote.ini are required'
Assert-That ((Get-Content -LiteralPath (Join-Path $PatchDirectory 'patch-manifest.json') -Raw -Encoding UTF8) -eq $originalManifest) 'Production manifest was modified'
[ordered]@{passed=$true;checks=@('production-host-version-rejection','copy-and-hash','backup','idempotence','corrupt-manifest-rejection','unknown-dll-rejection','running-target-before-idempotence','configuration-bytes','configuration-acls','missing-host');sourceSha256=$newHash;previousSha256=$oldHash;fixtureHostVersion=$version;worktreeOnly=$true}|
    ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $testRoot 'result.json') -Encoding UTF8
Write-Output "PASS: $testRoot\result.json"
