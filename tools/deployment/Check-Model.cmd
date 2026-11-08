@echo off
cd /d "%~dp0"
chcp 65001 >nul
"%~dp0battery_console_ui.exe" --check-model > "%~dp0model-check-result.txt" 2>&1
type "%~dp0model-check-result.txt"
pause
