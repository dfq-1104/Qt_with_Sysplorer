param([string]$SysplorerHome='')
$ErrorActionPreference='Stop'
if(-not $SysplorerHome){
    $receipt=Join-Path $env:LOCALAPPDATA 'BatteryConsole\server\last-server.json'
    if(-not(Test-Path -LiteralPath $receipt)){throw 'Server receipt missing. Use Update-Plugin.ps1 -SysplorerHome with the actual installed directory.'}
    $SysplorerHome=(Get-Content -LiteralPath $receipt -Raw|ConvertFrom-Json).sysplorerHome
}
$isAdmin=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if($SysplorerHome.StartsWith($env:ProgramFiles+'\',[StringComparison]::OrdinalIgnoreCase) -and -not $isAdmin){throw 'Right-click Update-ServerPlugin.cmd and choose Run as administrator. Save and close the selected Sysplorer first. No changes were made.'}
& (Join-Path $PSScriptRoot 'Update-Plugin.ps1') -SysplorerHome $SysplorerHome
