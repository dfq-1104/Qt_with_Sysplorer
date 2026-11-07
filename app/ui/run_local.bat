@echo off
setlocal
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\tools\deployment\Start-LocalWorktree.ps1"
exit /b %errorlevel%
