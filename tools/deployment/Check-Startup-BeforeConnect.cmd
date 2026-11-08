@echo off
cd /d "%~dp0"
set "BATTERY_TEST_ARTIFACTS=%~dp0diagnostics-startup-check"
set "BATTERY_TEST_CONFIG_ROOT=%~dp0diagnostics-startup-check\settings"
if not exist "%BATTERY_TEST_ARTIFACTS%" mkdir "%BATTERY_TEST_ARTIFACTS%"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-Client.ps1" -SmokeTest