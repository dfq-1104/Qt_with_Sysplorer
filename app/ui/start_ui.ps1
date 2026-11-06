$ErrorActionPreference='Stop'
$uiRoot=$PSScriptRoot
$repoRoot=[IO.Path]::GetFullPath((Join-Path $uiRoot '..\..'))
$package=if(Test-Path -LiteralPath (Join-Path $uiRoot 'runtime_buttons\sysplorer.ini')){'runtime_buttons'}elseif(Test-Path -LiteralPath (Join-Path $uiRoot 'runtime_tuning\sysplorer.ini')){'runtime_tuning'}else{'runtime'}
$exe=Join-Path $uiRoot "$package\battery_console_ui.exe"
$configPath=Join-Path $uiRoot "$package\sysplorer.ini"
$configuredHome=''
if(Test-Path -LiteralPath $configPath){
    $homeLine=Get-Content -LiteralPath $configPath | Where-Object {$_ -match '^Home='} | Select-Object -First 1
    if($homeLine){$configuredHome=$homeLine.Substring(5).Trim()}
}
$source=Join-Path $repoRoot 'tools\sysplorer_udp_plugin\release\battery_udp_control.dll'
$hostRoot=if($env:SYSPLORER_HOME){$env:SYSPLORER_HOME}elseif($configuredHome){$configuredHome}else{'C:\Program Files\MWORKS\Sysplorer 2026a'}
$target=Join-Path $hostRoot 'Bin64\addins\BatteryUdpControl\battery_udp_control.dll'
function Get-BinaryHash([string]$path) {
    $stream=[IO.File]::OpenRead($path)
    $sha=[Security.Cryptography.SHA256]::Create()
    try {return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','')} finally {$sha.Dispose();$stream.Dispose()}
}
try {    if(-not (Test-Path -LiteralPath $exe)){throw 'Please build and deploy the Qt application first.'}
    if(-not (Test-Path -LiteralPath $source)){throw 'Please build tools/sysplorer_udp_plugin first.'}
    $expected=(Get-BinaryHash $source)
    $matches=(Test-Path -LiteralPath $target) -and ((Get-BinaryHash $target) -eq $expected)
    if(-not $matches){
        $running=Get-Process mworks -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq (Join-Path $hostRoot 'Bin64\mworks.exe')}
        if($running){throw 'Plugin version mismatch. Save your models and close Sysplorer, then run this launcher again.'}
        Write-Host 'Synchronizing the matching Sysplorer plugin (Windows administrator authorization may appear)...'
        & (Join-Path $repoRoot 'tools\sysplorer_udp_plugin\deploy.ps1') -SysplorerHome $hostRoot -Elevate
        if($LASTEXITCODE -ne 0){throw 'Sysplorer plugin deployment failed.'}
        if((Get-BinaryHash $target) -ne $expected){throw 'Installed plugin does not match this application build.'}
    }
    $env:BATTERY_PROJECT_DIR=$repoRoot
    Start-Process -FilePath $exe -WorkingDirectory (Split-Path -Parent $exe)
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    Read-Host 'Press Enter to close'
    exit 1
}