param([switch]$ConnectLocal)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$context=& (Join-Path $PSScriptRoot 'Start-LocalWorktree.ps1') -EnvironmentOnly
$output=Join-Path $root ('logs\direct-client\'+[guid]::NewGuid().ToString('N'))
$client=Join-Path $output 'client'
[IO.Directory]::CreateDirectory($output)|Out-Null
Copy-Item -LiteralPath $context.client -Destination $client -Recurse
$binding=Join-Path $client 'local-worktree.json'
$originalBinding=[IO.File]::ReadAllText($binding)
$exe=Join-Path $client 'battery_console_ui.exe'
$settings=Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'BatteryConsole\RemoteConnection.ini'
$settingsBefore=if(Test-Path $settings){(Get-FileHash $settings).Hash}else{''}
$names=@('SYSPLORER_HOME','BATTERY_WORKTREE_ROOT','BATTERY_SIM_WORKSPACE','BATTERY_DISCOVERY_DIR','BATTERY_REMOTE_CONFIG','BATTERY_CONNECTION_MODE','BATTERY_TEST_ARTIFACTS','PATH','QT_QPA_PLATFORM','QT_QPA_PLATFORM_PLUGIN_PATH','QT_PLUGIN_PATH')
$saved=@{};foreach($name in $names){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
$cases=@();$owned=$null
function Run-Probe([string]$Name,[string]$Argument,[int]$ExpectedExit,[string]$ReportName){
    $artifacts=Join-Path $output $Name;[IO.Directory]::CreateDirectory($artifacts)|Out-Null
    $env:BATTERY_TEST_ARTIFACTS=$artifacts
    $script:owned=Start-Process -FilePath $exe -WorkingDirectory $client -WindowStyle Hidden -PassThru -ArgumentList $Argument
    if(-not $script:owned.WaitForExit(240000)){throw "Direct client timed out: $Name"}
    if($script:owned.ExitCode -ne $ExpectedExit){throw "Unexpected direct-client exit: $Name; $($script:owned.ExitCode)"}
    return Get-Content -LiteralPath (Join-Path $artifacts $ReportName) -Raw -Encoding UTF8|ConvertFrom-Json
}
try{
    foreach($name in @('QT_QPA_PLATFORM','QT_QPA_PLATFORM_PLUGIN_PATH','QT_PLUGIN_PATH')){Remove-Item "Env:$name" -ErrorAction SilentlyContinue}
    $env:PATH=$client+';'+$env:PATH
    $env:BATTERY_CONNECTION_MODE='local'
    $env:SYSPLORER_HOME='C:/Program Files/MWORKS/Sysplorer 2026a'
    $env:BATTERY_WORKTREE_ROOT='stale-parent-worktree'
    $env:BATTERY_SIM_WORKSPACE=Join-Path $output 'wrong-workspace'
    $env:BATTERY_DISCOVERY_DIR=Join-Path $output 'wrong-discovery'
    $env:BATTERY_REMOTE_CONFIG=Join-Path $output 'wrong-remote.ini'
    [IO.File]::WriteAllText((Join-Path $client 'sysplorer.ini'),"[Host]`r`nHome=C:/Program Files/MWORKS/Sysplorer 2026a`r`n[Runtime]`r`nWorktreeBound=true`r`n",[Text.UTF8Encoding]::new($false))
    $smoke=Run-Probe 'bare-start' '--smoke-test' 0 'ui_startup.json'
    if($smoke.phase -ne 'Idle' -or $smoke.connecting -or $smoke.running -or $smoke.worktreeRoot -ne $root -or $smoke.hostHome -ne $context.host -or $smoke.discoveryDirectory -ne $context.discovery){throw 'Direct EXE did not restore the private environment'}
    if(Test-Path (Join-Path $output 'wrong-workspace')){throw 'Client used inherited incorrect workspace'}
    $cases+=@{name='bare-start-overrides-old-system-and-parent-paths';passed=$true;phase=$smoke.phase;hostHome=$smoke.hostHome}
    if($ConnectLocal){
        $connection=Run-Probe 'bare-local-connect' '--verify-local-connection' 0 'local_connection.json'
        if(-not $connection.ok -or $connection.remoteMode -or $connection.phase -ne 'Connected' -or $connection.worktreeRoot -ne $root -or $connection.hostHome -ne $context.host){throw 'Direct local connection did not use the private host'}
        $cases+=@{name='bare-local-connect';passed=$true;phase=$connection.phase;hostHome=$connection.hostHome;jobDirectory=$connection.jobDirectory}
    }
    # Only remove the binding in the uniquely created fixture copy.
    if(-not [IO.Path]::GetFullPath($binding).StartsWith($output+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Fixture path escaped output'}
    Remove-Item -LiteralPath $binding
    $missing=Run-Probe 'missing-binding' '--smoke-test' 2 'runtime_bootstrap.json'
    if($missing.ok -or $missing.message -notmatch 'local-worktree.json'){throw 'Missing binding was not rejected'}
    $cases+=@{name='missing-binding-rejected';passed=$true}
    [IO.File]::WriteAllText($binding,'{"worktreeRoot":"C:/missing-battery-worktree"}',[Text.UTF8Encoding]::new($false))
    $wrong=Run-Probe 'wrong-binding' '--smoke-test' 2 'runtime_bootstrap.json'
    if($wrong.ok){throw 'Wrong worktree binding was accepted'}
    $cases+=@{name='wrong-binding-rejected';passed=$true}
    [IO.File]::WriteAllText($binding,$originalBinding,[Text.UTF8Encoding]::new($false))
    $settingsAfter=if(Test-Path $settings){(Get-FileHash $settings).Hash}else{''}
    if($settingsBefore -ne $settingsAfter){throw 'Direct launch changed user connection settings'}
    [ordered]@{passed=$true;clientSha256=(Get-FileHash $exe).Hash;worktreeRoot=$root;privateHost=$context.host;userSettingsUnchanged=$true;cases=$cases}|
        ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $output 'result.json') -Encoding UTF8
}finally{
    if($owned -and -not $owned.HasExited){$owned.Kill();$owned.WaitForExit()}
    foreach($name in $saved.Keys){if($null -eq $saved[$name]){Remove-Item "Env:$name" -ErrorAction SilentlyContinue}else{[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}}
}
Write-Output "PASS: $output\result.json"
