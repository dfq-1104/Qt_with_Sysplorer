param([string]$SysplorerHome='',[string]$ClientStatePath=(Join-Path $env:LOCALAPPDATA 'BatteryConsole\local-runtime.json'),[string]$Workspace='C:\BatteryConsoleData\jobs')
$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot 'Setup-Deployment.ps1') -Mode Local -SysplorerHome $SysplorerHome -ClientStatePath $ClientStatePath -Workspace $Workspace
