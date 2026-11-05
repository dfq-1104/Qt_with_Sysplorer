@echo off
setlocal
cd /d "%~dp0"
set "BATTERY_PROJECT_DIR=%~dp0..\.."
if not exist "runtime\battery_console_ui.exe" (
  echo Please build and deploy the application first.
  pause
  exit /b 1
)
start "" "runtime\battery_console_ui.exe"
