param([string]$SysplorerHome='',[string]$Workspace='C:\BatteryConsoleData\jobs',[int]$Port=39001,[string]$AllowedClientAddress='LocalSubnet',[switch]$DryRun)
$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot 'Setup-Deployment.ps1') -Mode Server -SysplorerHome $SysplorerHome -Workspace $Workspace -Port $Port -AllowedClientAddress $AllowedClientAddress -DryRun:$DryRun
