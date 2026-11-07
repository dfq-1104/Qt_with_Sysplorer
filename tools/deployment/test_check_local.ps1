$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$testRoot=Join-Path $root ('logs\local-check\diagnostic-'+[guid]::NewGuid().ToString('N'))
$fixtureRoot=Join-Path $testRoot 'host'
$bin=Join-Path $fixtureRoot 'Bin64'
[IO.Directory]::CreateDirectory((Join-Path $bin 'addins\BatteryUdpControl'))|Out-Null
$exe=Join-Path $bin 'mworks.exe'
Copy-Item -LiteralPath (Join-Path $root 'app\ui\tests\build_startup_fixture\release\mworks.exe') -Destination $exe
$plugin=Join-Path $bin 'addins\BatteryUdpControl\battery_udp_control.dll'
[IO.File]::WriteAllBytes($plugin,[Text.Encoding]::ASCII.GetBytes('test-plugin-only'))
$before=(Get-FileHash -LiteralPath $plugin).Hash
$expected=(Get-FileHash -LiteralPath (Join-Path $root 'model\BatterySim.mo')).Hash.ToLowerInvariant()
$environment=@{}
foreach($name in @('BATTERY_LAUNCH_SENTINEL','BATTERY_FIXTURE_UDP','BATTERY_DISCOVERY_DIR','BATTERY_FIXTURE_MODEL_HASH','BATTERY_FIXTURE_MISSING_MODEL','BATTERY_FIXTURE_MISSING_SPLIT','BATTERY_FIXTURE_BUSY','BATTERY_FIXTURE_READY_AFTER_MS')){$environment[$name]=[Environment]::GetEnvironmentVariable($name,'Process');Remove-Item "Env:$name" -ErrorAction SilentlyContinue}
$environment['PATH']=$env:PATH
$owned=$null
$check=Join-Path $PSScriptRoot 'Check-Local.ps1'
function Assert-That($condition,[string]$message){if(-not $condition){throw $message}}
try{
    $env:PATH='C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\bin;'+$env:PATH
    $env:BATTERY_FIXTURE_UDP='1';$env:BATTERY_FIXTURE_MODEL_HASH='old-model';$env:BATTERY_FIXTURE_MISSING_MODEL='1'
    foreach($scenario in @('incompatible','compatible')){
        if($scenario -eq 'compatible'){$env:BATTERY_FIXTURE_MODEL_HASH=$expected;Remove-Item Env:BATTERY_FIXTURE_MISSING_MODEL}
        $discovery=Join-Path $testRoot $scenario
        $marker=Join-Path $testRoot ($scenario+'.jsonl')
        $env:BATTERY_DISCOVERY_DIR=$discovery.Substring($root.Length+1)
        $env:BATTERY_LAUNCH_SENTINEL=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($marker))
        $owned=Start-Process -FilePath $exe -WorkingDirectory $root -WindowStyle Hidden -PassThru
        $endpoint=Join-Path $discovery ($owned.Id.ToString()+'.json')
        $clock=[Diagnostics.Stopwatch]::StartNew()
        while(-not(Test-Path -LiteralPath $endpoint) -and $clock.ElapsedMilliseconds -lt 5000){Start-Sleep -Milliseconds 50}
        Assert-That (Test-Path -LiteralPath $endpoint) 'Fixture did not advertise'
        $report=Join-Path $testRoot ($scenario+'.json')
        & $check -SysplorerHome $fixtureRoot -ExpectedModelHash $expected -DiscoveryDirectory $discovery -OutputFile $report|Out-Null
        $json=Get-Content -LiteralPath $report -Raw|ConvertFrom-Json
        Assert-That ($json.processes.Count -eq 1) 'Diagnostic failed to identify the target PID'
        Assert-That ($json.processes[0].state -eq $scenario.ToUpperInvariant()) 'Incorrect diagnostic classification'
        $secret=(Get-Content -LiteralPath $endpoint -Raw|ConvertFrom-Json).token
        Assert-That (-not [IO.File]::ReadAllText($report).Contains($secret)) 'Diagnostic exposed the discovery token'
        $owned.Kill();$owned.WaitForExit();$owned=$null
        Assert-That ([regex]::Matches([IO.File]::ReadAllText($marker),'"command":"hello"').Count -eq 1) 'Diagnostic should send exactly one hello'
        Assert-That ((Get-FileHash -LiteralPath $plugin).Hash -eq $before) 'Diagnostic modified the plugin'
    }
    $report=Join-Path $testRoot 'not-running.json'
    & $check -SysplorerHome $fixtureRoot -ExpectedModelHash $expected -OutputFile $report|Out-Null
    Assert-That ((Get-Content -LiteralPath $report -Raw|ConvertFrom-Json).issues -contains 'TARGET_NOT_RUNNING') 'Stopped host classification failed'
    & $check -SysplorerHome (Join-Path $testRoot 'missing') -ExpectedModelHash $expected -OutputFile $report|Out-Null
    $json=Get-Content -LiteralPath $report -Raw|ConvertFrom-Json
    Assert-That ($json.issues -contains 'HOST_NOT_FOUND' -and $json.issues -contains 'PLUGIN_NOT_FOUND') 'Missing files classification failed'
}finally{
    if($owned -and -not $owned.HasExited){
        Assert-That ($owned.Path -eq $exe) 'Refused to stop an unrelated process';$owned.Kill();$owned.WaitForExit()
    }
    foreach($name in $environment.Keys){if($null -eq $environment[$name]){Remove-Item "Env:$name" -ErrorAction SilentlyContinue}else{[Environment]::SetEnvironmentVariable($name,$environment[$name],'Process')}}
}
[ordered]@{passed=$true;checks=@('incompatible','compatible','not-running','missing-files','single-hello','no-discovery-secrets','plugin-unchanged');worktreeOnly=$true}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $testRoot 'result.json') -Encoding UTF8
Write-Output "PASS: $testRoot\result.json"
