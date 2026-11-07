$ErrorActionPreference='Stop'
foreach($key in @('QT_QPA_PLATFORM','QT_QPA_PLATFORM_PLUGIN_PATH','QT_PLUGIN_PATH','PYTHONHOME','PYTHONPATH')){Remove-Item "Env:$key" -ErrorAction SilentlyContinue}
$env:PATH=$PSScriptRoot+';'+$env:PATH
Start-Process -FilePath (Join-Path $PSScriptRoot 'battery_console_ui.exe') -WorkingDirectory $PSScriptRoot
