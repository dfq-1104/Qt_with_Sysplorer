@echo off
setlocal
set "BATTERY_REMOTE_TARGET=%~1"
set "BATTERY_REMOTE_TCP_PORT=%~2"
if not defined BATTERY_REMOTE_TARGET set "BATTERY_REMOTE_TARGET=172.19.15.159"
if not defined BATTERY_REMOTE_TCP_PORT set "BATTERY_REMOTE_TCP_PORT=39001"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\tools\deployment\Start-RemoteChecked.ps1" -Address "%BATTERY_REMOTE_TARGET%" -Port "%BATTERY_REMOTE_TCP_PORT%" -KeyFile "%~dp0..\..\logs\remote-vm\access-key.txt"
if errorlevel 1 pause
endlocal
