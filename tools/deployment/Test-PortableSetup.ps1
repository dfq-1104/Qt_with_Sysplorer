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
foreach($name in @('Setup-Deployment.ps1','Update-Plugin.ps1')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $package}
Copy-Item -LiteralPath (Join-Path $root 'tools\sysplorer_udp_plugin\release\battery_udp_control.dll') -Destination $package
$installer=Join-Path $package 'Setup-Deployment.ps1';$state=Join-Path $testRoot 'local-runtime.json'
& $installer -Mode Server -SysplorerHome $mockHome -Workspace $workspace -SkipFirewall -ClientStatePath $state
$config=Join-Path $mockHome 'Bin64\addins\BatteryUdpControl\remote.ini'
$before=[IO.File]::ReadAllBytes($config);$beforeHash=(Get-FileHash -LiteralPath $config).Hash
$target=Join-Path $mockHome 'Bin64\addins\BatteryUdpControl\battery_udp_control.dll'
[IO.File]::WriteAllBytes($target,[Text.Encoding]::ASCII.GetBytes('old-plugin-fixture'))
$oldHash=(Get-FileHash -LiteralPath $target).Hash
& $installer -Mode Server -SysplorerHome $mockHome -Workspace $workspace -SkipFirewall -ClientStatePath $state
if((Get-FileHash -LiteralPath $config).Hash -ne $beforeHash){throw 'Existing remote.ini or credential bytes changed'}
if((Get-FileHash -LiteralPath $target).Hash -ne (Get-FileHash -LiteralPath (Join-Path $package 'battery_udp_control.dll')).Hash){throw 'Plugin copy mismatch'}
if(-not(Test-Path -LiteralPath (Join-Path $package ('versions\'+$oldHash+'.dll')))){throw 'Old DLL backup missing'}
& $installer -Mode Local -SysplorerHome $mockHome -Workspace $workspace -SkipFirewall -ClientStatePath $state
if((Get-FileHash -LiteralPath $config).Hash -ne $beforeHash){throw 'Local setup changed server settings'}
$dry=& $installer -Mode Server -SysplorerHome $mockHome -Workspace $workspace -DryRun|ConvertFrom-Json
if(-not $dry.preserveConfig -or -not $dry.wouldConfigureFirewall -or $dry.port -ne 39001){throw 'Unexpected setup plan'}
if(($dry.firewallProfiles -join ',') -ne 'Domain,Private' -or $dry.allowedClientAddress -ne 'LocalSubnet'){throw 'Firewall defaults are too broad'}
$badManifest=@{files=@(@{path='battery_udp_control.dll';sha256=('0'*64)})}|ConvertTo-Json -Depth 4
[IO.File]::WriteAllText((Join-Path $package 'manifest.json'),$badManifest)
$rejected=$false;try{& $installer -Mode Local -SysplorerHome $mockHome -Workspace $workspace -SkipFirewall}catch{$rejected=$true}
if(-not $rejected){throw 'Corrupt package was accepted'}
[ordered]@{passed=$true;checks=@('first-install','repeat-preserves-ini-and-key-bytes','backup','sha256-copy','local-preserves-server','firewall-dry-run-only','corrupt-manifest-rejected');directory=$testRoot;systemInstallChanged=$false;firewallChanged=$false}|ConvertTo-Json|
    Set-Content -LiteralPath (Join-Path $testRoot 'result.json') -Encoding UTF8
Write-Output "Portable setup simulation PASS: $testRoot"
