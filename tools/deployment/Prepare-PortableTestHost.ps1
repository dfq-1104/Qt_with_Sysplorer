param([string]$SysplorerHome='C:\Program Files\MWORKS\Sysplorer 2026a',[string]$Name='portable-2026a')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if($Name -notmatch '^[a-zA-Z0-9-]+$'){throw 'Use a simple test environment name'}
$destination=Join-Path $root ('.local-sysplorer\'+$Name)
if(Test-Path -LiteralPath $destination){throw 'Test environment already exists; choose a new name'}
if(-not(Test-Path -LiteralPath (Join-Path $SysplorerHome 'Bin64\mworks.exe'))){throw 'Legal source runtime is not accessible on this computer'}
[IO.Directory]::CreateDirectory($destination)|Out-Null
Copy-Item -LiteralPath (Join-Path $SysplorerHome 'Bin64') -Destination $destination -Recurse
Get-ChildItem -LiteralPath $SysplorerHome -Directory|Where-Object {$PSItem.Name -ne 'Bin64'}|ForEach-Object{
    if($PSItem.Name -eq 'Setting'){Copy-Item -LiteralPath $PSItem.FullName -Destination $destination -Recurse}
    else{New-Item -ItemType Junction -Path (Join-Path $destination $PSItem.Name) -Target $PSItem.FullName|Out-Null}
}
$plugin=Join-Path $destination 'Bin64\addins\BatteryUdpControl'
[IO.Directory]::CreateDirectory($plugin)|Out-Null
Copy-Item -LiteralPath (Join-Path $root 'tools\sysplorer_udp_plugin\release\battery_udp_control.dll') -Destination $plugin -Force
. (Join-Path $PSScriptRoot 'LocalWorktree.Common.ps1')
$drive=Connect-WorktreeDrive $root
$ascii=$drive.root+'.local-sysplorer\'+$Name
$data=$drive.root+'logs\portable\'+$Name
[IO.Directory]::CreateDirectory($data)|Out-Null
$keyBytes=New-Object byte[] 32;$rng=[Security.Cryptography.RandomNumberGenerator]::Create()
try{$rng.GetBytes($keyBytes)}finally{$rng.Dispose()}
$key=[BitConverter]::ToString($keyBytes).Replace('-','').ToLowerInvariant()
$reserve=New-Object Net.Sockets.TcpListener([Net.IPAddress]::Loopback,0);$reserve.Start();$port=$reserve.LocalEndpoint.Port;$reserve.Stop()
[IO.File]::WriteAllText((Join-Path $plugin 'remote.ini'),"[Remote]`r`nEnabled=true`r`nListenAddress=127.0.0.1`r`nPort=$port`r`nWorkspace=$($data.Replace('\','/'))/jobs`r`nStatusFile=$($data.Replace('\','/'))/server.json`r`nAccessKey=$key`r`n",(New-Object Text.UTF8Encoding($false)))
[IO.File]::WriteAllText((Join-Path $data 'test-key.txt'),$key)
[ordered]@{hostHome=$ascii;data=$data;port=$port;keyFile=(Join-Path $data 'test-key.txt');discovery=(Join-Path $data 'discovery');sourceRuntime=$SysplorerHome;mode='isolated-test-only'}|
    ConvertTo-Json|Set-Content -LiteralPath (Join-Path $data 'environment.json') -Encoding UTF8
Write-Output "Isolated test runtime ready: $ascii. No installed plugin or firewall was changed."
