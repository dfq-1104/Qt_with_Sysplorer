$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$context=& (Join-Path $PSScriptRoot 'Start-LocalWorktree.ps1') -EnvironmentOnly
$output=Join-Path $root 'logs\dual-mode\entries'
[IO.Directory]::CreateDirectory($output)|Out-Null
$settings=Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'BatteryConsole\RemoteConnection.ini'
$before=if(Test-Path $settings){(Get-FileHash -LiteralPath $settings).Hash}else{''}
$savedRemote=$false
if(Test-Path $settings){$text=Get-Content -LiteralPath $settings -Raw;$savedRemote=$text -match '(?m)^Remote=true\s*$'}
$oldArtifacts=$env:BATTERY_TEST_ARTIFACTS
$results=@()
try{
    foreach($case in @('local','generic','start-ui','remote')){
        $folder=Join-Path $output $case;[IO.Directory]::CreateDirectory($folder)|Out-Null
        $env:BATTERY_TEST_ARTIFACTS=$folder
        $parentPath=$env:PATH;$parentHost=$env:SYSPLORER_HOME
        if($case -eq 'local'){$owned=& (Join-Path $context.client 'Start-Local.ps1') -SmokeTest -PassThru}
        elseif($case -eq 'generic'){$owned=& (Join-Path $context.client 'Start-Client.ps1') -SmokeTest -PassThru}
        elseif($case -eq 'start-ui'){$owned=& (Join-Path $root 'app\ui\start_ui.ps1') -SmokeTest -PassThru}
        else{$owned=& (Join-Path $context.client 'Start-Remote.ps1') -Address '172.19.15.159' -Port 39001 -KeyFile (Join-Path $root 'logs\remote-vm\access-key.txt') -SmokeTest -PassThru}
        if($owned -isnot [Diagnostics.Process]){throw 'Expected one owned client process'}
        if(-not $owned.WaitForExit(20000) -or $owned.ExitCode -ne 0){throw "Startup failed: $case"}
        if($parentPath -cne $env:PATH -or $parentHost -cne $env:SYSPLORER_HOME){throw 'Caller environment changed'}
        $state=Get-Content -LiteralPath (Join-Path $folder 'ui_startup.json') -Raw -Encoding UTF8|ConvertFrom-Json
        $expectedRemote=if($case -eq 'local'){$false}elseif($case -eq 'remote'){$true}else{$savedRemote}
        if($state.remoteMode -ne $expectedRemote -or $state.phase -ne 'Idle' -or $state.connecting -or $state.running -or $state.taskId){throw "Unexpected startup state: $case"}
        $results+=@{entry=$case;client=$context.client;remote=$state.remoteMode;idle=$true;callerEnvironmentRestored=$true}
    }
}finally{$env:BATTERY_TEST_ARTIFACTS=$oldArtifacts}
$after=if(Test-Path $settings){(Get-FileHash -LiteralPath $settings).Hash}else{''}
if($before -ne $after){throw 'Global connection settings changed'}
[ordered]@{passed=$true;powerShell=$PSVersionTable.PSVersion.ToString();clientSha256=(Get-FileHash (Join-Path $context.client 'battery_console_ui.exe')).Hash;userSettingsUnchanged=$true;entries=$results}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $output 'result.json') -Encoding UTF8
Write-Output "PASS: $output\result.json"
