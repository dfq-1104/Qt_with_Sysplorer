param(
    [string]$SysplorerHome='',
    [string]$ExpectedModelHash='',
    [string]$DiscoveryDirectory='',
    [string]$OutputFile=''
)
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -ErrorAction Stop
if(-not $SysplorerHome){
    $SysplorerHome=$env:SYSPLORER_HOME
    $localState=Join-Path $env:LOCALAPPDATA 'BatteryConsole\local-runtime.json'
    if(-not $SysplorerHome -and (Test-Path -LiteralPath $localState)){$SysplorerHome=(Get-Content -LiteralPath $localState -Raw|ConvertFrom-Json).sysplorerHome}
    if(-not $SysplorerHome){
        $SysplorerHome='C:\Program Files\MWORKS\Sysplorer 2026a'
        $ini=Join-Path $PSScriptRoot 'sysplorer.ini'
        if(Test-Path -LiteralPath $ini){
            $section=''
            foreach($line in [IO.File]::ReadAllLines($ini)){
                if($line -match '^\s*\[([^]]+)\]'){$section=$Matches[1]}
                elseif($section -eq 'Host' -and $line -match '^\s*Home\s*=\s*(.+?)\s*$'){$SysplorerHome=$Matches[1]}
            }
        }
    }
}
if(-not $ExpectedModelHash){
    $runtimeModel=Join-Path $PSScriptRoot 'models\BatterySim.mo'
    if(Test-Path -LiteralPath $runtimeModel){$ExpectedModelHash=(Get-FileHash -LiteralPath $runtimeModel).Hash.ToLowerInvariant()}
    $manifest=Join-Path $PSScriptRoot 'manifest.json'
    if(-not $ExpectedModelHash -and (Test-Path -LiteralPath $manifest)){$ExpectedModelHash=(Get-Content -LiteralPath $manifest -Raw|ConvertFrom-Json).modelHash}
    if(-not $ExpectedModelHash){
        $modelSource=Join-Path $PSScriptRoot '..\..\model\BatterySim.mo'
        if(Test-Path -LiteralPath $modelSource){$ExpectedModelHash=(Get-FileHash -LiteralPath $modelSource).Hash.ToLowerInvariant()}
    }
}
if(-not $ExpectedModelHash -or $ExpectedModelHash -notmatch '^[0-9a-fA-F]{64}$'){throw 'Expected model SHA-256 missing. Supply -ExpectedModelHash or use a complete client package.'}
if(-not $DiscoveryDirectory){$DiscoveryDirectory=$env:BATTERY_DISCOVERY_DIR}
if(-not $DiscoveryDirectory){$DiscoveryDirectory=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'BatteryConsole\udp'}
if(-not $OutputFile){$OutputFile=Join-Path $PSScriptRoot 'local-diagnostic.json'}
$hostRoot=[IO.Path]::GetFullPath($SysplorerHome)
$exe=Join-Path $hostRoot 'Bin64\mworks.exe'
$plugin=Join-Path $hostRoot 'Bin64\addins\BatteryUdpControl\battery_udp_control.dll'
function File-Hash([string]$Path){if(Test-Path -LiteralPath $Path -PathType Leaf){return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash};return ''}
$issues=@()
if(-not(Test-Path -LiteralPath $exe -PathType Leaf)){$issues+='HOST_NOT_FOUND'}
if(-not(Test-Path -LiteralPath $plugin -PathType Leaf)){$issues+='PLUGIN_NOT_FOUND'}
$records=@()
foreach($localProcess in @(Get-Process mworks -ErrorAction SilentlyContinue)){
    if(-not $localProcess.Path -or [IO.Path]::GetFullPath($localProcess.Path) -ne $exe){continue}
    $entry=[ordered]@{pid=$localProcess.Id;hostExecutable=$localProcess.Path;loadedPlugins=@();state='NO_FRESH_DISCOVERY';hello=$null}
    try{
        $entry.loadedPlugins=@(foreach($module in @($localProcess.Modules|Where-Object {$_.ModuleName -eq 'battery_udp_control.dll'})){
            [ordered]@{path=$module.FileName;diskSha256=(File-Hash $module.FileName)}
        })
    }catch{$issues+='PROCESS_MODULES_UNREADABLE'}
    $endpoint=Join-Path $DiscoveryDirectory ($localProcess.Id.ToString()+'.json')
    if(Test-Path -LiteralPath $endpoint){
        try{
            $discovery=Get-Content -LiteralPath $endpoint -Raw|ConvertFrom-Json
            $updated=if($discovery.updated -is [DateTime]){[DateTimeOffset]$discovery.updated}else{[DateTimeOffset]::Parse($discovery.updated,[Globalization.CultureInfo]::InvariantCulture,[Globalization.DateTimeStyles]::RoundtripKind)}
            if($discovery.schema -ne 1 -or $discovery.protocol -ne 'battery-udp/1' -or $discovery.pid -ne $localProcess.Id -or
               ([DateTimeOffset]::UtcNow-$updated).TotalSeconds -gt 20 -or -not $discovery.bound -or
               $discovery.port -lt 1 -or $discovery.port -gt 65535){throw 'Invalid or stale local discovery'}
            $requestId=[guid]::NewGuid().ToString()
            $request=@{schema=1;id=$requestId;session=$discovery.session;token=$discovery.token;client=[guid]::NewGuid().ToString();taskId='';command='hello';args=@{}}
            $udp=New-Object Net.Sockets.UdpClient
            try{
                $udp.Connect([Net.IPAddress]::Loopback,[int]$discovery.port)
                $udp.Client.ReceiveTimeout=1500
                $bytes=[Text.Encoding]::UTF8.GetBytes(($request|ConvertTo-Json -Depth 4 -Compress))
                [void]$udp.Send($bytes,$bytes.Length)
                $peer=New-Object Net.IPEndPoint ([Net.IPAddress]::Loopback),0
                $reply=[Text.Encoding]::UTF8.GetString($udp.Receive([ref]$peer))|ConvertFrom-Json
                if($reply.schema -ne 1 -or $reply.id -ne $requestId -or $reply.session -ne $discovery.session -or $reply.taskId -ne ''){throw 'Unexpected hello response'}
                $missing=@(foreach($name in @('modelLifecycle','splitLifecycle')){if($reply.$name -ne $true){$name}})
                if($reply.modelTransferVersion -ne 1){$missing+='modelTransferVersion=1'}
                if($reply.modelInterface -ne 'battery-console/1'){$missing+='modelInterface=battery-console/1'}
                $modelMatches=$null
                $entry.hello=[ordered]@{ok=($reply.ok -eq $true);ready=($reply.ready -eq $true);modelLifecycle=($reply.modelLifecycle -eq $true);splitLifecycle=($reply.splitLifecycle -eq $true);missingCapabilities=$missing;actualModelHash=$reply.modelHash;modelMatches=$modelMatches;controllerState=$reply.controllerState;phase=$reply.status.phase}
                $entry.state=if(-not $reply.ok){'HELLO_REJECTED'}elseif(-not $reply.ready){'NOT_READY'}elseif($missing.Count){'INCOMPATIBLE'}elseif($reply.controllerState -ne 'Idle'){'BUSY'}else{'COMPATIBLE'}
            }finally{$udp.Dispose()}
        }catch{$entry.state='DISCOVERY_OR_HELLO_FAILED';$entry['error']=$_.Exception.Message}
    }
    $records+=$entry
}
if(-not $records.Count){$issues+='TARGET_NOT_RUNNING'}
# Never write discovery tokens, session credentials, remote.ini or access keys.
$report=[ordered]@{schema=1;mode='local';checkedAt=(Get-Date).ToString('o');hostExecutable=$exe;pluginOnDisk=[ordered]@{path=$plugin;sha256=(File-Hash $plugin)};expectedModelHash=$ExpectedModelHash;modelTextSource='client models/BatterySim.mo; verified after chunk upload';processes=$records;issues=$issues;readOnly=$true}
$outputPath=[IO.Path]::GetFullPath($OutputFile)
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($outputPath))|Out-Null
$report|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $outputPath -Encoding UTF8
Write-Output "Local diagnostic: $outputPath"
foreach($entry in $records){Write-Output "PID $($entry.pid): $($entry.state)"}
foreach($issue in $issues){Write-Output $issue}
