@echo off
setlocal
cd /d "%~dp0"
set "SYSPLORER_HOME=%TEMP%\BatteryConsoleRuntime"
set "QT_QPA_PLATFORM=windows"
set "QT_QPA_PLATFORM_PLUGIN_PATH="
set "QT_PLUGIN_PATH="
if not exist "%SYSPLORER_HOME%\Bin64\mworks.exe" (
  echo Please run tools/sysplorer_udp_plugin/prepare_isolated_host.ps1 first.
  exit /b 1
)
if not exist "runtime_tuning\battery_console_ui.exe" (
  echo Please deploy the runtime_tuning UI package first.
  exit /b 1
)
start "" "runtime_tuning\battery_console_ui.exe"
