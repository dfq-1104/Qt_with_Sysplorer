# Compatibility entry point. New deployments should use Setup-Server.cmd.
param([string]$SysplorerHome='', [string]$ListenAddress='0.0.0.0',
      [ValidateRange(1,65535)][int]$Port=39001, [string]$Workspace='C:\BatteryConsoleData\jobs',
      [string]$ServerUser=[Security.Principal.WindowsIdentity]::GetCurrent().Name,
      [switch]$OpenFirewall, [string]$AllowedClientAddress='LocalSubnet', [switch]$DisableRemote)
$ErrorActionPreference='Stop'
$sid=(New-Object Security.Principal.NTAccount($ServerUser)).Translate([Security.Principal.SecurityIdentifier]).Value
$mode=if($DisableRemote){'Local'}else{'Server'}
& (Join-Path $PSScriptRoot 'Setup-Deployment.ps1') -Mode $mode -SysplorerHome $SysplorerHome -Workspace $Workspace -Port $Port -ListenAddress $ListenAddress -AllowedClientAddress $AllowedClientAddress -UserSid $sid -SkipFirewall:(-not $OpenFirewall)
