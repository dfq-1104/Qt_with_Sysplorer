param([Parameter(Mandatory=$true)][string]$ClientDirectory)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$client=[IO.Path]::GetFullPath($ClientDirectory)
if(-not $client.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Client must be inside this worktree'}
$testRoot=Join-Path $root ('logs\embedded-model-r5\'+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($testRoot)|Out-Null
$names=@('BATTERY_MODEL_FILE','BATTERY_MODEL_DIAGNOSTICS','BATTERY_TEST_CONFIG_ROOT','BATTERY_TEST_ARTIFACTS','BATTERY_SIM_WORKSPACE','BATTERY_CONNECTION_MODE','SYSPLORER_HOME','BATTERY_WORKTREE_ROOT','QT_QPA_PLATFORM','PATH')
$saved=@{};foreach($name in $names){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
try{
    $env:PATH=$client+';'+$env:PATH
    $env:QT_QPA_PLATFORM='offscreen'
    $env:SYSPLORER_HOME=Join-Path $testRoot 'no-host-may-start'
    Remove-Item Env:BATTERY_WORKTREE_ROOT -ErrorAction SilentlyContinue
    $model=Join-Path $testRoot 'invalid.mo'
    [IO.File]::WriteAllBytes($model,[byte[]]@(0xff,0xfe,0x70,0))
    if(Test-Path (Join-Path $client 'models\BatterySim.mo')){throw 'Release package must not contain an external model'}
    foreach($scenario in @('embedded-local','embedded-remote','ignored-invalid','ignored-missing')){
        $out=Join-Path $testRoot $scenario;[IO.Directory]::CreateDirectory($out)|Out-Null
        $env:BATTERY_TEST_ARTIFACTS=$out
        $env:BATTERY_TEST_CONFIG_ROOT=Join-Path $out 'settings'
        $env:BATTERY_SIM_WORKSPACE=Join-Path $out 'jobs'
        $env:BATTERY_MODEL_DIAGNOSTICS=Join-Path $out 'diagnostics'
        $env:BATTERY_CONNECTION_MODE=if($scenario -eq 'embedded-remote'){'remote'}else{'local'}
        $env:BATTERY_MODEL_FILE=if($scenario -eq 'ignored-invalid'){$model}else{Join-Path $testRoot 'missing.mo'}
        $p=Start-Process -FilePath (Join-Path $client 'battery_console_ui.exe') -WorkingDirectory $client -ArgumentList '--startup-model-check --smoke-test' -PassThru -WindowStyle Hidden
        if(-not $p.WaitForExit(15000)){throw "Startup test did not terminate: $scenario (PID $($p.Id))"}
        $p.Refresh()
        $pre=Get-Content -LiteralPath (Join-Path $out 'startup_model_check.json') -Encoding UTF8 -Raw|ConvertFrom-Json
        $diag=Get-Content -LiteralPath $pre.diagnostic -Encoding UTF8 -Raw|ConvertFrom-Json
        if($pre.pid -ne $p.Id -or $diag.pid -ne $p.Id){throw 'Preflight must read inside the actual client process'}
        $ui=Get-Content -LiteralPath (Join-Path $out 'ui_startup.json') -Encoding UTF8 -Raw|ConvertFrom-Json
        if($ui.phase -ne 'Idle' -or $ui.connecting -or $ui.running -or $ui.taskId){throw 'Startup unexpectedly initiated a connection or task'}
        if($ui.remoteMode -ne ($scenario -eq 'embedded-remote')){throw 'Startup changed the connection mode'}
        if($p.ExitCode -ne 0 -or -not $pre.ok){throw "Embedded model failed: $($pre.message)"}
        if($diag.source -ne 'embedded-resource' -or $diag.path -ne ':/models/BatterySim.mo'){throw 'Client did not use its embedded resource'}
        $manifest=Get-Content -LiteralPath (Join-Path $client 'manifest.json') -Encoding UTF8 -Raw|ConvertFrom-Json
        if($diag.sha256 -ne $manifest.modelHash){throw 'Embedded model hash differs from the manifest'}
        Write-Output "PASS $scenario : exit $($p.ExitCode), same-process model check"
    }
    Write-Output "Artifacts: $testRoot"
}finally{
    foreach($name in $names){[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}
}
