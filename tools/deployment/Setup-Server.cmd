@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Setup-Server.ps1" %*
if errorlevel 1 echo Setup failed. See the error above.
pause
