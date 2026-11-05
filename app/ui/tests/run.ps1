$ErrorActionPreference='Stop'
$root=(Resolve-Path "$PSScriptRoot\..\..\..").Path
$env:BATTERY_PROJECT_DIR=$root
$env:BATTERY_SYSPLORER_GUI='0'
$env:BATTERY_HELPER_EXE="$root\app\ui\runtime\helper\sdk_probe.exe"
$env:BATTERY_SIM_WORKSPACE='C:\Users\TR\.codex\visualizations\2026\11\03\01a1a14e-d533-7a73-8c71-2fa02f587ae3\battery_ui_tests'
$env:BATTERY_TEST_ARTIFACTS="$root\logs"
$env:PATH="C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\bin;"+$env:PATH
$env:QT_QPA_PLATFORM='offscreen'
$env:QT_QPA_FONTDIR='C:\Windows\Fonts'
$env:QT_QPA_PLATFORM_PLUGIN_PATH='C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\plugins\platforms'
$testOutput=Join-Path $root 'logs\ui_integration.txt'
& "$PSScriptRoot\build\release\ui_integration_test.exe" -o "${testOutput},txt" -v1
if($LASTEXITCODE -ne 0){throw "UI integration test failed: $LASTEXITCODE"}
Get-Content "$root\logs\ui_integration.txt" -Tail 15

