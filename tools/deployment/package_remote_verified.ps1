$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -ErrorAction Stop
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$evidence=Get-Content -LiteralPath (Join-Path $root 'logs\remote-vm\guest-validation.json') -Raw -Encoding UTF8|ConvertFrom-Json
if(-not $evidence.overallPassed -or $evidence.steps.gui -ne 'passed'){throw 'Actual guest lifecycle and isolated GUI must pass before release packaging'}
$sourceClient=Join-Path $root 'dist\remote-vm\BatteryConsole-Client-win64'
$sourceManifest=Get-Content -LiteralPath (Join-Path $sourceClient 'manifest.json') -Raw -Encoding UTF8|ConvertFrom-Json
$modelHash=(Get-FileHash -LiteralPath (Join-Path $root 'model\BatterySim.mo')).Hash.ToLowerInvariant()
$clientHash=(Get-FileHash -LiteralPath (Join-Path $sourceClient 'battery_console_ui.exe')).Hash
$dll=Join-Path $root 'tools\sysplorer_udp_plugin\release\battery_udp_control.dll'
$pluginHash=(Get-FileHash -LiteralPath $dll).Hash
if($clientHash -ne 'F45D5349ED496BAC1EAB3C839081E4D9FD99D02244D012DF9DF4B4251132AC06' -or $clientHash -ne $evidence.clientSha256 -or $pluginHash -ne $evidence.pluginSha256 -or $modelHash -ne $evidence.modelHash){throw 'Validated binary/model baseline differs from packaging sources'}
$expectedSource=@('manifest.json')+@($sourceManifest.files|ForEach-Object {$_.path})
$actualSource=@(Get-ChildItem -LiteralPath $sourceClient -File -Recurse|ForEach-Object {$_.FullName.Substring($sourceClient.Length+1).Replace('\','/')})
if(@(Compare-Object ($expectedSource|Sort-Object) ($actualSource|Sort-Object)).Count){throw 'Unexpected file in original client runtime'}
foreach($file in $sourceManifest.files){if((Get-FileHash -LiteralPath (Join-Path $sourceClient $file.path)).Hash -ne $file.sha256){throw "Original client runtime changed: $($file.path)"}}
$output=Join-Path $root 'dist\remote-verified-20261108'
if(Test-Path -LiteralPath $output){throw 'Release directory exists; preserve it rather than overwrite'}
[IO.Directory]::CreateDirectory($output)|Out-Null
Copy-Item -LiteralPath $sourceClient -Destination $output -Recurse
$client=Join-Path $output 'BatteryConsole-Client-win64'
$server=Join-Path $output 'BatteryConsole-Server-win64'
[IO.Directory]::CreateDirectory($server)|Out-Null
Copy-Item -LiteralPath $dll -Destination (Join-Path $server 'battery_udp_control.dll')
foreach($name in @('Install-Server.ps1','Update-ServerPlugin.ps1','Start-Sysplorer.ps1','Check-Server.ps1','Diagnose-Server.ps1','remote.ini.example')){
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $server $name)
}
[ordered]@{name='Verified 2026b plugin update';newPluginSha256=$pluginHash;allowedPreviousSha256=@('9DE7BBE8C49BB7344409C08B0650EC3182EEF7D3105CFAD4C14F2FC208D3C509');allowedHostVersions=@('26.8.0.2280');modelHash=$modelHash}|
    ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $server 'patch-manifest.json') -Encoding UTF8
Add-Type -AssemblyName System.IO.Compression.FileSystem
$deliveries=@()
foreach($folder in @($client,$server)){
    Copy-Item -LiteralPath (Join-Path $root 'docs\远程模式最终部署与验收.md') -Destination (Join-Path $folder '部署说明.md') -Force
    Copy-Item -LiteralPath (Join-Path $root 'docs\虚拟机远程模式验收.md') -Destination (Join-Path $folder '验证记录.md') -Force
    Copy-Item -LiteralPath (Join-Path $root 'docs\远程求解器启动路径修复.md') -Destination (Join-Path $folder '启动路径修复.md') -Force
    $files=@(Get-ChildItem -LiteralPath $folder -File -Recurse|Where-Object Name -ne 'manifest.json'|Sort-Object FullName|ForEach-Object {
        [ordered]@{path=$_.FullName.Substring($folder.Length+1).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}
    })
    foreach($file in $files){if($file.path -match '(^|/)(access-key\.txt|remote\.ini|deployment\.json|RemoteConnection\.ini|ButtonDetails\.ini)$'){throw 'Private configuration in release package'}}
    [ordered]@{protocol='battery-tcp/1';architecture='win64';date='2026-11-08';modelHash=$modelHash;clientSha256=$clientHash;pluginSha256=$pluginHash;hostVersion='26.8.0.2280';realGuestLifecyclePassed=$true;isolatedGuiPassed=$true;files=$files}|
        ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $folder 'manifest.json') -Encoding UTF8
    $zip=$folder+'.zip';Compress-Archive -LiteralPath $folder -DestinationPath $zip
    $archive=[IO.Compression.ZipFile]::OpenRead($zip)
    try{
        $base=Split-Path -Leaf $folder
        $expected=@($files|ForEach-Object {$base+'/'+$_.path})+@($base+'/manifest.json')
        $actual=@($archive.Entries|Where-Object Name|ForEach-Object {$_.FullName.Replace('\','/')})
        if(@(Compare-Object ($expected|Sort-Object) ($actual|Sort-Object)).Count){throw 'ZIP inventory differs from release manifest'}
        foreach($entry in $archive.Entries|Where-Object Name){
            $stream=$entry.Open();$sha=[Security.Cryptography.SHA256]::Create()
            try{$hash=[BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','')}finally{$stream.Dispose();$sha.Dispose()}
            $relative=$entry.FullName.Replace('\','/').Substring($base.Length+1)
            if($hash -ne (Get-FileHash -LiteralPath (Join-Path $folder $relative)).Hash){throw "ZIP hash mismatch: $relative"}
        }
    }finally{$archive.Dispose()}
    $deliveries+=@{file=$zip;bytes=(Get-Item -LiteralPath $zip).Length;sha256=(Get-FileHash -LiteralPath $zip).Hash;entries=$expected.Count}
}
if((Get-FileHash -LiteralPath (Join-Path $client 'battery_console_ui.exe')).Hash -ne $clientHash){throw 'Packaged client executable changed'}
$deliveries|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $output 'delivery.json') -Encoding UTF8
$deliveries|Format-Table -AutoSize
