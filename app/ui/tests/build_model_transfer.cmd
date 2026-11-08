@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2017\Professional\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build_model_transfer mkdir build_model_transfer
cd build_model_transfer
"C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\bin\qmake.exe" ..\model_transfer_tests.pro
if errorlevel 1 exit /b 1
nmake /nologo
exit /b %errorlevel%
