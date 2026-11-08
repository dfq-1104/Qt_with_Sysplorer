param([Parameter(Mandatory=$true)][string]$Address,[int]$Port=39001,
      [string]$KeyFile='', [string]$ExpectedModelHash='', [string]$OutputFile='')
$ErrorActionPreference='Stop'
# A Qt-launched Windows PowerShell process can inherit PowerShell 7 module
# paths. Load the host's own standard utility cmdlets explicitly.
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -ErrorAction Stop
if(-not $OutputFile){$OutputFile=Join-Path $PSScriptRoot 'remote-diagnostic.json'}
$output=[IO.Path]::GetFullPath($OutputFile)
function Get-ExistingRemoteKey([string]$Path){
    if($Path){return [IO.File]::ReadAllText([IO.Path]::GetFullPath($Path),[Text.Encoding]::UTF8).Trim()}
    $settings=Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'BatteryConsole\RemoteConnection.ini'
    $text=[IO.File]::ReadAllText($settings,[Text.Encoding]::UTF8)
    $line=[regex]::Match($text,'(?m)^ProtectedKey=(.+)$').Groups[1].Value.Trim().Trim('"')
    if($line.StartsWith('@ByteArray(') -and $line.EndsWith(')')){$line=$line.Substring(11,$line.Length-12)}
    Add-Type -AssemblyName System.Security
    $encrypted=[Convert]::FromBase64String($line)
    return [Text.Encoding]::UTF8.GetString([Security.Cryptography.ProtectedData]::Unprotect($encrypted,$null,[Security.Cryptography.DataProtectionScope]::CurrentUser))
}
function Hmac-Hex([byte[]]$Key,[string]$Data){
    $hmac=New-Object Security.Cryptography.HMACSHA256
    try{$hmac.Key=$Key;return [BitConverter]::ToString($hmac.ComputeHash([Text.Encoding]::UTF8.GetBytes($Data))).Replace('-','').ToLowerInvariant()}finally{$hmac.Dispose()}
}
function Read-Exact($Stream,[int]$Count){
    $bytes=New-Object byte[] $Count;$offset=0
    while($offset -lt $Count){$read=$Stream.Read($bytes,$offset,$Count-$offset);if(-not $read){throw 'Server closed the connection before completing the current frame'};$offset+=$read}
    return ,$bytes
}
function Read-Frame($Stream){
    $header=Read-Exact $Stream 4
    $size=[int]$header[0]*16777216+[int]$header[1]*65536+[int]$header[2]*256+[int]$header[3]
    if($size -lt 1 -or $size -gt 1048576){throw 'Invalid protocol frame length'}
    return ([Text.Encoding]::UTF8.GetString((Read-Exact $Stream $size))|ConvertFrom-Json)
}
function Write-Frame($Stream,$Value){
    $data=[Text.Encoding]::UTF8.GetBytes(($Value|ConvertTo-Json -Depth 8 -Compress))
    $header=[BitConverter]::GetBytes([Net.IPAddress]::HostToNetworkOrder([int]$data.Length))
    $Stream.Write($header,0,4);$Stream.Write($data,0,$data.Length);$Stream.Flush()
}
$report=[ordered]@{address=$Address;port=$Port;expectedModelHash=$ExpectedModelHash;stage='credential';authenticated=$false;modelChecked=$false;comparison='not-checked';modelMatches=$null;capabilitiesMatch=$null;commandsSent=@();error=''}
$socket=New-Object Net.Sockets.TcpClient
try{
    $key=Get-ExistingRemoteKey $KeyFile
    if($key.Length -lt 32){throw 'Existing access credential is missing or shorter than 32 characters'}
    $report.stage='tcp-connect';$pending=$socket.BeginConnect($Address,$Port,$null,$null)
    if(-not $pending.AsyncWaitHandle.WaitOne(5000,$false)){throw 'TCP connection timed out'}
    $socket.EndConnect($pending);$stream=$socket.GetStream();$stream.ReadTimeout=5000;$stream.WriteTimeout=5000
    $report.stage='server-challenge';$challenge=Read-Frame $stream
    if($challenge.type -ne 'challenge' -or $challenge.protocol -ne 'battery-tcp/1'){throw 'Unexpected server protocol'}
    $identity=[guid]::NewGuid().ToString();$nonce=[guid]::NewGuid().ToString()+[guid]::NewGuid().ToString()
    $seed=$challenge.nonce+"`n"+$nonce+"`n"+$identity+"`n"+$challenge.session
    $keyBytes=[Text.Encoding]::UTF8.GetBytes($key)
    $report.stage='authentication';Write-Frame $stream @{type='authenticate';protocol='battery-tcp/1';client=$identity;nonce=$nonce;proof=(Hmac-Hex $keyBytes ("client`n"+$seed))}
    $auth=Read-Frame $stream
    if($auth.type -ne 'authenticated' -or $auth.proof -cne (Hmac-Hex $keyBytes ("server`n"+$seed))){throw 'Server credential proof failed'}
    $report.authenticated=$true;$report.stage='model-hello';$channel=[Text.Encoding]::ASCII.GetBytes((Hmac-Hex $keyBytes ("channel`n"+$seed)))
    $id=[guid]::NewGuid().ToString()
    $request=@{schema=1;id=$id;session=$challenge.session;client=$identity;taskId='';command='hello';args=@{}}
    $payload=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes(($request|ConvertTo-Json -Depth 5 -Compress)))
    Write-Frame $stream @{n='1';payload=$payload;mac=(Hmac-Hex $channel ("client`n1`n"+$payload))}
    $report.commandsSent=@('hello');$frame=Read-Frame $stream
    if($frame.n -ne '1' -or $frame.mac -cne (Hmac-Hex $channel ("server`n1`n"+$frame.payload))){throw 'Server response signature or sequence failed'}
    $reply=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($frame.payload))|ConvertFrom-Json
    $report['helloValidation']=[ordered]@{schema=$reply.schema;idMatches=($reply.id -eq $id);sessionMatches=($reply.session -eq $challenge.session);taskIdEmpty=([string]::IsNullOrEmpty($reply.taskId));ok=($reply.ok -eq $true);code=$reply.code;message=$reply.message}
    if($reply.schema -ne 1 -or $reply.id -ne $id -or $reply.session -ne $challenge.session -or -not [string]::IsNullOrEmpty($reply.taskId)){throw 'Invalid hello response; see helloValidation in the diagnostic'}
    $missing=@(foreach($name in @('modelLifecycle','splitLifecycle')){if($reply.$name -ne $true){$name}})
    if($reply.modelTransferVersion -ne 1){$missing+='modelTransferVersion=1'}
    if($reply.modelInterface -ne 'battery-console/1'){$missing+='modelInterface=battery-console/1'}
    $report.modelChecked=$false;$report.modelMatches=$null;$report.capabilitiesMatch=($missing.Count -eq 0)
    $report['actualModelHash']=$reply.modelHash;$report['missingCapabilities']=$missing
    $report['capabilities']=[ordered]@{modelLifecycle=($reply.modelLifecycle -eq $true);splitLifecycle=($reply.splitLifecycle -eq $true)}
    $report['ready']=($reply.ready -eq $true);$report['modelLoaded']=($reply.modelLoaded -eq $true);$report['phase']=$reply.status.phase;$report['hostPid']=$reply.status.pid
    $report['controllerState']=$reply.controllerState
    $report['lastTask']=[ordered]@{taskId=$reply.status.taskId;phase=$reply.status.phase;stage=$reply.status.stage;code=$reply.status.code;message=$reply.status.message;serverRunDirectory=$reply.status.jobDir}
    $report.comparison=if($report.capabilitiesMatch){'upload-capable; model verified after upload'}else{'capability-mismatch'}
    $report.stage='complete'
    if(-not $reply.ok){$report.stage='host-readiness';throw ('Host readiness/API check failed: '+$reply.code)}
}catch{$report.error=$_.Exception.Message}finally{
    $socket.Dispose();$key=$null;$keyBytes=$null;$channel=$null
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($output))|Out-Null
    [IO.File]::WriteAllText($output,($report|ConvertTo-Json -Depth 6),(New-Object Text.UTF8Encoding $true))
}
Write-Output "Remote model preflight: $output"
Write-Output "Authenticated=$($report.authenticated); ModelChecked=$($report.modelChecked); Comparison=$($report.comparison)"
if($report.error){throw $report.error}
if(-not $report.capabilitiesMatch){throw 'Remote plugin lacks model transfer capabilities. Update the plugin before running tasks.'}
