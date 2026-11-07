param([Parameter(Mandatory=$true)][string]$Address,[int]$Port=39001,[string]$KeyFile='',
      [string]$ClientDirectory='', [switch]$VerifyCustom,[switch]$PassThru,[switch]$SmokeTest,[switch]$ConnectionTest)
$ErrorActionPreference='Stop'
if(([int]$VerifyCustom.IsPresent+[int]$SmokeTest.IsPresent+[int]$ConnectionTest.IsPresent) -gt 1){throw 'Choose one verification mode'}
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -ErrorAction Stop
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$localContext=$null
if(Test-Path -LiteralPath (Join-Path $root '.local-sysplorer\state.json')){
    $localContext=& (Join-Path $PSScriptRoot 'Start-LocalWorktree.ps1') -EnvironmentOnly
    if(-not $ClientDirectory){$ClientDirectory=$localContext.client}
}
if(-not $ClientDirectory){
    $ClientDirectory=Join-Path $root 'dist\remote-connect-fix-20261108\BatteryConsole-Client-win64'
    if(-not(Test-Path -LiteralPath (Join-Path $ClientDirectory 'battery_console_ui.exe'))){$ClientDirectory=Join-Path $root 'dist\remote-vm\BatteryConsole-Client-win64'}
}
$client=[IO.Path]::GetFullPath($ClientDirectory)
if(-not $client.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Client runtime must stay in the current worktree'}
$manifest=Get-Content -LiteralPath (Join-Path $client 'manifest.json') -Raw -Encoding UTF8|ConvertFrom-Json
& (Join-Path $PSScriptRoot 'Test-RemoteModel.ps1') -Address $Address -Port $Port -KeyFile $KeyFile -ExpectedModelHash $manifest.modelHash | Out-Host
$report=Get-Content -LiteralPath (Join-Path $root 'logs\remote-vm\model-preflight.json') -Raw -Encoding UTF8|ConvertFrom-Json
if(-not $report.authenticated -or -not $report.modelMatches -or -not $report.capabilitiesMatch -or -not $report.ready -or $report.controllerState -ne 'Idle'){
    throw 'Remote model preflight did not confirm a compatible, ready, idle host. No client task was started.'
}
$names=@('BATTERY_CONNECTION_MODE','BATTERY_REMOTE_ADDRESS','BATTERY_REMOTE_PORT','BATTERY_REMOTE_KEY','BATTERY_SIM_WORKSPACE','BATTERY_TEST_ARTIFACTS','PATH')
$previous=@{};foreach($name in $names){$previous[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
if($localContext -and $client -eq $localContext.client){foreach($name in $localContext.environment.Keys){$previous[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}}
try{
    if($localContext -and $client -eq $localContext.client){foreach($name in $localContext.environment.Keys){[Environment]::SetEnvironmentVariable($name,$localContext.environment[$name],'Process')}}
    $env:BATTERY_CONNECTION_MODE='remote';$env:BATTERY_REMOTE_ADDRESS=$Address;$env:BATTERY_REMOTE_PORT=$Port.ToString()
    if($KeyFile){$env:BATTERY_REMOTE_KEY=[IO.File]::ReadAllText([IO.Path]::GetFullPath($KeyFile),[Text.Encoding]::UTF8).Trim()}
    else{Remove-Item Env:BATTERY_REMOTE_KEY -ErrorAction SilentlyContinue}
    if(-not $localContext -or $client -ne $localContext.client){$env:BATTERY_SIM_WORKSPACE=Join-Path $root 'logs\remote-vm\client-results'}
    $env:BATTERY_TEST_ARTIFACTS=Join-Path $root 'logs\remote-vm\gui'
    [IO.Directory]::CreateDirectory($env:BATTERY_TEST_ARTIFACTS)|Out-Null
    $env:PATH=$client+';'+$env:PATH
    $options=@{FilePath=(Join-Path $client 'battery_console_ui.exe');WorkingDirectory=$client;PassThru=$true}
    if($VerifyCustom){$options.ArgumentList='--verify-custom';$options.WindowStyle='Hidden'}
    if($SmokeTest){$options.ArgumentList='--smoke-test';$options.WindowStyle='Hidden'}
    if($ConnectionTest){$options.ArgumentList='--verify-remote-connection';$options.WindowStyle='Hidden'}
    $process=Start-Process @options
    if($PassThru){return $process}
    Write-Output "Remote client started (PID $($process.Id)) for $Address`:$Port. It will not launch or fall back to a local host."
}finally{
    foreach($name in $previous.Keys){[Environment]::SetEnvironmentVariable($name,$previous[$name],'Process')}
}
