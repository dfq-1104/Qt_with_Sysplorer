@echo off
setlocal
cd /d "%~dp0"
set "QT_QPA_PLATFORM=windows"
set "QT_QPA_PLATFORM_PLUGIN_PATH="
set "QT_PLUGIN_PATH="
if not defined SYSPLORER_HOME if not exist "runtime_buttons\sysplorer.ini" (
  echo Deploy runtime_buttons with -SysplorerHome pointing to a dedicated host first.
  exit /b 1
)
if not exist "runtime_buttons\battery_console_ui.exe" (
  echo Deploy the button-details UI package first.
  exit /b 1
)
start "" "runtime_buttons\battery_console_ui.exe"
