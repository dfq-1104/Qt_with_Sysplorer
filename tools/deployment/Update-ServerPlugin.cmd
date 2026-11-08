@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Update-ConfiguredServer.ps1" %*
if errorlevel 1 echo Update failed. Read the error above. Configuration and credentials were not changed.
pause
