param([string]$TestCase='')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$artifacts=Join-Path $root 'logs\remote'
[IO.Directory]::CreateDirectory($artifacts)|Out-Null
$env:BATTERY_TEST_ROOT=$artifacts
$env:PATH='C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\bin;'+$env:PATH
$env:QT_QPA_PLATFORM='windows'
$env:QT_QPA_FONTDIR='C:\Windows\Fonts'
$env:QT_QPA_PLATFORM_PLUGIN_PATH='C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\plugins\platforms'
$report=Join-Path $artifacts ('remote_'+(Get-Date -Format 'yyyyMMdd_HHmmss')+'.txt')
$arguments=@('-o',"${report},txt",'-v1')
if($TestCase){$arguments+=$TestCase}
Push-Location $root
try{
    & (Join-Path $PSScriptRoot 'build_remote\release\remote_integration_test.exe') @arguments
    $code=$LASTEXITCODE
}finally{Pop-Location}
Get-Content -LiteralPath $report -Tail 30
if($code -ne 0){throw "Remote tests failed: $code"}
Write-Output "Remote tests passed: $report"
