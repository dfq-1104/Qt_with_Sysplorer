param([string]$SysplorerHome='',[string]$OutputFile='')
$ErrorActionPreference='Stop'
foreach($module in @('Utility','Management')){Import-Module (Join-Path $PSHOME ('Modules\Microsoft.PowerShell.'+$module+'\Microsoft.PowerShell.'+$module+'.psd1')) -ErrorAction Stop}
if(-not $SysplorerHome){
    $receipt=Join-Path $env:LOCALAPPDATA 'BatteryConsole\server\last-server.json'
    if(-not(Test-Path -LiteralPath $receipt)){throw 'Server receipt missing. Supply -SysplorerHome using the installed Sysplorer directory.'}
    $SysplorerHome=(Get-Content -LiteralPath $receipt -Raw|ConvertFrom-Json).sysplorerHome
}
if(-not $OutputFile){$OutputFile=Join-Path $env:LOCALAPPDATA 'BatteryConsole\server\plugin-diagnostic.json'}
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($OutputFile)))|Out-Null
& (Join-Path $PSScriptRoot 'Diagnose-Server.ps1') -SysplorerHome $SysplorerHome -OutputFile $OutputFile
