$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
. (Join-Path $PSScriptRoot 'LocalWorktree.Common.ps1')
$drive=Connect-WorktreeDrive $root
$relative='logs\portable\setup-'+[guid]::NewGuid().ToString('N')
$testRoot=Get-WorktreeFile $root $relative
$package=Join-Path $testRoot 'package';$mockHome=Join-Path $testRoot 'Mock Sysplorer';$workspace=$drive.root+$relative+'\jobs'
[IO.Directory]::CreateDirectory($package)|Out-Null
[IO.Directory]::CreateDirectory((Join-Path $mockHome 'Bin64'))|Out-Null
# No vendor process is launched. Its PE version is used only to exercise detection.
Copy-Item -LiteralPath 'C:\Program Files\MWORKS\Sysplorer 2026a\Bin64\mworks.exe' -Destination (Join-Path $mockHome 'Bin64\mworks.exe')
foreach($name in @('Setup-Deployment.ps1','Update-Plugin.ps1','Server-Info.ps1','PrivateFile.Common.ps1')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $package}
Copy-Item -LiteralPath (Join-Path $root 'tools\sysplorer_udp_plugin\release\battery_udp_control.dll') -Destination $package
$installer=Join-Path $package 'Setup-Deployment.ps1';$state=Join-Path $testRoot 'local-runtime.json'
$options=@{SysplorerHome=$mockHome;Workspace=$workspace;SkipFirewall=$true;ClientStatePath=$state;NonInteractive=$true;ServerStateDirectory=(Join-Path $testRoot 'private-server')}
& $installer -Mode Local @options
$config=Join-Path $mockHome 'Bin64\addins\BatteryUdpControl\remote.ini'
if([IO.File]::ReadAllText($config) -notmatch 'Enabled=false'){throw 'Local setup unexpectedly enables remote access'}
& $installer -Mode Server @options
if([IO.File]::ReadAllText($config) -notmatch 'Enabled=true'){throw 'Local-to-server promotion failed'}
$receipt=Get-Content -LiteralPath (Join-Path $options.ServerStateDirectory 'last-server.json') -Raw|ConvertFrom-Json
$keyFile=$receipt.keyFile
if(([IO.File]::ReadAllText($keyFile)).Length -lt 32){throw 'Missing private credential export'}
if(-not (Get-Acl -LiteralPath $keyFile).AreAccessRulesProtected){throw 'Credential file inherits public permissions'}
$config=Join-Path $mockHome 'Bin64\addins\BatteryUdpControl\remote.ini'
$before=[IO.File]::ReadAllBytes($config);$beforeHash=(Get-FileHash -LiteralPath $config).Hash
$target=Join-Path $mockHome 'Bin64\addins\BatteryUdpControl\battery_udp_control.dll'
[IO.File]::WriteAllBytes($target,[Text.Encoding]::ASCII.GetBytes('old-plugin-fixture'))
$oldHash=(Get-FileHash -LiteralPath $target).Hash
& $installer -Mode Server @options
if((Get-FileHash -LiteralPath $config).Hash -ne $beforeHash){throw 'Existing remote.ini or credential bytes changed'}
if((Get-FileHash -LiteralPath $target).Hash -ne (Get-FileHash -LiteralPath (Join-Path $package 'battery_udp_control.dll')).Hash){throw 'Plugin copy mismatch'}
if(-not(Test-Path -LiteralPath (Join-Path $package ('versions\'+$oldHash+'.dll')))){throw 'Old DLL backup missing'}
& $installer -Mode Local @options
if((Get-FileHash -LiteralPath $config).Hash -ne $beforeHash){throw 'Local setup changed server settings'}
$dry=& $installer -Mode Server -SysplorerHome $mockHome -Workspace $workspace -DryRun|ConvertFrom-Json
if(-not $dry.preserveConfig -or -not $dry.wouldConfigureFirewall -or $dry.port -ne 39001){throw 'Unexpected setup plan'}
if(($dry.firewallProfiles -join ',') -ne 'Domain,Private' -or $dry.allowedClientAddress -ne 'LocalSubnet'){throw 'Firewall defaults are too broad'}
# Only process inspection is mocked; no vendor process is launched or stopped.
[IO.File]::WriteAllBytes($target,[Text.Encoding]::ASCII.GetBytes('another-old-plugin'))
function Get-Process {param($Name,$ErrorAction) [pscustomobject]@{Path=(Join-Path $mockHome 'Bin64\mworks.exe');Id=4242}}
$runningRejected=$false;try{& $installer -Mode Local @options}catch{$runningRejected=$PSItem.Exception.Message -match 'close all target Sysplorer'}
Remove-Item Function:\Get-Process
if(-not $runningRejected){throw 'Running-target guard failed'}
if([IO.File]::ReadAllText($target) -ne 'another-old-plugin'){throw 'Running target was modified'}
& $installer -Mode Local @options
# Exclusive file access simulates an unwritable target; no system ACL is changed.
$locked=[IO.File]::Open($target,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)
$permissionRejected=$false
try{try{& $installer -Mode Local @options}catch{$permissionRejected=$true}}finally{$locked.Dispose()}
if(-not $permissionRejected){throw 'Unwritable plugin was accepted'}
if((Get-FileHash -LiteralPath $config).Hash -ne $beforeHash){throw 'Failure paths changed credentials/configuration'}
$badManifest=@{files=@(@{path='battery_udp_control.dll';sha256=('0'*64)})}|ConvertTo-Json -Depth 4
[IO.File]::WriteAllText((Join-Path $package 'manifest.json'),$badManifest)
$rejected=$false;try{& $installer -Mode Local -SysplorerHome $mockHome -Workspace $workspace -SkipFirewall}catch{$rejected=$true}
if(-not $rejected){throw 'Corrupt package was accepted'}
[ordered]@{passed=$true;checks=@('first-local-install','local-to-server-promotion','restricted-credential-file','repeat-preserves-ini-and-key-bytes','backup','sha256-copy','local-preserves-server','firewall-dry-run-only','running-target-mock-rejected','exclusive-access-denied','corrupt-manifest-rejected');directory=$testRoot;systemInstallChanged=$false;firewallChanged=$false}|ConvertTo-Json|
    Set-Content -LiteralPath (Join-Path $testRoot 'result.json') -Encoding UTF8
Write-Output "Portable setup simulation PASS: $testRoot"
