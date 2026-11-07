param([switch]$SmokeTest,[switch]$PassThru)
$ErrorActionPreference='Stop'
$binding=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'local-worktree.json') -Raw -Encoding UTF8|ConvertFrom-Json
& (Join-Path $binding.worktreeRoot 'tools\deployment\Start-LocalWorktree.ps1') -PreserveConnectionMode -SmokeTest:$SmokeTest -PassThru:$PassThru
