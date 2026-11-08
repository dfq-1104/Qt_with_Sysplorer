@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Diagnose-Plugin.ps1" %*
if errorlevel 1 echo Diagnosis failed. Read the error above.
pause
