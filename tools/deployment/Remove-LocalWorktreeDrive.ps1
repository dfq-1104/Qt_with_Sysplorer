$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'LocalWorktree.Common.ps1')
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$stateFile=Get-WorktreeFile $root '.local-sysplorer\state.json'
$state=Get-Content -LiteralPath $stateFile -Raw -Encoding UTF8|ConvertFrom-Json
if(-not $state.driveLetter){Write-Output 'No worktree drive mapping recorded.';return}
if(-not(Get-WorktreeDriveTarget $state.driveLetter)){Write-Output 'Worktree drive mapping is already absent.';return}
if((Get-WorktreeDriveTarget $state.driveLetter) -ne $root){throw 'Drive now belongs to another target; left unchanged'}
foreach($process in @(Get-Process mworks,battery_console_ui -ErrorAction SilentlyContinue)){
    $path=$process.Path
    if(-not $path){throw 'A relevant process cannot be inspected; mapping left unchanged'}
    if($path.StartsWith($state.driveLetter+':\',[StringComparison]::OrdinalIgnoreCase)){$path=Join-Path $root $path.Substring(3)}
    if($path.StartsWith((Join-Path $root '.local-sysplorer')+'\',[StringComparison]::OrdinalIgnoreCase) -or
       $path.StartsWith((Join-Path $root 'dist\local-worktree')+'\',[StringComparison]::OrdinalIgnoreCase)){
        throw "Local worktree program is still running (PID $($process.Id)); mapping left unchanged. Close it yourself first."
    }
}
Remove-WorktreeDrive $root $state.driveLetter
Write-Output 'Removed only this worktree drive mapping. The next local start recreates it.'
