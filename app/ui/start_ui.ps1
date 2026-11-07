param([switch]$Remote,[string]$RuntimeDirectory='',[switch]$SmokeTest,[switch]$PassThru)
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if(-not $RuntimeDirectory -and (Test-Path -LiteralPath (Join-Path $repoRoot '.local-sysplorer\state.json'))){
    if($Remote){& (Join-Path $repoRoot 'tools\deployment\Start-LocalWorktree.ps1') -Remote -SmokeTest:$SmokeTest -PassThru:$PassThru}
    else{& (Join-Path $repoRoot 'tools\deployment\Start-LocalWorktree.ps1') -PreserveConnectionMode -SmokeTest:$SmokeTest -PassThru:$PassThru}
    return
}
if($RuntimeDirectory){
    $runtime=[IO.Path]::GetFullPath($RuntimeDirectory)
}else{
    $candidates=@((Join-Path $repoRoot 'dist\connection-fix\BatteryConsole-Client-win64'),
        (Join-Path $repoRoot 'dist\remote\BatteryConsole-Client-win64'),
        (Join-Path $PSScriptRoot 'runtime'),(Join-Path $PSScriptRoot 'runtime_buttons'),(Join-Path $PSScriptRoot 'runtime_tuning'))
    $runtime=$candidates | Where-Object {Test-Path -LiteralPath (Join-Path $_ 'battery_console_ui.exe')} | Select-Object -First 1
}
if(-not $runtime){throw 'Build and deploy the Qt client first. No Sysplorer was started or changed.'}
$exe=Join-Path $runtime 'battery_console_ui.exe'
if(-not(Test-Path -LiteralPath $exe)){throw "Qt client missing: $exe"}
# Launch only Qt; deployment and host startup require explicit user actions.
foreach($name in @('QT_QPA_PLATFORM','QT_QPA_PLATFORM_PLUGIN_PATH','QT_PLUGIN_PATH','PYTHONHOME','PYTHONPATH')){Remove-Item "Env:$name" -ErrorAction SilentlyContinue}
if($Remote){$env:BATTERY_CONNECTION_MODE='remote'}
$env:BATTERY_PROJECT_DIR=$repoRoot
$env:PATH=$runtime+';'+$env:PATH
$options=@{FilePath=$exe;WorkingDirectory=$runtime;PassThru=$true}
if($SmokeTest){$options.ArgumentList='--smoke-test';$options.WindowStyle='Hidden'}
$started=Start-Process @options
if($PassThru){return $started}
