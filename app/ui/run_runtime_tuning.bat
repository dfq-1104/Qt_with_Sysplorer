@echo off
setlocal
cd /d "%~dp0"
set "QT_QPA_PLATFORM=windows"
set "QT_QPA_PLATFORM_PLUGIN_PATH="
set "QT_PLUGIN_PATH="
if not defined SYSPLORER_HOME if not exist "runtime_tuning\sysplorer.ini" (
  echo Please deploy runtime_tuning with -SysplorerHome pointing to the prepared isolated host.
  exit /b 1
)
if not exist "runtime_tuning\battery_console_ui.exe" (
  echo Please deploy the runtime_tuning UI package first.
  exit /b 1
)
start "" "runtime_tuning\battery_console_ui.exe"
