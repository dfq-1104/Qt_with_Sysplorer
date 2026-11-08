param(
    [ValidateSet('Local','Server')][string]$Mode='Local',
    [string]$SysplorerHome='',
    [string]$Workspace='C:\BatteryConsoleData\jobs',
    [ValidateRange(1,65535)][int]$Port=39001,
    [string]$ListenAddress='0.0.0.0',
    [string]$AllowedClientAddress='LocalSubnet',
    [string]$ClientStatePath='',
    [string]$ServerStateDirectory='',
    [string]$ResultFile='',
    [string]$UserSid=[Security.Principal.WindowsIdentity]::GetCurrent().User.Value,
    [switch]$Elevated,
    [switch]$SkipFirewall,
    [switch]$NonInteractive,
    [switch]$DryRun
)
$ErrorActionPreference='Stop'
trap { if($ResultFile){[IO.File]::WriteAllText($ResultFile,$PSItem.Exception.Message);exit 1}; throw $PSItem }
# A GUI launched from PowerShell 7 can inherit its module search path. Load
# this PowerShell executable's own Windows modules, never a development module.
foreach($module in @('Utility','Management','Security')){
    Import-Module (Join-Path $PSHOME ('Modules\Microsoft.PowerShell.'+$module+'\Microsoft.PowerShell.'+$module+'.psd1')) -ErrorAction Stop
}
# All values passed across UAC are quoted as PowerShell literals; no access key
# is passed on a command line. The original desktop user's SID is retained.
function Quote-Literal([string]$Value){ return "'"+$Value.Replace("'","''")+"'" }
function Read-IniValue([string]$Text,[string]$Name){
    $match=[regex]::Match($Text,'(?m)^\s*'+[regex]::Escape($Name)+'\s*=\s*([^\r\n]*)')
    if($match.Success){return $match.Groups[1].Value.Trim()};return ''
}
function Set-IniValue([string]$Text,[string]$Name,[string]$Value){
    if((Read-IniValue $Text $Name) -ceq $Value){return $Text}
    $pattern='(?m)^(\s*'+[regex]::Escape($Name)+'\s*=)[^\r\n]*'
    if([regex]::IsMatch($Text,$pattern)){return [regex]::Replace($Text,$pattern,[Text.RegularExpressions.MatchEvaluator]{param($match) $match.Groups[1].Value+$Value})}
    $section=[regex]::Match($Text,'(?im)^\[Remote\][^\r\n]*\r?\n')
    if(-not $section.Success){return $Text+"`r`n[Remote]`r`n$Name=$Value`r`n"}
    return $Text.Insert($section.Index+$section.Length,"$Name=$Value`r`n")
}
. (Join-Path $PSScriptRoot 'PrivateFile.Common.ps1')
function Protect-PrivateFile([string]$Path){Set-PrivateFile $Path $UserSid}
function Select-CustomHome {
    Add-Type -AssemblyName System.Windows.Forms
    $picker=New-Object Windows.Forms.OpenFileDialog
    $picker.Title='Select the installed Sysplorer Bin64\mworks.exe'
    $picker.Filter='Sysplorer (mworks.exe)|mworks.exe';$picker.InitialDirectory=$env:ProgramFiles
    if($picker.ShowDialog() -ne 'OK'){throw 'Installation selection cancelled'}
    $chosen=[IO.Path]::GetDirectoryName([IO.Path]::GetDirectoryName($picker.FileName));$picker.Dispose();return $chosen
}
$requiresSelection=$false
if(-not $SysplorerHome -and $ClientStatePath -and (Test-Path -LiteralPath $ClientStatePath)){
    $previous=Get-Content -LiteralPath $ClientStatePath -Raw | ConvertFrom-Json
    if(Test-Path -LiteralPath (Join-Path $previous.sysplorerHome 'Bin64\mworks.exe')){$SysplorerHome=$previous.sysplorerHome}else{$requiresSelection=$true}
}
if(-not $SysplorerHome){
    $homes=@(Get-ChildItem -LiteralPath (Join-Path $env:ProgramFiles 'MWORKS') -Directory -ErrorAction SilentlyContinue |
        Where-Object {$PSItem.Name -match '^Sysplorer 2026[ab]$'}|Select-Object -ExpandProperty FullName)
    foreach($registry in @('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*','HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*','HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*')){
        $homes+=@(Get-ItemProperty -Path $registry -ErrorAction SilentlyContinue|Where-Object {$PSItem.DisplayName -match 'Sysplorer' -and $PSItem.InstallLocation}|Select-Object -ExpandProperty InstallLocation)
    }
    $candidates=@($homes|Where-Object {
        $image=Join-Path $PSItem 'Bin64\mworks.exe'
        (Test-Path -LiteralPath $image) -and [Diagnostics.FileVersionInfo]::GetVersionInfo($image).FileMajorPart -eq 26
    }|ForEach-Object {[IO.Path]::GetFullPath($PSItem).TrimEnd('\')}|Sort-Object -Unique)
    if($candidates.Count -eq 0){$SysplorerHome=Select-CustomHome}
    elseif($candidates.Count -eq 1 -and -not $requiresSelection){$SysplorerHome=$candidates[0]}
    else{
        Add-Type -AssemblyName System.Windows.Forms
        $form=New-Object Windows.Forms.Form;$form.Text='Select Sysplorer installation';$form.Width=700;$form.Height=200;$form.StartPosition='CenterScreen'
        $list=New-Object Windows.Forms.ComboBox;$list.DropDownStyle='DropDownList';$list.Left=20;$list.Top=20;$list.Width=640
        foreach($candidate in $candidates){[void]$list.Items.Add($candidate)}
        [void]$list.Items.Add('Browse for another installed mworks.exe...')
        $button=New-Object Windows.Forms.Button;$button.Text='OK';$button.Left=540;$button.Top=70;$button.DialogResult='OK'
        $form.Controls.AddRange(@($list,$button));$form.AcceptButton=$button
        if($form.ShowDialog() -ne 'OK' -or $list.SelectedIndex -lt 0){throw 'Installation selection cancelled'}
        $selection=$list.SelectedIndex;$SysplorerHome=[string]$list.SelectedItem;$form.Dispose()
        if($selection -eq $candidates.Count){$SysplorerHome=Select-CustomHome}
    }
}
$hostRoot=[IO.Path]::GetFullPath($SysplorerHome)
$hostExe=Join-Path $hostRoot 'Bin64\mworks.exe'
$source=Join-Path $PSScriptRoot 'battery_udp_control.dll'
$taskRoot=[IO.Path]::GetFullPath($Workspace)
if($taskRoot -match '[^\x00-\x7F]'){throw 'The task directory must use an ASCII path'}
if(-not(Test-Path -LiteralPath $hostExe) -or -not(Test-Path -LiteralPath $source)){throw 'Missing Sysplorer executable or packaged plugin'}
$version=[Diagnostics.FileVersionInfo]::GetVersionInfo($hostExe)
if($version.FileMajorPart -ne 26){throw 'Only Sysplorer 2026a/2026b x64 is supported'}
$reader=New-Object IO.BinaryReader([IO.File]::OpenRead($hostExe))
try{
    [void]$reader.BaseStream.Seek(0x3c,[IO.SeekOrigin]::Begin);$peOffset=$reader.ReadInt32()
    [void]$reader.BaseStream.Seek($peOffset,[IO.SeekOrigin]::Begin)
    if($reader.ReadUInt32() -ne 0x00004550 -or $reader.ReadUInt16() -ne 0x8664){throw 'Sysplorer must be a Windows x64 executable'}
}finally{$reader.Dispose()}
$folder=Join-Path $hostRoot 'Bin64\addins\BatteryUdpControl'
$target=Join-Path $folder 'battery_udp_control.dll';$config=Join-Path $folder 'remote.ini'
$existingConfig=if(Test-Path -LiteralPath $config){[IO.File]::ReadAllText($config)}else{''}
$configExists=Test-Path -LiteralPath $config
# Updating an existing deployment preserves its complete INI byte for byte.
if($configExists){
    $configuredWorkspace=Read-IniValue $existingConfig 'Workspace';if($configuredWorkspace){$taskRoot=[IO.Path]::GetFullPath($configuredWorkspace)}

    if($taskRoot -match '[^\x00-\x7F]'){throw 'Existing Workspace must use an ASCII path'}
    $configuredPort=Read-IniValue $existingConfig 'Port';if($configuredPort){$Port=[int]$configuredPort}
}
$pluginHash=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$manifest=Join-Path $PSScriptRoot 'manifest.json'
if(Test-Path -LiteralPath $manifest){
    $entries=@((Get-Content -LiteralPath $manifest -Raw|ConvertFrom-Json).files|Where-Object {$PSItem.path -eq 'battery_udp_control.dll'})
    if($entries.Count -ne 1 -or $entries[0].sha256 -ne $pluginHash){throw 'Packaged plugin differs from manifest; no changes made'}
}
$needsCopy=-not(Test-Path -LiteralPath $target)
if(-not $needsCopy){$needsCopy=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $pluginHash}
$desiredConfig=$existingConfig
if(-not $configExists){$desiredConfig="[Remote]`r`nEnabled=false`r`nListenAddress=$ListenAddress`r`nPort=$Port`r`nWorkspace=$($taskRoot.Replace('\','/'))`r`nAccessKey=`r`n"}
if($Mode -eq 'Server'){
    $desiredConfig=Set-IniValue $desiredConfig 'Enabled' 'true'
    # Explicit server setup promotes a local-only deployment. Preserve valid
    # credentials and port; never rotate a deployed credential implicitly.
    if((Read-IniValue $desiredConfig 'AccessKey').Length -lt 32){
        if(-not $DryRun){
            $bytes=New-Object byte[] 32;$rng=[Security.Cryptography.RandomNumberGenerator]::Create()
            try{$rng.GetBytes($bytes)}finally{$rng.Dispose()}
            $newKey=[BitConverter]::ToString($bytes).Replace('-','').ToLowerInvariant()
        }else{$newKey='GENERATED-ONLY-DURING-REAL-SETUP'}
        $desiredConfig=Set-IniValue $desiredConfig 'AccessKey' $newKey
    }
    $oldAddress=Read-IniValue $desiredConfig 'ListenAddress'
    if(-not $oldAddress -or $oldAddress -eq '127.0.0.1' -or $oldAddress -eq '::1'){$desiredConfig=Set-IniValue $desiredConfig 'ListenAddress' $ListenAddress}
    if(-not (Read-IniValue $desiredConfig 'Workspace')){$desiredConfig=Set-IniValue $desiredConfig 'Workspace' $taskRoot.Replace('\','/')}
    if(-not (Read-IniValue $desiredConfig 'Port')){$desiredConfig=Set-IniValue $desiredConfig 'Port' ([string]$Port)}
}
$configChanged= -not $configExists -or $desiredConfig -cne $existingConfig
if($Mode -eq 'Server' -and ($Port -lt 1 -or $Port -gt 65535)){throw 'Configured server port must be 1-65535; configuration was not changed'}
$isAdmin=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
$needsSetup=$needsCopy -or $configChanged -or -not(Test-Path -LiteralPath $taskRoot)
if($DryRun){
    [ordered]@{mode=$Mode;sysplorerHome=$hostRoot;workspace=$taskRoot;port=$Port;wouldCopyPlugin=$needsCopy;preserveConfig=($configExists -and -not $configChanged);wouldEnableRemote=($Mode -eq 'Server' -and $configChanged);wouldConfigureFirewall=($Mode -eq 'Server' -and -not $SkipFirewall);firewallProfiles=@('Domain','Private');allowedClientAddress=$AllowedClientAddress;firewallProgram=$hostExe;pluginSha256=$pluginHash}|ConvertTo-Json
    return
}
$protectedRoot=$hostRoot.StartsWith($env:ProgramFiles+'\',[StringComparison]::OrdinalIgnoreCase) -or $hostRoot.StartsWith($env:SystemRoot+'\',[StringComparison]::OrdinalIgnoreCase) -or $taskRoot -eq 'C:\BatteryConsoleData' -or $taskRoot.StartsWith('C:\BatteryConsoleData\',[StringComparison]::OrdinalIgnoreCase)
$requiresAdmin=($needsSetup -and $protectedRoot) -or ($Mode -eq 'Server' -and -not $SkipFirewall)
if($requiresAdmin -and -not $isAdmin){
    if($Elevated){throw 'Administrator permission was not granted'}
    $elevationReport=Join-Path ([IO.Path]::GetTempPath()) ('BatteryConsole-setup-'+[guid]::NewGuid().ToString('N')+'.txt')
    $command='& '+(Quote-Literal $PSCommandPath)+' -Elevated -Mode '+(Quote-Literal $Mode)+' -SysplorerHome '+(Quote-Literal $hostRoot)+' -Workspace '+(Quote-Literal $taskRoot)+' -Port '+$Port+' -ListenAddress '+(Quote-Literal $ListenAddress)+' -AllowedClientAddress '+(Quote-Literal $AllowedClientAddress)+' -UserSid '+(Quote-Literal $UserSid)
    if($SkipFirewall){$command+=' -SkipFirewall'}
    if($NonInteractive){$command+=' -NonInteractive'}
    $command+=' -ResultFile '+(Quote-Literal $elevationReport)
    # Client state is written by the original user below after elevation returns.
    $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
    $process=Start-Process -FilePath powershell.exe -Verb RunAs -WindowStyle Hidden -Wait -PassThru -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-EncodedCommand',$encoded)
    if($process.ExitCode -ne 0){
        $detail=if(Test-Path -LiteralPath $elevationReport){[IO.File]::ReadAllText($elevationReport)}else{'Administrator setup was cancelled or could not start.'}
        if(Test-Path -LiteralPath $elevationReport){Remove-Item -LiteralPath $elevationReport}
        throw ('Setup failed: '+$detail)
    }
    if(Test-Path -LiteralPath $elevationReport){Remove-Item -LiteralPath $elevationReport}
}else{
    if($needsCopy -or $configChanged){
        foreach($process in @(Get-Process mworks -ErrorAction SilentlyContinue)){
            try{$image=$process.Path}catch{throw 'Cannot inspect a running Sysplorer; close target instances before updating'}
            if(-not $image -or [IO.Path]::GetFullPath($image) -eq $hostExe){throw 'Save your work and close all target Sysplorer instances before plugin update. No process was stopped.'}
        }
    }
    if($needsSetup){
        [IO.Directory]::CreateDirectory($folder)|Out-Null;[IO.Directory]::CreateDirectory($taskRoot)|Out-Null
        $sid=New-Object Security.Principal.SecurityIdentifier($UserSid)
        $acl=Get-Acl -LiteralPath $taskRoot
        $acl.AddAccessRule((New-Object Security.AccessControl.FileSystemAccessRule($sid,'Modify','ContainerInherit,ObjectInherit','None','Allow')))
        Set-Acl -LiteralPath $taskRoot -AclObject $acl
        if($needsCopy){
            & (Join-Path $PSScriptRoot 'Update-Plugin.ps1') -SysplorerHome $hostRoot
        }
        if($configChanged){
            if($configExists){
                $configBackup=$config+'.before-server-setup.bak'
                if(-not(Test-Path -LiteralPath $configBackup)){[IO.File]::WriteAllText($configBackup,'');Protect-PrivateFile $configBackup;[IO.File]::WriteAllBytes($configBackup,[IO.File]::ReadAllBytes($config))}
                Protect-PrivateFile $configBackup
            }else{[IO.File]::WriteAllText($config,'')}
            Protect-PrivateFile $config
            [IO.File]::WriteAllText($config,$desiredConfig,(New-Object Text.UTF8Encoding($false)))
        }
    }
    if($Mode -eq 'Server' -and -not $SkipFirewall){
        $hasher=[Security.Cryptography.SHA256]::Create()
        try{$suffix=[BitConverter]::ToString($hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($hostRoot.ToLowerInvariant()))).Replace('-','').Substring(0,8)}finally{$hasher.Dispose()}
        $rule='BatteryConsole-Sysplorer-'+$Port+'-'+$suffix
        if(-not(Get-NetFirewallRule -Name $rule -ErrorAction SilentlyContinue)){
            New-NetFirewallRule -Name $rule -DisplayName $rule -Direction Inbound -Action Allow -Protocol TCP -LocalPort $Port -Program $hostExe -Profile Domain,Private -RemoteAddress $AllowedClientAddress|Out-Null
        }
    }
}
if($ClientStatePath){
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($ClientStatePath)))|Out-Null
    [ordered]@{schema=1;sysplorerHome=$hostRoot;workspace=$taskRoot;pluginSha256=$pluginHash}|ConvertTo-Json|Set-Content -LiteralPath $ClientStatePath -Encoding UTF8
}
Write-Output 'Setup completed. Start Sysplorer normally; no special launch script is required.'
if($Mode -eq 'Server' -and -not $Elevated){
    & (Join-Path $PSScriptRoot 'Server-Info.ps1') -SysplorerHome $hostRoot -NonInteractive:$NonInteractive -StateDirectory $ServerStateDirectory
}
