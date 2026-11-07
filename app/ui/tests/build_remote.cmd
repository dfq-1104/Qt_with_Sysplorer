@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2017\Professional\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build_startup_fixture mkdir build_startup_fixture
pushd build_startup_fixture
"C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\bin\qmake.exe" ..\startup_fixture.pro
if errorlevel 1 exit /b 1
nmake /nologo
if errorlevel 1 exit /b 1
popd
if not exist build_remote mkdir build_remote
cd build_remote
"C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\bin\qmake.exe" ..\remote_tests.pro
if errorlevel 1 exit /b 1
nmake /nologo
exit /b %errorlevel%
