param([string]$BuildName=('repair-'+(Get-Date -Format 'yyyyMMdd-HHmmss')))
$ErrorActionPreference='Stop'
if($BuildName -notmatch '^[a-zA-Z0-9_-]+$'){throw 'BuildName must contain ASCII letters, numbers, underscore or hyphen only'}
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$output=Join-Path $root ('build\'+$BuildName)
if(Test-Path -LiteralPath $output){throw 'Clean build requires a new directory; choose another BuildName'}
[IO.Directory]::CreateDirectory($output)|Out-Null
$lines=@('@echo off','call "C:\Program Files (x86)\Microsoft Visual Studio\2017\Professional\VC\Auxiliary\Build\vcvars64.bat"','if errorlevel 1 exit /b 1','set "PATH=C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\bin;%PATH%"')
$projects=[ordered]@{ui='app/ui/ui.pro';plugin='tools/sysplorer_udp_plugin/plugin.pro';validation='app/ui/tests/model_validation_tests.pro';transfer='app/ui/tests/model_transfer_tests.pro';remote='app/ui/tests/remote_tests.pro';build_startup_fixture='app/ui/tests/startup_fixture.pro';layout='app/ui/tests/tests.pro'}
foreach($entry in $projects.GetEnumerator()){
    $lines+=@(('mkdir '+$entry.Key),('cd '+$entry.Key),('qmake "../../../'+$entry.Value+'" CONFIG+=release'),'if errorlevel 1 exit /b 1','nmake /nologo','if errorlevel 1 exit /b 1','cd ..')
}
$lines+='exit /b 0'
[IO.File]::WriteAllText((Join-Path $output 'build.cmd'),($lines -join "`r`n")+"`r`n",[Text.Encoding]::ASCII)
Push-Location $output
try{& cmd.exe /d /c build.cmd *> build.log;if($LASTEXITCODE){throw "Clean build failed; see $output\build.log"}}finally{Pop-Location}
Write-Output "Clean build completed: $output"
