param([switch]$EnvironmentOnly,[switch]$SmokeTest,[switch]$VerifyCustom,[switch]$PassThru,[switch]$PreserveConnectionMode,[string]$ArtifactsDirectory='',[switch]$Remote,[switch]$ConnectionTest)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'LocalWorktree.Common.ps1')
if(@($EnvironmentOnly,$SmokeTest,$VerifyCustom,$ConnectionTest|Where-Object {$_}).Count -gt 1){throw 'Choose only one validation mode'}
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$stateFile=Get-WorktreeFile $root '.local-sysplorer\state.json'
if(-not(Test-Path -LiteralPath $stateFile)){throw 'Run tools\deployment\Prepare-LocalWorktree.ps1 once before starting the local worktree client.'}
$state=Get-Content -LiteralPath $stateFile -Raw -Encoding UTF8|ConvertFrom-Json
if($state.worktreeRoot -ne $root){throw 'Local runtime belongs to another worktree. Prepare it again in this worktree.'}
$hostRoot=Get-WorktreeFile $root $state.hostRelative
$client=Get-WorktreeFile $root $state.clientRelative
$plugin=Get-WorktreeFile $root ($state.hostRelative+'\Bin64\addins\BatteryUdpControl\battery_udp_control.dll')
$exe=Get-WorktreeFile $root ($state.clientRelative+'\battery_console_ui.exe')
if((Get-FileHash -LiteralPath $plugin).Hash -ne $state.pluginSha256 -or (Get-FileHash -LiteralPath $exe).Hash -ne $state.clientSha256 -or
   (Get-FileHash -LiteralPath (Join-Path $hostRoot 'Bin64\mworks.exe')).Hash -ne $state.hostSha256){throw 'Local runtime binaries changed. Launch was cancelled.'}
$drive=Connect-WorktreeDrive $root $state.driveLetter
$asciiHost=Join-Path $drive.root $state.hostRelative
$jobs=Get-WorktreeFile $root '.local-sysplorer\jobs'
$discovery=Get-WorktreeFile $root '.local-sysplorer\discovery'
$temporary=Get-WorktreeFile $root '.local-sysplorer\tmp'
foreach($path in @($jobs,$discovery,$temporary)){[IO.Directory]::CreateDirectory($path)|Out-Null}
Write-LocalHostConfiguration $client $asciiHost
Write-LocalClientManifest $client $state.modelHash
$state.driveLetter=$drive.letter;Save-WorktreeJson $state $stateFile
$context=[ordered]@{
 SYSPLORER_HOME=$asciiHost.Replace('\','/')
 BATTERY_SIM_WORKSPACE=(Join-Path $drive.root '.local-sysplorer\jobs').Replace('\','/')
 BATTERY_DISCOVERY_DIR=(Join-Path $drive.root '.local-sysplorer\discovery').Replace('\','/')
 BATTERY_REMOTE_CONFIG=(Join-Path $asciiHost 'Bin64\addins\BatteryUdpControl\remote.ini').Replace('\','/')
 TEMP=(Join-Path $drive.root '.local-sysplorer\tmp');TMP=(Join-Path $drive.root '.local-sysplorer\tmp')
 PATH=$client+';'+$env:PATH
}
if($ArtifactsDirectory){
    $artifacts=[IO.Path]::GetFullPath($ArtifactsDirectory)
    if(-not $artifacts.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Validation artifacts must stay inside this worktree'}
    $artifacts=Get-WorktreeFile $root $artifacts.Substring($root.Length+1)
    [IO.Directory]::CreateDirectory($artifacts)|Out-Null;$context['BATTERY_TEST_ARTIFACTS']=$artifacts
}
if($EnvironmentOnly){return [pscustomobject]@{host=$context.SYSPLORER_HOME;client=$client;workspace=$context.BATTERY_SIM_WORKSPACE;discovery=$context.BATTERY_DISCOVERY_DIR;drive=$drive.letter;environment=$context}}
$clear=@('QT_QPA_PLATFORM','QT_QPA_PLATFORM_PLUGIN_PATH','QT_PLUGIN_PATH','PYTHONHOME','PYTHONPATH','BATTERY_CONNECTION_MODE','BATTERY_REMOTE_ADDRESS','BATTERY_REMOTE_PORT','BATTERY_REMOTE_KEY')
$saved=@{};foreach($name in @($context.Keys)+$clear){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
try{
foreach($name in $clear){Remove-Item "Env:$name" -ErrorAction SilentlyContinue}
foreach($name in $context.Keys){[Environment]::SetEnvironmentVariable($name,$context[$name],'Process')}
if($Remote){$env:BATTERY_CONNECTION_MODE='remote'}elseif(-not $PreserveConnectionMode){$env:BATTERY_CONNECTION_MODE='local'}
$arguments=@();if($SmokeTest){$arguments+='--smoke-test'};if($VerifyCustom){$arguments+='--verify-custom'};if($ConnectionTest){$arguments+='--verify-remote-connection'}
$options=@{FilePath=$exe;WorkingDirectory=$client;PassThru=$true}
if($arguments.Count){$options.ArgumentList=$arguments;$options.WindowStyle='Hidden'}
$process=Start-Process @options
if($PassThru){return $process}
Write-Output "Local Qt started (PID $($process.Id)). Host starts only after an explicit connection or simulation action."
}finally{foreach($name in $saved.Keys){[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}}
