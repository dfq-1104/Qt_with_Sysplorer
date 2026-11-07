param([Parameter(Mandatory=$true)][string]$SysplorerHome)
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -ErrorAction Stop
$patchDirectory=Split-Path -Parent $MyInvocation.MyCommand.Path
$manifestPath=Join-Path $patchDirectory 'patch-manifest.json'
$manifest=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8|ConvertFrom-Json
$source=Join-Path $patchDirectory 'battery_udp_control.dll'
$hostRoot=[IO.Path]::GetFullPath($SysplorerHome)
$hostExe=Join-Path $hostRoot 'Bin64\mworks.exe'
$target=Join-Path $hostRoot 'Bin64\addins\BatteryUdpControl\battery_udp_control.dll'
$config=Join-Path $hostRoot 'Bin64\addins\BatteryUdpControl\remote.ini'
if(-not(Test-Path -LiteralPath $hostExe) -or -not(Test-Path -LiteralPath $target) -or -not(Test-Path -LiteralPath $config)){throw 'Existing host, plugin and remote.ini are required. This patch does not perform a first installation.'}
if((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $manifest.newPluginSha256){throw 'Patch DLL hash does not match the patch manifest'}
$existingHash=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
foreach($process in @(Get-Process mworks -ErrorAction SilentlyContinue)){
    if(-not $process.Path -or $process.Path -eq $hostExe){throw 'Save your work and close the target Sysplorer before updating its DLL'}
}
if($manifest.allowedHostVersions -and (Get-Item -LiteralPath $hostExe).VersionInfo.FileVersion -notin @($manifest.allowedHostVersions)){throw 'Host build is not an allowed patch target; left unchanged'}
if($existingHash -eq $manifest.newPluginSha256){Write-Output 'This plugin patch is already installed. Configuration and credentials were not changed.';return}
if($existingHash -notin @($manifest.allowedPreviousSha256)){throw "Installed DLL is not a recognized patch target. Left unchanged. SHA256: $existingHash"}
$configurationHash=(Get-FileHash -LiteralPath $config -Algorithm SHA256).Hash
$backup=Join-Path $patchDirectory ('battery_udp_control.'+(Get-Date -Format 'yyyyMMdd-HHmmssfff')+'.dll.bak')
Copy-Item -LiteralPath $target -Destination $backup
if((Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash -ne $existingHash){throw 'Backup verification failed; no DLL was updated'}
try{
    Copy-Item -LiteralPath $source -Destination $target -Force
    if((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $manifest.newPluginSha256){throw 'Updated DLL verification failed'}
    if((Get-FileHash -LiteralPath $config -Algorithm SHA256).Hash -ne $configurationHash){throw 'Configuration changed during update; check the existing remote.ini'}
}catch{
    try{Copy-Item -LiteralPath $backup -Destination $target -Force}catch{Write-Warning "Automatic DLL rollback failed. Restore from: $backup"}
    throw
}
Write-Output "Updated plugin: $target"
Write-Output "SHA256: $($manifest.newPluginSha256)"
Write-Output "Old DLL backup: $backup"
Write-Output 'remote.ini, access credentials and firewall rules were preserved. Start Sysplorer and check TCP 39001 again.'
