param([string]$TestCase='')
$ErrorActionPreference='Stop'
$root=(Resolve-Path "$PSScriptRoot\..\..\..").Path
$env:BATTERY_PROJECT_DIR=$root
$env:BATTERY_SIM_WORKSPACE=Join-Path $env:TEMP 'BatterySplitLifecycleTests'
$env:BATTERY_TEST_ARTIFACTS=Join-Path $root 'logs\split_lifecycle'
New-Item -ItemType Directory -Path $env:BATTERY_TEST_ARTIFACTS -Force | Out-Null
$env:PATH="C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\bin;"+$env:PATH
$env:QT_QPA_PLATFORM='windows'
$env:QT_QPA_FONTDIR='C:\Windows\Fonts'
$env:QT_QPA_PLATFORM_PLUGIN_PATH='C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\plugins\platforms'
$testOutput=Join-Path $env:BATTERY_TEST_ARTIFACTS ('ui_integration_'+(Get-Date -Format 'yyyyMMdd_HHmmss')+'.txt')
$arguments=@('-o',"${testOutput},txt",'-v1')
if($TestCase){$arguments+= $TestCase}
& "$PSScriptRoot\build\release\ui_integration_test.exe" @arguments
$testExitCode=$LASTEXITCODE
# A detached host can inherit QtTest's log handle; its lifetime must not turn a passed test into a script failure.
try {Get-Content -LiteralPath $testOutput -Tail 30} catch {Write-Output "QtTest report saved at $testOutput (host still holds the file handle)."}
if($testExitCode -ne 0){throw "UI integration test failed: $testExitCode"}
Write-Output "QtTest passed; exit code $testExitCode. Report: $testOutput"