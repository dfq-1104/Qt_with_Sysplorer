@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Check-Server.ps1" %*
if errorlevel 1 echo Server is NOT ready. Read the reason above, then retry this check.
pause
