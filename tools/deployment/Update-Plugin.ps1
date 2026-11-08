param([string]$SysplorerHome='C:\Program Files\MWORKS\Sysplorer 2026a')
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -ErrorAction Stop
$hostRoot=[IO.Path]::GetFullPath($SysplorerHome)
$hostExe=Join-Path $hostRoot 'Bin64\mworks.exe'
$source=Join-Path $PSScriptRoot 'battery_udp_control.dll'
$target=Join-Path $hostRoot 'Bin64\addins\BatteryUdpControl\battery_udp_control.dll'
if(-not(Test-Path -LiteralPath $hostExe -PathType Leaf)){throw "Sysplorer not found: $hostExe"}
if(-not(Test-Path -LiteralPath $source -PathType Leaf)){throw 'Matching plugin DLL missing from this package'}
foreach($process in @(Get-Process mworks -ErrorAction SilentlyContinue)){
    if(-not $process.Path){throw 'Cannot inspect a running Sysplorer. Run as Administrator and close the target instance first.'}
    if([IO.Path]::GetFullPath($process.Path) -eq $hostExe){throw "Target Sysplorer is running (PID $($process.Id)). Save work and close every instance of this installation; nothing was changed."}
}
$sourceHash=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$manifestPath=Join-Path $PSScriptRoot 'manifest.json'
if(Test-Path -LiteralPath $manifestPath){
    $manifest=Get-Content -LiteralPath $manifestPath -Raw|ConvertFrom-Json
    $entry=@($manifest.files|Where-Object {$_.path -eq 'battery_udp_control.dll'})
    if($entry.Count -ne 1 -or $entry[0].sha256 -ne $sourceHash){throw 'Package plugin hash differs from manifest; nothing was installed'}
}
$existed=Test-Path -LiteralPath $target -PathType Leaf
$oldHash=if($existed){(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash}else{''}
$backup=''
if($existed -and $oldHash -ne $sourceHash){
    $backup=Join-Path $PSScriptRoot "versions\$oldHash.dll"
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($backup))|Out-Null
    if(-not(Test-Path -LiteralPath $backup)){Copy-Item -LiteralPath $target -Destination $backup}
    if((Get-FileHash -LiteralPath $backup).Hash -ne $oldHash){throw 'Backup hash mismatch; nothing was installed'}
}
if($oldHash -ne $sourceHash){
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target))|Out-Null
    try{
        [IO.File]::WriteAllBytes($target,[IO.File]::ReadAllBytes($source))
        if((Get-FileHash -LiteralPath $target).Hash -ne $sourceHash){throw 'Installed DLL hash mismatch'}
    }catch{
        $failure=$_.Exception.Message
        if($existed -and $backup){
            try{[IO.File]::WriteAllBytes($target,[IO.File]::ReadAllBytes($backup))}catch{throw "Update failed: $failure. Backup: $backup. Automatic restore failed: $($_.Exception.Message)"}
        }elseif(-not $existed -and (Test-Path -LiteralPath $target)){Remove-Item -LiteralPath $target}
        throw "Plugin update failed: $failure. A protected installation requires Administrator PowerShell."
    }
}
[ordered]@{target=$target;previousSha256=$oldHash;installedSha256=$sourceHash;backup=$backup;updatedAt=(Get-Date).ToString('o');configurationChanged=$false}|
    ConvertTo-Json|Set-Content -LiteralPath (Join-Path $PSScriptRoot 'update-report.json') -Encoding UTF8
Write-Output "Plugin ready: $target"
Write-Output "SHA256: $sourceHash"
if($backup){Write-Output "Previous DLL backup: $backup"}
Write-Output 'remote.ini, access keys, port and firewall were not changed. Restart Sysplorer, then explicitly reconnect Qt.'
