$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$context=& (Join-Path $PSScriptRoot 'Start-LocalWorktree.ps1') -EnvironmentOnly
$saved=@{}
foreach($name in @($context.environment.Keys)+@('BATTERY_CONNECTION_MODE','BATTERY_TEST_ARTIFACTS','QT_QPA_PLATFORM','QT_QPA_PLATFORM_PLUGIN_PATH','BATTERY_HOST_TIMEOUT_MS')){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
$workspace=$context.workspace+'/validation-'+[guid]::NewGuid().ToString('N').Substring(0,8)
$artifacts=Join-Path $root 'logs\dual-mode\real-local'
[IO.Directory]::CreateDirectory($artifacts)|Out-Null
$ownedTest=$null
try{
    foreach($name in $context.environment.Keys){[Environment]::SetEnvironmentVariable($name,$context.environment[$name],'Process')}
    $env:PATH='C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\bin;'+$env:PATH
    $env:BATTERY_CONNECTION_MODE='local';$env:BATTERY_SIM_WORKSPACE=$workspace
    $env:BATTERY_TEST_ARTIFACTS=$workspace+'/test-artifacts';[IO.Directory]::CreateDirectory($env:BATTERY_TEST_ARTIFACTS)|Out-Null
    $env:QT_QPA_PLATFORM='windows';$env:QT_QPA_PLATFORM_PLUGIN_PATH=Join-Path $context.client 'platforms'
    $env:BATTERY_HOST_TIMEOUT_MS='180000'
    $suites=@(
      @{name='detailed';exe='app\ui\tests\build_local_detailed\release\local_detailed_test.exe';cases=@()},
      @{name='ui';exe='app\ui\tests\build\release\ui_integration_test.exe';cases=@('layoutAndDisclosure','startupFailureKeepsUiUsable','modelLoadAndReuse','startIncludesPreparation','automaticStartFailureKeepsStage','realButtonsAndTaskLifecycle','runtimeTuningButtons','busyHostIsSkipped')}
    )
    $results=@()
    foreach($suite in $suites){
        $report=Join-Path $artifacts ($suite.name+'.txt')
        $options=@{FilePath=(Join-Path $root $suite.exe);WorkingDirectory=$root;WindowStyle='Hidden';PassThru=$true;ArgumentList=@('-o',('"'+$report+',txt"'),'-v1')+$suite.cases}
        $ownedTest=Start-Process @options
        if(-not $ownedTest.WaitForExit(1200000)){throw 'Owned local test timed out'}
        $results+=@{suite=$suite.name;exitCode=$ownedTest.ExitCode;report=$report}
        if($ownedTest.ExitCode -ne 0){throw "Local suite failed: $($suite.name); inspect $report"}
    }
    & (Join-Path $PSScriptRoot 'Check-Local.ps1') -SysplorerHome $context.host -DiscoveryDirectory $context.discovery -ExpectedModelHash (Get-Content (Join-Path $context.client 'manifest.json') -Raw -Encoding UTF8|ConvertFrom-Json).modelHash -OutputFile (Join-Path $artifacts 'diagnostic.json')|Out-Host
    [ordered]@{passed=$true;client=$context.client;privateHost=$context.host;workspace=$workspace;suites=$results;artifacts=$env:BATTERY_TEST_ARTIFACTS}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $artifacts 'result.json') -Encoding UTF8
}finally{
    if($ownedTest -and -not $ownedTest.HasExited){$ownedTest.Kill();$ownedTest.WaitForExit()}
    # Only PIDs recorded as launched by this test's unique workspace qualify.
    foreach($log in @(Get-ChildItem -LiteralPath $workspace -Filter client.log -Recurse -File -ErrorAction SilentlyContinue)){
        foreach($line in Get-Content -LiteralPath $log.FullName){
            try{$row=$line|ConvertFrom-Json}catch{continue}
            if($row.event -ne 'launched'){continue}
            $owned=Get-Process -Id ([int]$row.pid) -ErrorAction SilentlyContinue
            if(-not $owned){continue}
            $path=$owned.Path
            $expected=Join-Path $context.host 'Bin64\mworks.exe'
            $state=Get-Content (Join-Path $root '.local-sysplorer\state.json') -Raw -Encoding UTF8|ConvertFrom-Json
            $physical=Join-Path $root ($state.hostRelative+'\Bin64\mworks.exe')
            if([IO.Path]::GetFullPath($path) -notin @([IO.Path]::GetFullPath($expected),[IO.Path]::GetFullPath($physical))){throw 'Refused to stop an unrelated host'}
            $owned.Kill();$owned.WaitForExit()
        }
    }
    foreach($name in $saved.Keys){[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}
}
Write-Output "PASS: $artifacts\result.json"
