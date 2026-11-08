param(
    [Parameter(Mandatory=$true)][string]$Address,
    [int]$Port=39001,
    [Parameter(Mandatory=$true)][string]$KeyFile,
    [Parameter(Mandatory=$true)][string]$ClientDirectory
)
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -ErrorAction Stop
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$client=[IO.Path]::GetFullPath($ClientDirectory)
if(-not $client.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Client must remain in current worktree'}
$output=Join-Path $root ('logs\remote-vm\connection-check-'+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($output)|Out-Null
$beforeReport=Join-Path $output 'server-before.json'
& (Join-Path $PSScriptRoot 'Test-RemoteModel.ps1') -Address $Address -Port $Port -KeyFile $KeyFile -OutputFile $beforeReport | Out-Host
$before=Get-Content -LiteralPath $beforeReport -Raw -Encoding UTF8|ConvertFrom-Json
$invalid=Join-Path $output 'invalid-test-key.txt'
[IO.File]::WriteAllText($invalid,'public-invalid-test-credential-0123456789abcdef',[Text.Encoding]::ASCII)
$userSettings=Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'BatteryConsole\RemoteConnection.ini'
$settingsBefore=if(Test-Path -LiteralPath $userSettings){(Get-FileHash -LiteralPath $userSettings).Hash}else{''}
$names=@('BATTERY_CONNECTION_MODE','BATTERY_REMOTE_ADDRESS','BATTERY_REMOTE_PORT','BATTERY_REMOTE_KEY','BATTERY_TEST_ARTIFACTS','BATTERY_SIM_WORKSPACE','PATH','QT_QPA_PLATFORM','QT_QPA_PLATFORM_PLUGIN_PATH','QT_PLUGIN_PATH','PYTHONHOME','PYTHONPATH')
$saved=@{};foreach($name in $names){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
$cases=@()
try{
    foreach($scenario in @('wrong-key','valid-key')){
        $artifacts=Join-Path $output $scenario;[IO.Directory]::CreateDirectory($artifacts)|Out-Null
        $env:BATTERY_CONNECTION_MODE='local';$env:BATTERY_REMOTE_ADDRESS='172.19.0.1';$env:BATTERY_REMOTE_PORT='39002'
        $env:BATTERY_REMOTE_KEY='public-stale-parent-credential-0123456789abcdef'
        $env:BATTERY_TEST_ARTIFACTS=$artifacts;$env:BATTERY_SIM_WORKSPACE=Join-Path $output 'client-logs'
        $expected=@{};foreach($name in $names){$expected[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
        $selectedKey=if($scenario -eq 'wrong-key'){$invalid}else{$KeyFile}
        $owned=$null
        try{
            $owned=& (Join-Path $client 'Start-Remote.ps1') -Address $Address -Port $Port -KeyFile $selectedKey -ConnectionTest -PassThru
            if($owned -isnot [Diagnostics.Process]){throw 'Launcher did not return exactly one process'}
            foreach($name in $names){if([Environment]::GetEnvironmentVariable($name,'Process') -cne $expected[$name]){throw "Caller environment changed: $name"}}
            if(-not $owned.WaitForExit(20000)){throw 'Owned connection-only client timed out'}
            $reportPath=Join-Path $artifacts 'remote_connection.json'
            $report=Get-Content -LiteralPath $reportPath -Raw -Encoding UTF8|ConvertFrom-Json
            $shouldPass=$scenario -eq 'valid-key'
            if($report.ok -ne $shouldPass -or $owned.ExitCode -ne $(if($shouldPass){0}else{1})){throw 'Unexpected authentication result'}
            if(-not $report.remoteMode -or $report.address -ne $Address -or $report.port -ne $Port -or $report.taskId -or $report.running){throw 'Unexpected endpoint, fallback or task submission'}
            if($shouldPass){if($report.phase -ne 'Connected' -or $report.modelHash -ne $before.actualModelHash){throw 'Connection/model confirmation failed'}}
            else{if($report.phase -ne 'ConnectionFailed' -or $report.message -notmatch 'access-key.txt'){throw 'Wrong-key failure did not explain credential recovery'}}
            $cases+=@{scenario=$scenario;expectedSuccess=$shouldPass;exitCode=$owned.ExitCode;phase=$report.phase;remoteOnly=$true;taskEmpty=$true;callerEnvironmentRestored=$true}
        }finally{
            if($owned -and -not $owned.HasExited){
                if([IO.Path]::GetFullPath($owned.Path) -ne [IO.Path]::GetFullPath((Join-Path $client 'battery_console_ui.exe'))){throw 'Refused to stop an unowned process'}
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
$settingsAfter=if(Test-Path -LiteralPath $userSettings){(Get-FileHash -LiteralPath $userSettings).Hash}else{''}
if($settingsBefore -ne $settingsAfter){throw 'User connection settings changed during connection-only verification'}
$afterReport=Join-Path $output 'server-after.json'
& (Join-Path $PSScriptRoot 'Test-RemoteModel.ps1') -Address $Address -Port $Port -KeyFile $KeyFile -OutputFile $afterReport | Out-Host
$after=Get-Content -LiteralPath $afterReport -Raw -Encoding UTF8|ConvertFrom-Json
if($before.lastTask.taskId -ne $after.lastTask.taskId){throw 'Server task changed during connection-only verification'}
[ordered]@{passed=$true;powerShell=$PSVersionTable.PSVersion.ToString();address=$Address;port=$Port;clientSha256=(Get-FileHash -LiteralPath (Join-Path $client 'battery_console_ui.exe')).Hash;modelHash=$after.actualModelHash;userSettingsUnchanged=$true;serverTaskUnchanged=$true;cases=$cases}|
    ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $output 'result.json') -Encoding UTF8
Write-Output "PASS: $output\result.json"
