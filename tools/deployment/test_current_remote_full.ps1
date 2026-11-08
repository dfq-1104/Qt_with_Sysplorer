param(
    [string]$Address='172.19.15.159',
    [ValidateRange(1,65535)][int]$Port=39001,
    [Parameter(Mandatory=$true)][string]$KeyFile,
    [switch]$RealOnly
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$context=& (Join-Path $PSScriptRoot 'Start-LocalWorktree.ps1') -EnvironmentOnly
$output=Join-Path $root ('logs\remote-full\'+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($output)|Out-Null
$keyPath=[IO.Path]::GetFullPath($KeyFile)
if(-not(Test-Path -LiteralPath $keyPath -PathType Leaf)){throw 'Existing server access-key.txt is required'}
& (Join-Path $PSScriptRoot 'Test-RemoteModel.ps1') -Address $Address -Port $Port -KeyFile $keyPath -OutputFile (Join-Path $output 'before.json')|Out-Host
$before=Get-Content -LiteralPath (Join-Path $output 'before.json') -Raw -Encoding UTF8|ConvertFrom-Json
if(-not $before.ready -or $before.controllerState -ne 'Idle'){throw 'Remote host must be ready and idle before real task tests'}
function Get-UserSettingsFingerprint {
    $folder=Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'BatteryConsole'
    $files=@(Get-ChildItem -LiteralPath $folder -Filter '*.ini' -File -ErrorAction SilentlyContinue|Sort-Object Name|ForEach-Object {
        [ordered]@{name=$_.Name;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}
    })
    return ConvertTo-Json -InputObject $files -Compress
}
$settingsBefore=Get-UserSettingsFingerprint
$names=@($context.environment.Keys)+@('PATH','BATTERY_TEST_ROOT','BATTERY_TEST_ARTIFACTS','QT_QPA_PLATFORM','QT_QPA_PLATFORM_PLUGIN_PATH','BATTERY_REAL_REMOTE_ADDRESS','BATTERY_REAL_REMOTE_PORT','BATTERY_REAL_REMOTE_KEY_FILE','BATTERY_REAL_REMOTE_KEY','BATTERY_REAL_DUAL_MODE','BATTERY_CONNECTION_MODE','BATTERY_REMOTE_ADDRESS','BATTERY_REMOTE_PORT','BATTERY_REMOTE_KEY')
$saved=@{};foreach($name in $names){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
$owned=$null
try {
    foreach($name in $context.environment.Keys){[Environment]::SetEnvironmentVariable($name,$context.environment[$name],'Process')}
    $env:PATH='C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\bin;'+$env:PATH
    $env:QT_QPA_PLATFORM='windows';$env:QT_QPA_PLATFORM_PLUGIN_PATH=Join-Path $context.client 'platforms'
    $env:BATTERY_TEST_ROOT=$output;$env:BATTERY_TEST_ARTIFACTS=$output
    $env:BATTERY_REAL_REMOTE_ADDRESS=$Address;$env:BATTERY_REAL_REMOTE_PORT=$Port.ToString()
    $env:BATTERY_REAL_REMOTE_KEY_FILE=$keyPath;$env:BATTERY_REAL_DUAL_MODE='1'
    Remove-Item Env:BATTERY_REAL_REMOTE_KEY,Env:BATTERY_REMOTE_KEY -ErrorAction SilentlyContinue
    $exe=Join-Path $root 'app\ui\tests\build_remote\release\remote_integration_test.exe'
    $report=Join-Path $output 'tests.txt'
    $arguments=@('-o',('"'+$report+',txt"'),'-v1')
    if($RealOnly){$arguments+=@('realHostRemoteGuiWorkflow','realDualModeConnectionSwitch','realHostRemoteLifecycle')}
    $owned=Start-Process -FilePath $exe -WorkingDirectory $root -WindowStyle Hidden -PassThru -ArgumentList $arguments
    if(-not $owned.WaitForExit(1200000)){throw 'Owned real remote test process timed out; inspect tests.txt'}
    if($owned.ExitCode -ne 0){throw ('Real remote tests failed; inspect '+$report)}
    if((Get-UserSettingsFingerprint) -cne $settingsBefore){throw 'User settings changed during isolated real remote tests'}
    & (Join-Path $PSScriptRoot 'Test-RemoteModel.ps1') -Address $Address -Port $Port -KeyFile $keyPath -OutputFile (Join-Path $output 'after.json')|Out-Host
    [ordered]@{passed=$true;realCasesOnly=[bool]$RealOnly;address=$Address;port=$Port;sourceCommit=(& git -C $root rev-parse HEAD);modelHash=$before.actualModelHash;clientSha256=(Get-FileHash -LiteralPath (Join-Path $context.client 'battery_console_ui.exe')).Hash;report=$report;userSettingsUnchanged=$true;realRemoteGui=$true;realRemoteLifecycle=$true;realModeSwitch=$true}|
        ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $output 'result.json') -Encoding UTF8
} catch {
    [ordered]@{passed=$false;address=$Address;port=$Port;report=$report;error=$_.Exception.Message;userServerNotStopped=$true}|
        ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $output 'failure.json') -Encoding UTF8
    throw
} finally {
    if($owned -and -not $owned.HasExited){$owned.Kill();$owned.WaitForExit()}
    # Only host PIDs recorded as launched inside this unique test subtree qualify.
    foreach($log in @(Get-ChildItem -LiteralPath $output -Filter client.log -Recurse -File -ErrorAction SilentlyContinue)){
        foreach($line in Get-Content -LiteralPath $log.FullName){
            try{$row=$line|ConvertFrom-Json}catch{continue}
            if($row.event -ne 'launched'){continue}
            $hostProcess=Get-Process -Id ([int]$row.pid) -ErrorAction SilentlyContinue
            if(-not $hostProcess){continue}
            $state=Get-Content -LiteralPath (Join-Path $root '.local-sysplorer\state.json') -Raw -Encoding UTF8|ConvertFrom-Json
            $allowed=@((Join-Path $context.host 'Bin64\mworks.exe'),(Join-Path $root ($state.hostRelative+'\Bin64\mworks.exe')))|ForEach-Object {[IO.Path]::GetFullPath($_)}
            if([IO.Path]::GetFullPath($hostProcess.Path) -notin $allowed){throw 'Refused to stop a host outside the prepared current-tree runtime'}
            $hostProcess.Kill();$hostProcess.WaitForExit()
        }
    }
    foreach($name in $saved.Keys){
        if($null -eq $saved[$name]){Remove-Item "Env:$name" -ErrorAction SilentlyContinue}
        else{[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}
    }
}
Get-Content -LiteralPath $report -Tail 5
Write-Output "PASS: $output\result.json"
