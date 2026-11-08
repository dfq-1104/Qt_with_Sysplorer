$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$testRoot=Join-Path $root ('logs\file-access-r2\server-gate-'+[guid]::NewGuid().ToString('N'))
$package=Join-Path $testRoot 'package';[IO.Directory]::CreateDirectory($package)|Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Check-Server.ps1') -Destination $package
$probe=@'
param($Address,$Port,$CredentialConfigPath,$OutputFile)
[IO.File]::WriteAllText($OutputFile,[IO.File]::ReadAllText($env:BATTERY_TEST_GATE_REPORT))
'@
[IO.File]::WriteAllText((Join-Path $package 'Test-RemoteModel.ps1'),$probe)
$savedLocal=$env:LOCALAPPDATA;$savedReport=$env:BATTERY_TEST_GATE_REPORT
try{
    $env:LOCALAPPDATA=Join-Path $testRoot 'user';$remote=Join-Path $env:LOCALAPPDATA 'BatteryConsole\remote';[IO.Directory]::CreateDirectory($remote)|Out-Null
    $mockHome=Join-Path $testRoot 'host';$addins=Join-Path $mockHome 'Bin64\addins\BatteryUdpControl';[IO.Directory]::CreateDirectory($addins)|Out-Null
    [IO.File]::WriteAllText((Join-Path $addins 'battery_udp_control.dll'),'test-plugin')
    $env:BATTERY_TEST_GATE_REPORT=Join-Path $testRoot 'probe.json'
    $testCredential='fixture-key-not-a-real-credential-0123456789'
    function Get-Process {
        param($Name,$Id,$ErrorAction)
        if(-not $fixtureProcessExists){return}
        $mods=@();if($fixturePluginLoaded){$mods+=@([pscustomobject]@{ModuleName='battery_udp_control.dll';FileName=(Join-Path $addins 'battery_udp_control.dll')})}
        [pscustomobject]@{Id=42;Path=(Join-Path $mockHome 'Bin64\mworks.exe');Modules=$mods}
    }
    function Get-NetTCPConnection {param($State,$LocalPort,$ErrorAction) [pscustomobject]@{LocalAddress='0.0.0.0';LocalPort=39001;OwningProcess=42}}
    $cases=[ordered]@{none='READY';authentication='ENGINE_NOT_READY';upload='ENGINE_NOT_READY';engine='ENGINE_NOT_READY';unavailable='ENGINE_NOT_READY';busy='ENGINE_NOT_READY';background='HOST_BACKGROUND_WAIT';process='HOST_NOT_RUNNING';notLoaded='PLUGIN_NOT_LOADED';noStatus='PLUGIN_STATUS_MISSING';hostDisabled='CONFIG_VIEW_MISMATCH';configDisabled='REMOTE_DISABLED';wrongPort='PORT_NOT_LISTENING';portFailed='PORT_NOT_LISTENING'}
    foreach($defect in $cases.Keys){
        $fixtureProcessExists=$defect -ne 'process';$fixturePluginLoaded=$defect -ne 'notLoaded'
        $enabled=if($defect -eq 'configDisabled'){'false'}else{'true'}
        [IO.File]::WriteAllText((Join-Path $addins 'remote.ini'),"[Remote]`r`nEnabled=$enabled`r`nPort=39001`r`nAccessKey=$testCredential`r`n")
        $record=Join-Path $remote '42.json'
        $port=if($defect -eq 'wrongPort'){39002}else{39001}
        $status=@{pid=42;port=$port;enabled=($defect -notin @('hostDisabled','configDisabled'));listening=($defect -notin @('portFailed','hostDisabled','configDisabled'));address='0.0.0.0';error=''}
        $status|ConvertTo-Json|Set-Content -LiteralPath $record
        if($defect -eq 'noStatus'){Remove-Item -LiteralPath $record}
        $data=@{authenticated=($defect -ne 'authentication');capabilitiesMatch=($defect -ne 'upload');ready=($defect -notin @('engine','background'));controllerState='Idle';backgroundTaskRunning=($defect -eq 'background')}
        if($defect -eq 'unavailable'){$data.controllerState='Unavailable'};if($defect -eq 'busy'){$data.controllerState='Running'}
        $data|ConvertTo-Json|Set-Content -LiteralPath $env:BATTERY_TEST_GATE_REPORT
        $accepted=$true;$outputPath=Join-Path $testRoot ($defect+'.json')
        try{$output=& (Join-Path $package 'Check-Server.ps1') -SysplorerHome $mockHome -OutputFile $outputPath}catch{$accepted=$false}
        $bytes=[IO.File]::ReadAllText($outputPath);$result=$bytes|ConvertFrom-Json
        if($accepted -ne ($defect -eq 'none') -or $result.code -ne $cases[$defect]){throw "Server readiness gate failed: $defect, code=$($result.code)"}
        if($bytes.Contains($testCredential)){throw 'Credential leaked to diagnostic'}
    }
    [ordered]@{passed=$true;type='mocked-process-and-report-gates';cases=$cases;systemChanges=$false;credentialsRedacted=$true}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $testRoot 'result.json')
}finally{
    Remove-Item Function:\Get-Process -ErrorAction SilentlyContinue;Remove-Item Function:\Get-NetTCPConnection -ErrorAction SilentlyContinue
    $env:LOCALAPPDATA=$savedLocal;$env:BATTERY_TEST_GATE_REPORT=$savedReport
}
Write-Output "Server readiness diagnosis PASS (14 mocked scenarios): $testRoot"