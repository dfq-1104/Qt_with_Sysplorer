param(
    [string]$SysplorerHome='C:\Program Files\MWORKS\Sysplorer 2026a',
    [string]$ListenAddress='0.0.0.0',
    [ValidateRange(1,65535)][int]$Port=39001,
    [string]$Workspace='C:\BatteryJobs',
    [string]$ServerUser=[Security.Principal.WindowsIdentity]::GetCurrent().Name,
    [switch]$OpenFirewall,
    [string]$AllowedClientAddress='LocalSubnet',
    [switch]$DisableRemote
)
$ErrorActionPreference='Stop'
$hostRoot=[IO.Path]::GetFullPath($SysplorerHome)
$hostExe=Join-Path $hostRoot 'Bin64\mworks.exe'
$source=Join-Path $PSScriptRoot 'battery_udp_control.dll'
$taskRoot=[IO.Path]::GetFullPath($Workspace)
$listenIp=$null
if(-not [Net.IPAddress]::TryParse($ListenAddress,[ref]$listenIp)){throw 'ListenAddress must be an IP address, such as 0.0.0.0 or 192.168.1.20'}
if($taskRoot -match '[^\x00-\x7F]' -or $hostRoot -match '[^\x00-\x7F]'){throw 'SysplorerHome and Workspace must use ASCII paths'}
if(-not(Test-Path -LiteralPath $hostExe)){throw "Sysplorer not found: $hostExe"}
if(-not(Test-Path -LiteralPath $source)){throw 'Server plugin DLL missing from this package'}
$isAdmin=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if(-not $isAdmin -and $OpenFirewall){throw 'Firewall changes require an Administrator PowerShell. No application was changed.'}
if(-not $isAdmin){Write-Output 'Installing with current-user permissions. A protected Sysplorer installation directory requires an Administrator PowerShell.'}
$processes=@(Get-Process mworks -ErrorAction SilentlyContinue)
foreach($process in $processes){if(-not $process.Path -or $process.Path -eq $hostExe){throw 'Close the target Sysplorer before installing or updating its plugin'}}
$serverSid=(New-Object Security.Principal.NTAccount($ServerUser)).Translate([Security.Principal.SecurityIdentifier])
function Set-PrivateFile([string]$Path){
    $acl=New-Object Security.AccessControl.FileSecurity
    $acl.SetAccessRuleProtection($true,$false)
    foreach($sid in @($serverSid,(New-Object Security.Principal.SecurityIdentifier('S-1-5-18')),(New-Object Security.Principal.SecurityIdentifier('S-1-5-32-544')))){
        $acl.AddAccessRule((New-Object Security.AccessControl.FileSystemAccessRule($sid,'FullControl','Allow')))
    }
    Set-Acl -LiteralPath $Path -AclObject $acl
}
$folder=Join-Path $hostRoot 'Bin64\addins\BatteryUdpControl'
[IO.Directory]::CreateDirectory($folder)|Out-Null
[IO.Directory]::CreateDirectory($taskRoot)|Out-Null
# Grant the intended desktop user task-directory access without changing its parent.
$workspaceAcl=Get-Acl -LiteralPath $taskRoot
$workspaceAcl.AddAccessRule((New-Object Security.AccessControl.FileSystemAccessRule($serverSid,'Modify','ContainerInherit,ObjectInherit','None','Allow')))
Set-Acl -LiteralPath $taskRoot -AclObject $workspaceAcl
$target=Join-Path $folder 'battery_udp_control.dll'
$config=Join-Path $folder 'remote.ini'
$stamp=Get-Date -Format 'yyyyMMdd-HHmmssfff'
foreach($existing in @($target,$config)){
    if(Test-Path -LiteralPath $existing){
        $backup="$existing.$stamp.bak";Copy-Item -LiteralPath $existing -Destination $backup
        if($existing -eq $config){Set-PrivateFile $backup}
    }
}
Copy-Item -LiteralPath $source -Destination $target -Force
if((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $target).Hash){throw 'Installed plugin hash mismatch'}
$keyBytes=New-Object byte[] 32
$rng=[Security.Cryptography.RandomNumberGenerator]::Create()
try{$rng.GetBytes($keyBytes)}finally{$rng.Dispose()}
$accessKey=[BitConverter]::ToString($keyBytes).Replace('-','').ToLowerInvariant()
$enabled=if($DisableRemote){'false'}else{'true'}
$content="[Remote]`r`nEnabled=$enabled`r`nListenAddress=$ListenAddress`r`nPort=$Port`r`nWorkspace=$($taskRoot.Replace('\','/'))`r`nAccessKey=$accessKey`r`n"
# Create a restricted file before writing the secret.
if(-not(Test-Path -LiteralPath $config)){[IO.File]::WriteAllText($config,'')};Set-PrivateFile $config
[IO.File]::WriteAllText($config,$content,(New-Object Text.UTF8Encoding($false)))
$keyPath=Join-Path $PSScriptRoot 'access-key.txt'
if(-not(Test-Path -LiteralPath $keyPath)){[IO.File]::WriteAllText($keyPath,'')};Set-PrivateFile $keyPath
[IO.File]::WriteAllText($keyPath,$accessKey,(New-Object Text.UTF8Encoding($false)))
if($OpenFirewall -and -not $DisableRemote){
    $ruleName="BatteryConsole-Sysplorer-TCP-$Port"
    $oldRule=Get-NetFirewallRule -Name $ruleName -ErrorAction SilentlyContinue
    if($oldRule){Remove-NetFirewallRule -Name $ruleName}
    New-NetFirewallRule -Name $ruleName -DisplayName $ruleName -Direction Inbound -Action Allow -Protocol TCP -LocalPort $Port -Program $hostExe -Profile Domain,Private -RemoteAddress $AllowedClientAddress | Out-Null
}
$receipt=[ordered]@{sysplorerHome=$hostRoot;workspace=$taskRoot;port=$Port;listenAddress=$ListenAddress;serverUser=$ServerUser;remoteEnabled=(-not $DisableRemote);pluginSha256=(Get-FileHash -LiteralPath $target).Hash;installedAt=(Get-Date).ToString('o')}
$receipt | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'deployment.json') -Encoding UTF8
Write-Output "Installed plugin: $target"
Write-Output "Remote configuration: $config"
Write-Output "Access key saved privately to: $keyPath (copy its value to the Qt connection dialog; do not share logs or screenshots of the key)"
Write-Output 'Start Sysplorer as the configured ServerUser. The plugin starts its listener automatically.'
if(-not $OpenFirewall -and -not $DisableRemote){Write-Output "Firewall was not changed. Allow TCP $Port for mworks.exe from the intended client on your LAN if needed."}
