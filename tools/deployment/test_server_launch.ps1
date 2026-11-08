param([string]$BaselineLauncher='')
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -ErrorAction Stop
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$testRoot=Join-Path $root ('logs\remote-vm\server-path\test-'+[guid]::NewGuid().ToString('N'))
$fixtureHome=Join-Path $testRoot 'host with spaces'
$bin=Join-Path $fixtureHome 'Bin64'
$solverBin=Join-Path $fixtureHome 'Simulator\Bin64'
[IO.Directory]::CreateDirectory($bin)|Out-Null
[IO.Directory]::CreateDirectory($solverBin)|Out-Null
$fixture=Join-Path $root 'app\ui\tests\build_startup_fixture\release\mworks.exe'
Copy-Item -LiteralPath $fixture -Destination (Join-Path $bin 'mworks.exe')
Copy-Item -LiteralPath $fixture -Destination (Join-Path $solverBin 'MWSolver.exe')
foreach($name in @('Qt5Core.dll','Qt5Network.dll')){Copy-Item -LiteralPath (Join-Path 'C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\bin' $name) -Destination (Join-Path $bin $name)}
$package=Join-Path $testRoot 'package'
[IO.Directory]::CreateDirectory($package)|Out-Null
$launcher=Join-Path $package 'Start-Sysplorer.ps1'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Start-Sysplorer.ps1') -Destination $launcher
$workspace=Join-Path $testRoot 'jobs with spaces'
$receipt=Join-Path $package 'deployment.json'
@{sysplorerHome=$fixtureHome;workspace=$workspace}|ConvertTo-Json|Set-Content -LiteralPath $receipt -Encoding UTF8
$configuration=@($receipt,(Join-Path $package 'access-key.txt'),(Join-Path $bin 'remote.ini'))
foreach($path in $configuration|Where-Object {$_ -ne $receipt}){[IO.File]::WriteAllText($path,'public-test-only-existing-configuration')}
$configurationBefore=@{}
foreach($path in $configuration){$configurationBefore[$path]=@{hash=(Get-FileHash -LiteralPath $path).Hash;acl=(Get-Acl -LiteralPath $path).Sddl}}
function Assert-That($condition,[string]$message){if(-not $condition){throw $message}}
$names=@('PATH','BATTERY_SIM_WORKSPACE','BATTERY_LAUNCH_SENTINEL','BATTERY_FIXTURE_SOLVER','BATTERY_FIXTURE_UDP','QT_QPA_PLATFORM','QT_QPA_PLATFORM_PLUGIN_PATH','QT_PLUGIN_PATH','PYTHONHOME','PYTHONPATH')
$saved=@{}
foreach($name in $names){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
$results=@()
try{
    $scenarios=@('explicit','receipt')
    if($BaselineLauncher){$scenarios=@('before-fix')+$scenarios}
    foreach($scenario in $scenarios){
        $marker=Join-Path $testRoot ($scenario+'.jsonl')
        # Remove Qt and vendor paths: only the launcher can give the descendant
        # its dependency directory. Qt DLLs exist in host Bin64, never solver Bin64.
        $env:PATH=$env:SystemRoot+'\System32;'+$env:SystemRoot
        $env:BATTERY_SIM_WORKSPACE='original-caller-workspace'
        $env:BATTERY_LAUNCH_SENTINEL=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($marker))
        $env:BATTERY_FIXTURE_SOLVER='1'
        Remove-Item Env:BATTERY_FIXTURE_UDP -ErrorAction SilentlyContinue
        foreach($name in @('QT_QPA_PLATFORM','QT_QPA_PLATFORM_PLUGIN_PATH','QT_PLUGIN_PATH','PYTHONHOME','PYTHONPATH')){[Environment]::SetEnvironmentVariable($name,'poisoned-for-fixture','Process')}
        $caller=@{};foreach($name in $names){$caller[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
        $owned=$null
        try{
            if($scenario -eq 'before-fix'){& $BaselineLauncher -SysplorerHome $fixtureHome -Workspace $workspace}
            elseif($scenario -eq 'receipt'){$owned=& $launcher -Hidden -PassThru}
            else{$owned=& $launcher -SysplorerHome $fixtureHome -Workspace $workspace -Hidden -PassThru}
            $deadline=[DateTime]::UtcNow.AddSeconds(15)
            while(-not(Test-Path -LiteralPath $marker) -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 50}
            Assert-That (Test-Path -LiteralPath $marker) 'Fixture host failed to load'
            $hostRecord=Get-Content -LiteralPath $marker -Encoding UTF8|Select-Object -First 1|ConvertFrom-Json
            if(-not $owned){$owned=Get-Process -Id $hostRecord.pid -ErrorAction SilentlyContinue}
            if($owned){Assert-That ($owned.WaitForExit(15000)) 'Owned launcher fixture timed out'}
            $records=@(Get-Content -LiteralPath $marker -Encoding UTF8|ForEach-Object {$_|ConvertFrom-Json})
            $child=@($records|Where-Object role -eq 'solver')
            if($scenario -eq 'before-fix'){
                Assert-That ($child.Count -eq 0) 'Baseline unexpectedly found the descendant dependency'
                $results+=@{scenario=$scenario;expectedMissingChildDependency=$true;hostStarted=$true;solverStarted=$false}
                continue
            }
            Assert-That ($records.Count -eq 2 -and $child.Count -eq 1) 'Descendant could not load DLL from host Bin64'
            Assert-That ($owned.ExitCode -eq 0) 'Fixture host/solver exited unsuccessfully'
            foreach($record in $records){
                Assert-That ($record.path.StartsWith($bin+';'+$solverBin+';',[StringComparison]::OrdinalIgnoreCase)) 'Host runtime paths were not inherited'
                Assert-That ($record.workspace -eq $workspace) 'Child workspace differs from deployment receipt'
                Assert-That ($record.workingDirectory.Replace('/','\') -eq (Join-Path $workspace 'host')) 'Host working directory differs'
                foreach($name in @('QT_QPA_PLATFORM','QT_QPA_PLATFORM_PLUGIN_PATH','QT_PLUGIN_PATH','PYTHONHOME','PYTHONPATH')){Assert-That ([string]::IsNullOrEmpty($record.$name)) "Contaminated child environment: $name"}
            }
            foreach($name in $names){Assert-That ([Environment]::GetEnvironmentVariable($name,'Process') -ceq $caller[$name]) "Calling terminal environment changed: $name"}
            $results+=@{scenario=$scenario;hostStarted=$true;solverStarted=$true;exitCode=$owned.ExitCode;callerEnvironmentRestored=$true}
        }finally{
            if($owned -and -not $owned.HasExited){
                Assert-That ([IO.Path]::GetFullPath($owned.Path) -eq [IO.Path]::GetFullPath((Join-Path $bin 'mworks.exe'))) 'Refused to stop an unowned process'
                $owned.Kill();$owned.WaitForExit()
            }
        }
    }
}finally{
    foreach($name in $saved.Keys){
        if($null -eq $saved[$name]){Remove-Item "Env:$name" -ErrorAction SilentlyContinue}
        else{[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}
    }
}
foreach($path in $configuration){
    Assert-That ((Get-FileHash -LiteralPath $path).Hash -eq $configurationBefore[$path].hash) 'Configuration bytes changed'
    Assert-That ((Get-Acl -LiteralPath $path).Sddl -eq $configurationBefore[$path].acl) 'Configuration ACL changed'
}
[ordered]@{passed=$true;scenarios=$results;scope='Actual descendant DLL loading using Qt fixture, not a real solver';configurationPreserved=$true;worktreeOnly=$true}|
    ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $testRoot 'result.json') -Encoding UTF8
Write-Output "PASS: $testRoot\result.json"
