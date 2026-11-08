param([Parameter(Mandatory=$true)][string]$ClientDirectory,[Parameter(Mandatory=$true)][string]$HostEnvironment)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
. (Join-Path $PSScriptRoot 'LocalWorktree.Common.ps1')
$client=[IO.Path]::GetFullPath($ClientDirectory)
if(-not $client.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Test client must stay in this worktree'}
$e=Get-Content -LiteralPath $HostEnvironment -Raw|ConvertFrom-Json
$physicalHome=[IO.Path]::GetFullPath($e.hostHome)
if($physicalHome -match '^([T-Z]):\\'){
    $mapped=Get-WorktreeDriveTarget $Matches[1]
    if($mapped -eq $root){$physicalHome=Join-Path $root $physicalHome.Substring(3)}
}
if(-not $physicalHome.StartsWith((Join-Path $root '.local-sysplorer')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Only a worktree-isolated host is allowed'}
$output=Join-Path $root ('logs\portable\runtime-'+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($output)|Out-Null
$names=@('PATH','BATTERY_TEST_ARTIFACTS','BATTERY_TEST_CONFIG_ROOT','BATTERY_MODEL_FILE','BATTERY_CONNECTION_MODE','SYSPLORER_HOME','BATTERY_SIM_WORKSPACE','BATTERY_WORKTREE_ROOT','BATTERY_LOCAL_CONFIG','BATTERY_DISCOVERY_DIR','BATTERY_REMOTE_ADDRESS','BATTERY_REMOTE_PORT','BATTERY_REMOTE_KEY','QT_QPA_PLATFORM','QT_QPA_PLATFORM_PLUGIN_PATH','QT_PLUGIN_PATH')
$saved=@{};foreach($name in $names){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
$results=@()
try{
    foreach($name in @('SYSPLORER_HOME','BATTERY_MODEL_FILE','BATTERY_WORKTREE_ROOT','QT_QPA_PLATFORM','QT_QPA_PLATFORM_PLUGIN_PATH','QT_PLUGIN_PATH')){Remove-Item "Env:$name" -ErrorAction SilentlyContinue}
    $env:PATH="$env:SystemRoot\System32;$env:SystemRoot;$env:SystemRoot\System32\WindowsPowerShell\v1.0"
    $env:BATTERY_TEST_CONFIG_ROOT=Join-Path $output 'settings';$env:BATTERY_DISCOVERY_DIR=$e.discovery
    $env:BATTERY_LOCAL_CONFIG=Join-Path $output 'local-runtime.json'
    [ordered]@{schema=1;sysplorerHome=$e.hostHome;workspace=($e.data+'\jobs')}|ConvertTo-Json|Set-Content -LiteralPath $env:BATTERY_LOCAL_CONFIG -Encoding UTF8
    foreach($mode in @('local','remote')){
        $env:BATTERY_CONNECTION_MODE=$mode
        $env:BATTERY_REMOTE_ADDRESS='127.0.0.1';$env:BATTERY_REMOTE_PORT=[string]$e.port
        $env:BATTERY_REMOTE_KEY=[IO.File]::ReadAllText($e.keyFile).Trim()
        $env:BATTERY_SIM_WORKSPACE=Join-Path $output ($mode+'-cache')
        foreach($operation in @('connect','solve')){
            $artifacts=Join-Path $output ($mode+'-'+$operation);[IO.Directory]::CreateDirectory($artifacts)|Out-Null;$env:BATTERY_TEST_ARTIFACTS=$artifacts
            $argument=if($operation -eq 'solve'){'--verify-custom'}elseif($mode -eq 'local'){'--verify-local-connection'}else{'--verify-remote-connection'}
            $process=Start-Process -FilePath (Join-Path $client 'battery_console_ui.exe') -ArgumentList $argument -WorkingDirectory $client -WindowStyle Hidden -PassThru
            $deadline=(Get-Date).AddSeconds(180)
            while(-not $process.WaitForExit(1000)){if((Get-Date) -ge $deadline){throw "Client $mode/$operation timed out; inspect the isolated task before continuing"}}
            $report=if($operation -eq 'solve'){'gui_custom_probe.json'}else{$mode+'_connection.json'}
            $result=Get-Content -LiteralPath (Join-Path $artifacts $report) -Raw|ConvertFrom-Json
            if($process.ExitCode -ne 0 -or -not $result.ok){throw "Packaged client $mode/$operation failed: $($result.message)"}
            $results+=@{mode=$mode;operation=$operation;passed=$true;report=(Join-Path $artifacts $report)}
        }
    }
}finally{
    foreach($name in $saved.Keys){if($null -eq $saved[$name]){Remove-Item "Env:$name" -ErrorAction SilentlyContinue}else{[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}}
}
[ordered]@{passed=$true;client=$client;host=$e.hostHome;tests=$results;developerPathRequired=$false;installedHostModified=$false}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $output 'result.json') -Encoding UTF8
Write-Output "Packaged dual-mode runtime PASS: $output"
