@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-Client.ps1" %*
if errorlevel 1 (
    echo Client launcher failed. Read the error above.
    pause
    exit /b 1
)
