param(
    [Parameter(Mandatory=$true)][string]$TestHost,
    [string[]]$TestCase=@('layoutAndDisclosure','startupFailureKeepsUiUsable','modelLoadAndReuse','startIncludesPreparation','automaticStartFailureKeepsStage','realButtonsAndTaskLifecycle','runtimeTuningButtons','busyHostIsSkipped')
)
$ErrorActionPreference='Stop'
$root=(Resolve-Path "$PSScriptRoot\..\..\..").Path
if(-not(Test-Path -LiteralPath (Join-Path $TestHost 'Bin64\mworks.exe'))){throw 'Dedicated test host is missing'}
$env:SYSPLORER_HOME=$TestHost
$env:BATTERY_HOST_TIMEOUT_MS='180000'
$env:BATTERY_SIM_WORKSPACE=Join-Path $env:TEMP 'B0cf2Jobs'
$env:BATTERY_TEST_ARTIFACTS=Join-Path $root 'logs\button_details'
New-Item -ItemType Directory -Path $env:BATTERY_TEST_ARTIFACTS -Force | Out-Null
$env:PATH='C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\bin;'+$env:PATH
$env:QT_QPA_PLATFORM='windows'
$env:QT_QPA_PLATFORM_PLUGIN_PATH='C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\plugins\platforms'
$stamp=Get-Date -Format 'yyyyMMdd_HHmmss'
$env:BATTERY_TEST_RUN_ID=$stamp
$report=Join-Path $env:BATTERY_TEST_ARTIFACTS "buttons_$stamp.txt"
$testArgs=@('-o','-,txt','-v1')+$TestCase
$test=Start-Process -FilePath "$PSScriptRoot\build\release\ui_integration_test.exe" -ArgumentList $testArgs -WindowStyle Hidden -PassThru -RedirectStandardOutput $report -RedirectStandardError (Join-Path $env:BATTERY_TEST_ARTIFACTS "stderr_$stamp.txt")
# A timed WaitForExit observes the test process itself, not detached host descendants.
if(-not $test.WaitForExit(3600000)){throw 'QtTest exceeded the one-hour guard'}
try {Get-Content -LiteralPath $report -Tail 60} catch {Write-Output "Report retained (a detached host still holds its log handle): $report"}
if($test.ExitCode -ne 0){throw "QtTest failed: $($test.ExitCode). Report: $report"}
Write-Output "QtTest passed. Report: $report"
