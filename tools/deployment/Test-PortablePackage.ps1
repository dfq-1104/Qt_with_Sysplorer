param([Parameter(Mandatory=$true)][string]$PackageDirectory)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$output=Join-Path $root ('logs\portable\packages-'+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($output)|Out-Null
$zip=Join-Path ([IO.Path]::GetFullPath($PackageDirectory)) 'BatteryConsole-Client-win64.zip'
$saved=@{};$names=@('PATH','BATTERY_TEST_ARTIFACTS','BATTERY_TEST_CONFIG_ROOT','BATTERY_MODEL_FILE','BATTERY_CONNECTION_MODE','SYSPLORER_HOME','BATTERY_SIM_WORKSPACE','BATTERY_WORKTREE_ROOT','BATTERY_LOCAL_CONFIG')
foreach($name in $names){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
$results=@()
try{
    foreach($name in @('ascii-extract','中文 解压 空格')){
        $extract=Join-Path $output $name;Expand-Archive -LiteralPath $zip -DestinationPath $extract
        $client=Join-Path $extract 'BatteryConsole-Client-win64'
        $manifest=Get-Content -LiteralPath (Join-Path $client 'manifest.json') -Raw|ConvertFrom-Json
        $files=@(Get-ChildItem -LiteralPath $client -Recurse -File)
        if($files.Count -ne $manifest.files.Count+1){throw 'Unexpected package file count'}
        foreach($entry in $manifest.files){
            $file=Join-Path $client $entry.path
            if((Get-FileHash -LiteralPath $file).Hash -ne $entry.sha256 -or (Get-Item -LiteralPath $file).Length -ne $entry.bytes){throw "Manifest mismatch: $($entry.path)"}
        }
        foreach($file in $files){
            if($file.Name -match '^(local-worktree\.json|access-key\.txt|deployment\.json)$'){throw 'Development settings or credentials were packaged'}
            $bytes=[IO.File]::ReadAllBytes($file.FullName)
            foreach($encoding in @([Text.Encoding]::UTF8,[Text.Encoding]::Unicode)){
                if($encoding.GetString($bytes) -match '\.codex[\\/]worktrees[\\/](be10|5f2f|570e)[\\/]'){throw "Development root leaked into $($file.Name)"}
            }
        }
        $artifacts=Join-Path $extract 'evidence';[IO.Directory]::CreateDirectory($artifacts)|Out-Null
        $env:BATTERY_TEST_ARTIFACTS=$artifacts;$env:BATTERY_TEST_CONFIG_ROOT=Join-Path $artifacts 'settings';$env:BATTERY_CONNECTION_MODE='local'
        foreach($variable in @('BATTERY_MODEL_FILE','SYSPLORER_HOME','BATTERY_WORKTREE_ROOT','BATTERY_SIM_WORKSPACE','BATTERY_LOCAL_CONFIG')){Remove-Item "Env:$variable" -ErrorAction SilentlyContinue}
        # This PATH contains Windows utilities only. Qt/SDK/developer tools are
        # not available to the extracted client through the environment.
        $env:PATH="$env:SystemRoot\System32;$env:SystemRoot;$env:SystemRoot\System32\WindowsPowerShell\v1.0"
        $before=@(Get-Process mworks -ErrorAction SilentlyContinue|Select-Object -ExpandProperty Id)
        $command='/d /s /c ""'+(Join-Path $client 'Start-Client.cmd')+'" -SmokeTest"'
        $launcher=Start-Process -FilePath "$env:SystemRoot\System32\cmd.exe" -ArgumentList $command -WindowStyle Hidden -PassThru
        $deadline=(Get-Date).AddSeconds(20);$report=Join-Path $artifacts 'ui_startup.json'
        while(-not(Test-Path -LiteralPath $report) -and (Get-Date) -lt $deadline){Start-Sleep -Milliseconds 100}
        if(-not(Test-Path -LiteralPath $report)){throw 'Extracted client startup did not produce its report'}
        $startup=Get-Content -LiteralPath $report -Raw|ConvertFrom-Json
        if($startup.phase -ne 'Idle' -or $startup.connecting -or $startup.running -or $startup.taskId){throw 'Client auto-connected or started a task'}
        $newHosts=@(Get-Process mworks -ErrorAction SilentlyContinue|Where-Object {$PSItem.Id -notin $before})
        if($newHosts.Count){throw 'Client startup launched a host'}
        if($startup.modelHash -ne (Get-FileHash -LiteralPath (Join-Path $client 'models\BatterySim.mo')).Hash.ToLowerInvariant()){throw 'Client did not read its external packaged model'}
        $results+=@{directory=$client;fileCount=$files.Count;manifestVerified=$true;noWorktreeBinding=$true;noDeveloperPathRequired=$true;startup=$startup}
    }
    $serverExtract=Join-Path $output 'server-extract'
    Expand-Archive -LiteralPath (Join-Path $PackageDirectory 'BatteryConsole-Server-win64.zip') -DestinationPath $serverExtract
    $server=Join-Path $serverExtract 'BatteryConsole-Server-win64'
    $manifest=Get-Content -LiteralPath (Join-Path $server 'manifest.json') -Raw|ConvertFrom-Json
    $files=@(Get-ChildItem -LiteralPath $server -Recurse -File)
    if($files.Count -ne $manifest.files.Count+1 -or $manifest.modelTransferVersion -ne 1){throw 'Unexpected server package contents'}
    foreach($entry in $manifest.files){if((Get-FileHash -LiteralPath (Join-Path $server $entry.path)).Hash -ne $entry.sha256){throw 'Server manifest mismatch'}}
    foreach($name in @('remote.ini','access-key.txt','local-worktree.json')){if(Test-Path -LiteralPath (Join-Path $server $name)){throw 'Server credentials or machine settings were packaged'}}
    if(-not(Test-Path -LiteralPath (Join-Path $server 'Setup-Server.cmd'))){throw 'Server one-click entry is missing'}
    $results+=@{directory=$server;fileCount=$files.Count;manifestVerified=$true;noCredentials=$true;setupEntryPresent=$true}
}finally{
    foreach($name in $saved.Keys){if($null -eq $saved[$name]){Remove-Item "Env:$name" -ErrorAction SilentlyContinue}else{[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}}
}
[ordered]@{passed=$true;tests=$results}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $output 'result.json') -Encoding UTF8
Write-Output "Portable client ZIP verification PASS: $output"
