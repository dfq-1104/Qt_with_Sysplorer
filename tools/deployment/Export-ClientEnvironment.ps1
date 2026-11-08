param([Parameter(Mandatory=$true)][string]$ClientExecutable)
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -ErrorAction Stop
[Console]::OutputEncoding=New-Object Text.UTF8Encoding($false)
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$state=Get-Content -LiteralPath (Join-Path $root '.local-sysplorer\state.json') -Raw -Encoding UTF8|ConvertFrom-Json
if($state.worktreeRoot -ne $root){throw 'Private runtime belongs to another worktree'}
$exe=[IO.Path]::GetFullPath($ClientExecutable)
if((Get-FileHash -LiteralPath $exe).Hash -ne $state.clientSha256){throw 'This client differs from the prepared runtime. Open the current worktree launcher or prepare a new generation.'}
$context=& (Join-Path $PSScriptRoot 'Start-LocalWorktree.ps1') -EnvironmentOnly
$environment=[ordered]@{}
foreach($name in $context.environment.Keys){$environment[$name]=$context.environment[$name]}
$environment['BATTERY_WORKTREE_ROOT']=$root
[ordered]@{schema=1;worktreeRoot=$root;clientSha256=$state.clientSha256;environment=$environment}|ConvertTo-Json -Depth 5 -Compress
