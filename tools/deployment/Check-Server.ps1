param([int]$Port=0,[string]$SysplorerHome='', [string]$OutputFile='',[string]$StatusDirectory='')
$ErrorActionPreference='Stop'
if(-not $OutputFile){$OutputFile=Join-Path $env:LOCALAPPDATA 'BatteryConsole\server\server-diagnostic.json'}
$report=[ordered]@{schema=2;checkedAt=(Get-Date).ToString('o');stage='environment';code='';message='';ready=$false;sysplorerHome=$SysplorerHome;port=$Port;configuration=@{};processes=@();statusRecords=@();listeners=@();preflight=$null}
$accessKey=''
function Safe-Message([string]$Message){if($accessKey){$Message=$Message.Replace($accessKey,'[redacted]')};return $Message.Substring(0,[Math]::Min(1200,$Message.Length))}
function Stop-Check([string]$Code,[string]$Message){$report.code=$Code;throw $Message}
try{
    if(-not $SysplorerHome){
        $receipt=Join-Path $env:LOCALAPPDATA 'BatteryConsole\server\last-server.json'
        if(Test-Path -LiteralPath $receipt){$saved=Get-Content -LiteralPath $receipt -Raw|ConvertFrom-Json;$SysplorerHome=$saved.sysplorerHome;if(-not $Port){$Port=$saved.port}}
    }
    if(-not $Port){$Port=39001};$report.port=$Port;$report.sysplorerHome=$SysplorerHome
    if(-not $SysplorerHome){Stop-Check 'SETUP_NOT_FOUND' '未找到服务器配置记录。请使用配置服务器时的 Windows 用户运行本检查，或重新打开 Setup-Server.cmd。'}
    $target=[IO.Path]::GetFullPath((Join-Path $SysplorerHome 'Bin64\mworks.exe'))
    $plugin=Join-Path $SysplorerHome 'Bin64\addins\BatteryUdpControl\battery_udp_control.dll'
    $config=Join-Path $SysplorerHome 'Bin64\addins\BatteryUdpControl\remote.ini'
    $report['hostExecutable']=$target;$report['pluginPath']=$plugin;$report['configPath']=$config
    $text=if(Test-Path -LiteralPath $config){[IO.File]::ReadAllText($config)}else{''}
    function Ini([string]$Name){return [regex]::Match($text,'(?m)^\s*'+[regex]::Escape($Name)+'\s*=\s*([^\r\n]*)').Groups[1].Value.Trim()}
    $accessKey=Ini 'AccessKey'
    $report.configuration=@{exists=(Test-Path -LiteralPath $config);enabled=((Ini 'Enabled') -eq 'true');listenAddress=(Ini 'ListenAddress');port=(Ini 'Port');credentialValid=($accessKey.Length -ge 32);workspace=(Ini 'Workspace')}
    $targets=@();$unreadable=$false
    foreach($process in @(Get-Process mworks -ErrorAction SilentlyContinue)){
        $path='';try{$path=$process.Path}catch{$unreadable=$true}
        $entry=[ordered]@{pid=$process.Id;path=$path;selected=$false;pluginLoaded=$false;moduleInspection='not-selected';fileAccessModules=@()}
        if($path -and [IO.Path]::GetFullPath($path) -eq $target){
            $entry.selected=$true;$targets+=$process
            try{
                $mods=@($process.Modules);$entry.pluginLoaded=@($mods|Where-Object {$PSItem.ModuleName -eq 'battery_udp_control.dll'}).Count -gt 0
                $entry.moduleInspection='complete';$entry['pluginModules']=@($mods|Where-Object {$PSItem.ModuleName -eq 'battery_udp_control.dll'}|Select-Object -ExpandProperty FileName)
                $entry.fileAccessModules=@($mods|Where-Object {$PSItem.ModuleName -in @('TyEnc64.dll','intefsctrl64.dll')}|Select-Object -ExpandProperty ModuleName)
            }catch{$entry.moduleInspection='access-denied'}
        }
        $report.processes+=@($entry)
    }
    if(-not $targets.Count){
        if($unreadable){Stop-Check 'HOST_PATH_UNREADABLE' '无法读取 Sysplorer 进程路径。请以运行 Sysplorer 的同一 Windows 用户检查；必要时在管理员 PowerShell 运行检查。'}
        Stop-Check 'HOST_NOT_RUNNING' ('所选安装的 Sysplorer 未运行。请打开：'+$target+'。其他安装的窗口不算本服务器。')
    }
    if(-not(Test-Path -LiteralPath $plugin)){Stop-Check 'PLUGIN_NOT_INSTALLED' '所选安装缺少插件 DLL。保存关闭该安装的 Sysplorer，再运行 Setup-Server.cmd。'}
    $report['pluginSha256']=(Get-FileHash -LiteralPath $plugin).Hash
    $selected=@($report.processes|Where-Object {$PSItem.selected})
    if(-not @($selected|Where-Object {$PSItem.pluginLoaded}).Count -and -not @($selected|Where-Object {$PSItem.moduleInspection -eq 'access-denied'}).Count){
        Stop-Check 'PLUGIN_NOT_LOADED' 'Sysplorer 已运行，但没有加载 battery_udp_control.dll。请在 Sysplorer 的插件管理中检查 Battery UDP Control 是否启用，以及插件加载错误；不要重复生成凭据。'
    }
    if(-not $StatusDirectory){$StatusDirectory=Join-Path $env:LOCALAPPDATA 'BatteryConsole\remote'}
    $report['statusDirectory']=$StatusDirectory;$listener=$null;$liveRecords=@()
    foreach($record in @(Get-ChildItem -LiteralPath $StatusDirectory -Filter '*.json' -ErrorAction SilentlyContinue|Sort-Object LastWriteTime -Descending)){
        try{$status=Get-Content -LiteralPath $record.FullName -Raw|ConvertFrom-Json}catch{$report.statusRecords+=@{path=$record.FullName;error='STATUS_JSON_READ_FAILED'};continue}
        $isTarget=@($targets|Where-Object {$PSItem.Id -eq $status.pid}).Count -gt 0
        $entry=@{pid=$status.pid;enabled=$status.enabled;listening=$status.listening;address=$status.address;port=$status.port;error=(Safe-Message ([string]$status.error));selected=$isTarget;path=$record.FullName}
        $report.statusRecords+=@($entry)
        if(-not $isTarget){continue};$liveRecords+=@($entry)
        if($status.enabled -and $status.listening -and $status.port -eq $Port){$listener=$status}
    }
    if(-not $liveRecords.Count){Stop-Check 'PLUGIN_STATUS_MISSING' '目标插件未生成当前用户的状态记录。请检查插件加载错误、运行用户及配置的 StatusFile/BATTERY_REMOTE_CONFIG 是否指向旧位置；诊断已列出进程和加载模块。'}
    if(-not $listener){
        if(-not @($liveRecords|Where-Object {$PSItem.enabled}).Count){
            if($report.configuration.enabled){Stop-Check 'CONFIG_VIEW_MISMATCH' '磁盘配置已启用，但运行中的插件报告远程禁用。请确认配置后已手动重启目标 Sysplorer；若仍不同，检查该进程的配置读取权限或透明加密策略。'}
            Stop-Check 'REMOTE_DISABLED' '远程监听配置尚未启用。保存关闭目标 Sysplorer，运行 Setup-Server.cmd 后再通过原图标启动。'
        }
        $errors=@($liveRecords|ForEach-Object {$PSItem.error}|Where-Object {$PSItem}) -join '; '
        Stop-Check 'PORT_NOT_LISTENING' ('插件已加载，但所选 TCP 端口未监听。请核对诊断中的实际端口及监听错误。'+$errors)
    }
    try{
        $report.listeners=@(Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction Stop|Select-Object LocalAddress,LocalPort,OwningProcess)
    }catch{$report['listenerInspection']='not-available; authenticated connection still required'}
    $report.stage='authentication'
    $probeFile=Join-Path ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($OutputFile))) ('preflight-'+[guid]::NewGuid().ToString('N')+'.json')
    $address=if($listener.address -eq '0.0.0.0'){'127.0.0.1'}elseif($listener.address -eq '::'){'::1'}else{$listener.address}
    try{& (Join-Path $PSScriptRoot 'Test-RemoteModel.ps1') -Address $address -Port $Port -CredentialConfigPath $config -OutputFile $probeFile|Out-Null}
    finally{if(Test-Path -LiteralPath $probeFile){$report.preflight=Get-Content -LiteralPath $probeFile -Raw|ConvertFrom-Json}}
    $probe=$report.preflight
    if($probe.authenticated -and $probe.capabilitiesMatch -and -not $probe.ready -and $probe.controllerState -eq 'Idle' -and $probe.backgroundTaskRunning){
        Stop-Check 'HOST_BACKGROUND_WAIT' '宿主控制器空闲，但后台初始化/工作任务尚未结束。请稍后重新检查；尚未判定为其他仿真任务占用。'
    }
    if(-not $probe.authenticated -or -not $probe.capabilitiesMatch -or -not $probe.ready -or $probe.controllerState -ne 'Idle'){
        Stop-Check 'ENGINE_NOT_READY' ('监听已找到，但认证/上传/引擎尚未全部就绪。controller='+$probe.controllerState+'；请等待初始化或先结束运行任务。')
    }
    $report.ready=$true;$report.stage='complete';$report.code='READY';$report.message='可以连接：监听、双向认证、模型上传能力、仿真引擎均已就绪。'
}catch{
    if(-not $report.code){$report.code='CHECK_FAILED'};$report.message=Safe-Message $PSItem.Exception.Message
}finally{
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($OutputFile)))|Out-Null
    [IO.File]::WriteAllText([IO.Path]::GetFullPath($OutputFile),($report|ConvertTo-Json -Depth 9),(New-Object Text.UTF8Encoding($true)))
    $accessKey=$null;$text=$null
}
Write-Output ('['+$report.code+'] '+$report.message)
Write-Output ('诊断文件：'+[IO.Path]::GetFullPath($OutputFile))
if(-not $report.ready){throw ($report.code+': '+$report.message)}
Write-Output '此检查在服务器本机进行，客户端仍需验证实际网络连通性。'