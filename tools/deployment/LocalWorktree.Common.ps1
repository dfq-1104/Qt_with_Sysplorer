$ErrorActionPreference='Stop'
if(-not ('BatteryWorktree.NativePaths' -as [type])){
    Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
namespace BatteryWorktree {
    public static class NativePaths {
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
        public static extern uint QueryDosDevice(string name, StringBuilder target, int count);
    }
}
'@
}
function Get-WorktreeFile([string]$Root,[string]$Relative){
    $base=[IO.Path]::GetFullPath($Root).TrimEnd('\')
    $path=[IO.Path]::GetFullPath((Join-Path $base $Relative))
    if(-not $path.StartsWith($base+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Local runtime path must stay inside this worktree'}
    $check=$path
    while($check -and $check -ne $base){
        if(Test-Path -LiteralPath $check){
            if((Get-Item -LiteralPath $check -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Local output must not follow a junction: $check"}
        }
        $check=[IO.Path]::GetDirectoryName($check)
    }
    return $path
}
function Get-WorktreeDriveTarget([string]$Letter){
    $buffer=New-Object Text.StringBuilder 32768
    if(-not [BatteryWorktree.NativePaths]::QueryDosDevice(($Letter.TrimEnd(':')+':'),$buffer,$buffer.Capacity)){return ''}
    $value=$buffer.ToString()
    if($value.StartsWith('\??\')){return [IO.Path]::GetFullPath($value.Substring(4)).TrimEnd('\')}
    return $value
}
function Connect-WorktreeDrive([string]$Root,[string]$Preferred=''){
    $base=[IO.Path]::GetFullPath($Root).TrimEnd('\')
    $letters=@();if($Preferred){$letters+=($Preferred.TrimEnd(':').ToUpperInvariant())}
    $letters+=@('Z','Y','X','W','V','U','T')
    foreach($letter in @($letters|Select-Object -Unique)){
        if($letter -notmatch '^[T-Z]$'){throw 'Local worktree drive must use a letter from T to Z'}
        $target=Get-WorktreeDriveTarget $letter
        if($target -eq $base){return [pscustomobject]@{letter=$letter;root=($letter+':\');created=$false}}
        if($target -or (Test-Path ($letter+':\'))){continue}
        & "$env:SystemRoot\System32\subst.exe" ($letter+':') $base
        if($LASTEXITCODE -or (Get-WorktreeDriveTarget $letter) -ne $base){throw 'Could not verify the worktree drive mapping'}
        return [pscustomobject]@{letter=$letter;root=($letter+':\');created=$true}
    }
    throw 'No free worktree drive letter (T-Z). Existing drives were left unchanged.'
}
function Remove-WorktreeDrive([string]$Root,[string]$Letter){
    if((Get-WorktreeDriveTarget $Letter) -ne [IO.Path]::GetFullPath($Root).TrimEnd('\')){throw 'Drive mapping does not belong to this worktree; left unchanged'}
    & "$env:SystemRoot\System32\subst.exe" ($Letter.TrimEnd(':')+':') /D
    if($LASTEXITCODE){throw 'Could not remove worktree mapping'}
}
function Save-WorktreeJson($Value,[string]$Path){
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path))|Out-Null
    $json=$Value|ConvertTo-Json -Depth 7
    [IO.File]::WriteAllText($Path,$json,(New-Object Text.UTF8Encoding $true))
}
function Write-LocalClientManifest([string]$Client,[string]$ModelHash){
    $files=@(Get-ChildItem -LiteralPath $Client -Recurse -File|Where-Object {$_.Name -ne 'manifest.json'}|Sort-Object FullName|ForEach-Object{
        [ordered]@{path=$_.FullName.Substring($Client.Length+1).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()}
    })
    Save-WorktreeJson ([ordered]@{architecture='win64';deployment='local-worktree';modelHash=$ModelHash;files=$files}) (Join-Path $Client 'manifest.json')
}
function Write-LocalHostConfiguration([string]$Client,[string]$HomePath){
    $path=Join-Path $Client 'sysplorer.ini'
    $other=New-Object 'Collections.Generic.List[string]'
    $hostOptions=New-Object 'Collections.Generic.List[string]'
    $section=''
    if(Test-Path -LiteralPath $path){
        foreach($line in [IO.File]::ReadAllLines($path,[Text.Encoding]::UTF8)){
            if($line -match '^\s*\[([^]]+)\]'){
                $section=$Matches[1]
                if($section -ne 'Host'){$other.Add($line)}
            }elseif($section -eq 'Host'){
                if($line -notmatch '^\s*Home\s*='){$hostOptions.Add($line)}
            }else{$other.Add($line)}
        }
    }
    if(-not @($hostOptions|Where-Object {$_ -match '^\s*TimeoutMs\s*='}).Count){$hostOptions.Add('TimeoutMs=180000')}
    $lines=@($other)+@('[Host]',('Home='+$HomePath.Replace('\','/')))+@($hostOptions)
    if(-not @($lines|Where-Object {$_ -match '^\s*WorktreeBound\s*=\s*true\s*$'}).Count){$lines+=@('[Runtime]','WorktreeBound=true')}
    [IO.File]::WriteAllText($path,($lines -join "`r`n")+"`r`n",(New-Object Text.UTF8Encoding $true))
}
