param([string]$SysplorerHome='C:\Program Files\MWORKS\Sysplorer 2026a',[switch]$Elevate)
$ErrorActionPreference='Stop'
$source=Join-Path $PSScriptRoot 'release\battery_udp_control.dll'
$targetFolder=Join-Path $SysplorerHome 'Bin64\addins\BatteryUdpControl'
$target=Join-Path $targetFolder 'battery_udp_control.dll'
$report=Join-Path $PSScriptRoot 'deployment.json'
$isAdmin=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
try {
    if(-not $isAdmin){
        if(-not $Elevate){throw 'Administrator permission required. Run this script with -Elevate and accept Windows UAC, or open PowerShell as Administrator.'}
        Write-Host 'Requesting Windows administrator authorization for the dedicated plugin file...'
        $args='-NoProfile -ExecutionPolicy Bypass -File "'+$PSCommandPath+'" -SysplorerHome "'+$SysplorerHome+'"'
        $child=Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -ArgumentList $args -Verb RunAs -WindowStyle Hidden -PassThru -Wait
        if($child.ExitCode -ne 0){
            $detail=''
            if(Test-Path -LiteralPath $report){$detail=(Get-Content -LiteralPath $report -Raw|ConvertFrom-Json).error}
            throw "Elevated deployment failed, exit $($child.ExitCode): $detail"
        }
        $result=Get-Content -LiteralPath $report -Raw|ConvertFrom-Json
        if(-not $result.ok){throw $result.error}
        Write-Host ("Deployed: "+$result.target)
        exit 0
    }
    if(-not (Test-Path -LiteralPath (Join-Path $SysplorerHome 'Bin64\mworks.exe'))){throw 'Target Sysplorer not found'}
    $sourceHash=(Get-FileHash -LiteralPath $source).Hash
    if(Test-Path -LiteralPath $target){
        $oldHash=(Get-FileHash -LiteralPath $target).Hash
        if($oldHash -ne $sourceHash){
            $backup=Join-Path $PSScriptRoot ('versions\'+$oldHash+'.dll')
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($backup))|Out-Null
            [IO.File]::WriteAllBytes($backup,[IO.File]::ReadAllBytes($target))
        }
    }
    [IO.Directory]::CreateDirectory($targetFolder)|Out-Null
    if(-not (Test-Path -LiteralPath $target) -or (Get-FileHash -LiteralPath $target).Hash -ne $sourceHash){
        [IO.File]::WriteAllBytes($target,[IO.File]::ReadAllBytes($source))
    }
    if((Get-FileHash -LiteralPath $target).Hash -ne $sourceHash){throw 'Deployment hash mismatch'}
    @{ok=$true;target=$target;administrator=$isAdmin;sha256=$sourceHash;utc=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json|Set-Content -LiteralPath $report -Encoding utf8
    Write-Host "SUCCESS: $target"
    Write-Host "SHA256: $sourceHash"
} catch {
    $message=$_.Exception.Message
    @{ok=$false;administrator=$isAdmin;error=$message;utc=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json|Set-Content -LiteralPath $report -Encoding utf8
    Write-Error -Message $message -ErrorAction Continue
    Write-Host "Details: $report"
    exit 1
}
