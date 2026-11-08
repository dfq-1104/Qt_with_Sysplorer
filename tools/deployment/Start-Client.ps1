param([switch]$SmokeTest)
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Management\Microsoft.PowerShell.Management.psd1') -ErrorAction Stop
$clean=@('QT_QPA_PLATFORM','QT_QPA_PLATFORM_PLUGIN_PATH','QT_PLUGIN_PATH','PYTHONHOME','PYTHONPATH',
    'SYSPLORER_HOME','BATTERY_WORKTREE_ROOT','BATTERY_DISCOVERY_DIR','BATTERY_REMOTE_CONFIG','BATTERY_SIM_WORKSPACE','BATTERY_LOCAL_CONFIG','BATTERY_MODEL_FILE',
    'BATTERY_CONNECTION_MODE','BATTERY_REMOTE_ADDRESS','BATTERY_REMOTE_PORT','BATTERY_REMOTE_KEY')
$saved=@{};foreach($name in @($clean)+@('PATH')){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
try{
    # Normal package startup uses this computer's saved settings. Development
    # bindings inherited from an unrelated shell must not select a private host.
    foreach($name in $clean){Remove-Item "Env:$name" -ErrorAction SilentlyContinue}
    $env:PATH=$PSScriptRoot+';'+$saved['PATH']
    $options=@{FilePath=(Join-Path $PSScriptRoot 'battery_console_ui.exe');WorkingDirectory=$PSScriptRoot;WindowStyle='Normal';ArgumentList='--startup-model-check'}
    if($SmokeTest){$options.ArgumentList='--startup-model-check --smoke-test';$options.WindowStyle='Hidden'}
    Start-Process @options | Out-Null
}catch{
    Write-Error $_ -ErrorAction Continue
    exit 1
}finally{
    foreach($name in $saved.Keys){if($null -eq $saved[$name]){Remove-Item "Env:$name" -ErrorAction SilentlyContinue}else{[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}}
}
