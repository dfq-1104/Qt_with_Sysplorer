param(
    [Parameter(Mandatory=$true)][ValidateNotNullOrEmpty()][string]$Address,
    [ValidateRange(1,65535)][int]$Port=39001,
    [Parameter(Mandatory=$true)][string]$KeyFile,
    [switch]$PassThru,
    [switch]$SmokeTest,
    [switch]$ConnectionTest
)
$ErrorActionPreference='Stop'
if($SmokeTest -and $ConnectionTest){throw 'Choose one verification mode'}
$exe=Join-Path $PSScriptRoot 'battery_console_ui.exe'
if(-not(Test-Path -LiteralPath $exe)){throw 'Extract this script beside battery_console_ui.exe'}
$credential=[IO.File]::ReadAllText([IO.Path]::GetFullPath($KeyFile),[Text.Encoding]::UTF8).Trim()
if($credential.Length -lt 32){throw 'Use the existing server access-key.txt; at least 32 characters are required'}
$context=$null
if(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'local-worktree.json')){
    Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -ErrorAction Stop
    $binding=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'local-worktree.json') -Raw -Encoding UTF8|ConvertFrom-Json
    $context=& (Join-Path $binding.worktreeRoot 'tools\deployment\Start-LocalWorktree.ps1') -EnvironmentOnly
    if($context.client -ne $PSScriptRoot -and
       (Get-FileHash -LiteralPath $exe).Hash -ne (Get-FileHash -LiteralPath (Join-Path $context.client 'battery_console_ui.exe')).Hash){
        throw 'This copied client differs from the prepared version. Extract the current package again.'
    }
}
$names=@('BATTERY_CONNECTION_MODE','BATTERY_REMOTE_ADDRESS','BATTERY_REMOTE_PORT','BATTERY_REMOTE_KEY','PATH','QT_QPA_PLATFORM','QT_QPA_PLATFORM_PLUGIN_PATH','QT_PLUGIN_PATH','PYTHONHOME','PYTHONPATH')
$saved=@{};foreach($name in $names){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
if($context){foreach($name in $context.environment.Keys){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}}
try{
    if($context){foreach($name in $context.environment.Keys){[Environment]::SetEnvironmentVariable($name,$context.environment[$name],'Process')}}
    foreach($name in @('QT_QPA_PLATFORM','QT_QPA_PLATFORM_PLUGIN_PATH','QT_PLUGIN_PATH','PYTHONHOME','PYTHONPATH')){Remove-Item "Env:$name" -ErrorAction SilentlyContinue}
    # Explicit remote endpoint and credential take precedence over old saved
    # settings. No user/global settings or credential files are rewritten.
    $env:BATTERY_CONNECTION_MODE='remote';$env:BATTERY_REMOTE_ADDRESS=$Address.Trim();$env:BATTERY_REMOTE_PORT=$Port.ToString();$env:BATTERY_REMOTE_KEY=$credential
    $env:PATH=$PSScriptRoot+';'+$saved['PATH']
    $options=@{FilePath=$exe;WorkingDirectory=$PSScriptRoot;PassThru=$true;WindowStyle='Normal'}
    if($SmokeTest){$options.ArgumentList='--smoke-test';$options.WindowStyle='Hidden'}
    if($ConnectionTest){$options.ArgumentList='--verify-remote-connection';$options.WindowStyle='Hidden'}
    $started=Start-Process @options
    if($PassThru){return $started}
    Write-Output "Remote client opened for $Address`:$Port. Use Save and connect in Connection settings."
}finally{
    # PowerShell 7/.NET preserves empty strings: remove originally absent values.
    foreach($name in $saved.Keys){
        if($null -eq $saved[$name]){Remove-Item "Env:$name" -ErrorAction SilentlyContinue}
        else{[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}
    }
    $credential=$null
}
