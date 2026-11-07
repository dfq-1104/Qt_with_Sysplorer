param([string]$SysplorerHome='C:\Program Files\MWORKS\Sysplorer 2026a',
      [string]$ClientDirectory='', [string]$PluginDll='', [switch]$NewGeneration)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'LocalWorktree.Common.ps1')
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$stateFile=Get-WorktreeFile $root '.local-sysplorer\state.json'
$sourceHost=[IO.Path]::GetFullPath($SysplorerHome)
$sourceExe=Join-Path $sourceHost 'Bin64\mworks.exe'
if(-not $ClientDirectory){
    $ClientDirectory=Join-Path $root 'dist\dual-mode-build\BatteryConsole-Client-win64'
    if(-not(Test-Path -LiteralPath $ClientDirectory)){$ClientDirectory=Join-Path $root 'dist\remote-connect-fix-20261108\BatteryConsole-Client-win64'}
}
if(-not $PluginDll){$PluginDll=Join-Path $root 'dist\dual-mode-inputs\local-2026a.dll'}
foreach($file in @($sourceExe,(Join-Path $ClientDirectory 'battery_console_ui.exe'),$PluginDll)){
    if(-not(Test-Path -LiteralPath $file -PathType Leaf)){throw "Required file missing: $file"}
}
$pluginHash=(Get-FileHash -LiteralPath $PluginDll).Hash
$hostHash=(Get-FileHash -LiteralPath $sourceExe).Hash
$clientHash=(Get-FileHash -LiteralPath (Join-Path $ClientDirectory 'battery_console_ui.exe')).Hash
$modelHash=(Get-Content -LiteralPath (Join-Path $ClientDirectory 'manifest.json') -Raw -Encoding UTF8|ConvertFrom-Json).modelHash
if($modelHash -notmatch '^[0-9a-fA-F]{64}$'){throw 'Client model hash missing from its manifest'}
if(@(Get-ChildItem -LiteralPath $ClientDirectory -Recurse -Force|Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}).Count){throw 'Client source must contain regular files and directories'}
function Sync-LocalClientSupport([string]$TargetClient){
    $managed=[ordered]@{
        'Start-Client.ps1'=(Join-Path $PSScriptRoot 'Start-WorktreeClient.ps1')
        'Start-Local.ps1'=(Join-Path $PSScriptRoot 'Start-LocalClient.ps1')
        'Start-Remote.ps1'=(Join-Path $PSScriptRoot 'Start-Remote.ps1')
        '本机运行说明.md'=(Join-Path $root 'docs\双模式合并运行说明.md')
        '部署说明.md'=(Join-Path $root 'docs\双模式合并运行说明.md')
        '验证记录.md'=(Join-Path $root 'docs\双模式合并验证记录.md')
    }
    $changed=$false
    foreach($name in $managed.Keys){
        $target=Get-WorktreeFile $root ((Join-Path $TargetClient $name).Substring($root.Length+1))
        if(-not(Test-Path -LiteralPath $target) -or (Get-FileHash -LiteralPath $target).Hash -ne (Get-FileHash -LiteralPath $managed[$name]).Hash){
            Copy-Item -LiteralPath $managed[$name] -Destination $target -Force;$changed=$true
        }
    }
    if($changed){Write-LocalClientManifest $TargetClient $modelHash}
}
if(Test-Path -LiteralPath $stateFile){
    $previous=Get-Content -LiteralPath $stateFile -Raw -Encoding UTF8|ConvertFrom-Json
    if($previous.worktreeRoot -ne $root){throw 'Prepared runtime belongs to another worktree'}
    $previousHost=Get-WorktreeFile $root ($previous.hostRelative+'\Bin64\mworks.exe')
    $previousPlugin=Get-WorktreeFile $root ($previous.hostRelative+'\Bin64\addins\BatteryUdpControl\battery_udp_control.dll')
    $previousClient=Get-WorktreeFile $root ($previous.clientRelative+'\battery_console_ui.exe')
    if(-not $NewGeneration -and $previous.pluginSha256 -eq $pluginHash -and $previous.hostSha256 -eq $hostHash -and $previous.clientSha256 -eq $clientHash -and
       (Test-Path -LiteralPath $previousHost) -and (Test-Path -LiteralPath $previousPlugin) -and (Test-Path -LiteralPath $previousClient) -and
       (Get-FileHash -LiteralPath $previousHost).Hash -eq $hostHash -and (Get-FileHash -LiteralPath $previousPlugin).Hash -eq $pluginHash -and
       (Get-FileHash -LiteralPath $previousClient).Hash -eq $clientHash){
        Sync-LocalClientSupport ([IO.Path]::GetDirectoryName($previousClient))
        Write-Output "Local worktree runtime already prepared: $stateFile";return
    }
    if(-not $NewGeneration){throw 'Prepared runtime differs from this build. Keep active programs intact and pass -NewGeneration to prepare a new runtime.'}
    Save-WorktreeJson $previous (Get-WorktreeFile $root ('.local-sysplorer\history\'+[guid]::NewGuid().ToString('N')+'.json'))
}
$generation=[guid]::NewGuid().ToString('N').Substring(0,12)
$hostRelative='.local-sysplorer\host-'+$generation
$clientRelative='dist\local-worktree\'+$generation+'\BatteryConsole-Client-win64'
$destination=Get-WorktreeFile $root $hostRelative
$client=Get-WorktreeFile $root $clientRelative
[IO.Directory]::CreateDirectory($destination)|Out-Null
[IO.Directory]::CreateDirectory($client)|Out-Null
# The executable, addins, Qt plugins and settings are private copies. Large
# vendor libraries/tools are read from the existing licensed installation.
$sourceBin=Join-Path $sourceHost 'Bin64'
$bin=Join-Path $destination 'Bin64'
[IO.Directory]::CreateDirectory($bin)|Out-Null
Get-ChildItem -LiteralPath $sourceBin -File|ForEach-Object{Copy-Item -LiteralPath $_.FullName -Destination $bin}
Get-ChildItem -LiteralPath $sourceBin -Directory|Where-Object {$_.Name -ne 'GPUCache'}|ForEach-Object{
    Copy-Item -LiteralPath $_.FullName -Destination $bin -Recurse
}
Get-ChildItem -LiteralPath $sourceHost -Directory|Where-Object {$_.Name -ne 'Bin64'}|ForEach-Object{
    $target=Join-Path $destination $_.Name
    if($_.Name -eq 'Setting'){Copy-Item -LiteralPath $_.FullName -Destination $destination -Recurse}
    else{New-Item -ItemType Junction -Path $target -Target $_.FullName|Out-Null}
}
$plugin=Join-Path $bin 'addins\BatteryUdpControl\battery_udp_control.dll'
[void](Get-WorktreeFile $root ($hostRelative+'\Bin64\addins\BatteryUdpControl\battery_udp_control.dll'))
[void](Get-WorktreeFile $root ($hostRelative+'\Bin64\addins\BatteryUdpControl\remote.ini'))
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($plugin))|Out-Null
Copy-Item -LiteralPath $PluginDll -Destination $plugin -Force
"[Remote]`r`nEnabled=false`r`n"|Set-Content -LiteralPath (Join-Path ([IO.Path]::GetDirectoryName($plugin)) 'remote.ini') -Encoding ASCII
if((Get-FileHash -LiteralPath $plugin).Hash -ne $pluginHash -or (Get-FileHash -LiteralPath (Join-Path $bin 'mworks.exe')).Hash -ne $hostHash){throw 'Private host binary hash mismatch'}
Get-ChildItem -LiteralPath $ClientDirectory -Force|ForEach-Object{Copy-Item -LiteralPath $_.FullName -Destination $client -Recurse}
Sync-LocalClientSupport $client
Save-WorktreeJson ([ordered]@{worktreeRoot=$root;clientRelative=$clientRelative}) (Join-Path $client 'local-worktree.json')
Write-LocalHostConfiguration $client $destination
Write-LocalClientManifest $client $modelHash
$state=[ordered]@{schema=1;worktreeRoot=$root;hostRelative=$hostRelative;clientRelative=$clientRelative;sourceHost=$sourceHost;pluginSha256=$pluginHash;hostSha256=$hostHash;clientSha256=$clientHash;modelHash=$modelHash;driveLetter='';preparedAt=(Get-Date).ToString('o')}
Save-WorktreeJson $state $stateFile
Write-Output "Prepared private local host: $destination"
Write-Output "Plugin SHA256: $pluginHash"
Write-Output 'Use app\ui\run_local.bat. Preparing this runtime did not start Qt or Sysplorer and did not update the installed plugin.'
