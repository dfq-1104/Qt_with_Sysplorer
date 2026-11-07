param([int]$Port=39001)
$ErrorActionPreference='Stop'
$folder=Join-Path $env:LOCALAPPDATA 'BatteryConsole\remote'
$records=@(Get-ChildItem -LiteralPath $folder -Filter '*.json' -ErrorAction SilentlyContinue|Sort-Object LastWriteTime -Descending)
$found=$false
foreach($record in $records){
    $status=Get-Content -LiteralPath $record.FullName -Raw|ConvertFrom-Json
    if($status.port -ne $Port){continue}
    $hostProcess=Get-Process -Id $status.pid -ErrorAction SilentlyContinue
    if(-not $hostProcess -or $hostProcess.ProcessName -ne 'mworks'){continue}
    $found=$true
    [PSCustomObject]@{PID=$status.pid;Process='mworks';Enabled=$status.enabled;Listening=$status.listening;Address=$status.address;Port=$status.port;LastError=$status.error;StatusFile=$record.FullName}
}
if(-not $found){throw 'No live Sysplorer listener record found for this Windows user and port. Start Sysplorer and check remote.ini.'}
