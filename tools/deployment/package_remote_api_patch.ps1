$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -ErrorAction Stop
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$distribution=Join-Path $root 'dist\remote-vm'
$package=Join-Path $distribution 'BatteryConsole-Server-ApiPatch'
$dll=Join-Path $root 'tools\sysplorer_udp_plugin\release\battery_udp_control.dll'
$pluginHash=(Get-FileHash -LiteralPath $dll).Hash
$modelHash=(Get-FileHash -LiteralPath (Join-Path $root 'model\BatterySim.mo')).Hash.ToLowerInvariant()
$clientHash=(Get-FileHash -LiteralPath (Join-Path $distribution 'BatteryConsole-Client-win64\battery_console_ui.exe')).Hash
$native=Get-Content -LiteralPath (Join-Path $root 'logs\remote-vm\native-result.json') -Raw -Encoding UTF8|ConvertFrom-Json
if(-not $native.passed -or $native.pluginSha256 -ne $pluginHash -or $native.modelHash -ne $modelHash){throw 'Current DLL/model must pass isolated native regression before packaging'}
if($clientHash -ne 'F45D5349ED496BAC1EAB3C839081E4D9FD99D02244D012DF9DF4B4251132AC06' -or $modelHash -ne 'e03bd3634004ca82bb190521c214ec5fae25713120db83caf016aeac8b7472cd'){throw 'Validated client/model baseline changed'}
$files=@('battery_udp_control.dll','Update-ServerPlugin.ps1','Diagnose-Server.ps1','README.md','patch-manifest.json')
[IO.Directory]::CreateDirectory($package)|Out-Null
foreach($entry in @(Get-ChildItem -LiteralPath $package -Force)){
    if($entry.PSIsContainer -or $entry.Name -notin $files){throw "Unexpected package entry; left unchanged: $($entry.Name)"}
}
Copy-Item -LiteralPath $dll -Destination (Join-Path $package 'battery_udp_control.dll') -Force
foreach($name in @('Update-ServerPlugin.ps1','Diagnose-Server.ps1')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $package $name) -Force}
Copy-Item -LiteralPath (Join-Path $root 'docs\远程插件任务接口补丁.md') -Destination (Join-Path $package 'README.md') -Force
$manifest=[ordered]@{
    name='BatteryConsole-Server-ApiPatch'
    builtAt=(Get-Date).ToString('o')
    protocol='battery-tcp/1'
    newPluginSha256=$pluginHash
    allowedPreviousSha256=@('9DE7BBE8C49BB7344409C08B0650EC3182EEF7D3105CFAD4C14F2FC208D3C509')
    allowedHostVersions=@('26.8.0.2280')
    modelHash=$modelHash
    unchangedClientSha256=$clientHash
    validation=[ordered]@{isolatedNative2026a=$true;guest2026b='pending-install-and-test';modelOnGuest='not-yet-verified'}
    files=@()
}
$guestEvidence=Join-Path $root 'logs\remote-vm\guest-validation.json'
if(Test-Path -LiteralPath $guestEvidence){
    $guest=Get-Content -LiteralPath $guestEvidence -Raw -Encoding UTF8|ConvertFrom-Json
    if($guest.pluginSha256 -eq $pluginHash -and $guest.clientSha256 -eq $clientHash -and $guest.modelHash -eq $modelHash){
        $manifest.validation.guest2026b=if($guest.overallPassed){'passed'}else{'partial-validation-see-steps'}
        $manifest.validation.modelOnGuest=if($guest.modelVerified){'verified-match'}else{'not-yet-verified'}
        $manifest.validation['guestSteps']=$guest.steps
        $manifest.validation['guestEvidenceDate']=$guest.date
        $manifest.validation['guestFailureCode']=$guest.lastTask.code
    }
}
foreach($name in $files|Where-Object {$_ -ne 'patch-manifest.json'}){
    $path=Join-Path $package $name
    $manifest.files+=@{path=$name;bytes=(Get-Item -LiteralPath $path).Length;sha256=(Get-FileHash -LiteralPath $path).Hash}
}
$manifest|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $package 'patch-manifest.json') -Encoding UTF8
$zip=Join-Path $distribution 'BatteryConsole-Server-ApiPatch.zip'
Compress-Archive -LiteralPath $package -DestinationPath $zip -Force
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive=[IO.Compression.ZipFile]::OpenRead($zip)
try{
    $actual=@($archive.Entries|Where-Object {$_.Name}|ForEach-Object {$_.FullName.Replace('\','/')})
    $expected=@($files|ForEach-Object {'BatteryConsole-Server-ApiPatch/'+$_})
    if(@(Compare-Object ($actual|Sort-Object) ($expected|Sort-Object)).Count){throw 'Unexpected ZIP file list'}
    foreach($entry in $archive.Entries|Where-Object {$_.Name}){
        $stream=$entry.Open();$sha=[Security.Cryptography.SHA256]::Create()
        try{$zipHash=[BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','')}finally{$sha.Dispose();$stream.Dispose()}
        if($zipHash -ne (Get-FileHash -LiteralPath (Join-Path $package $entry.Name)).Hash){throw "ZIP hash mismatch: $($entry.Name)"}
    }
}finally{$archive.Dispose()}
Write-Output "Patch: $zip"
Write-Output "ZIP SHA256: $((Get-FileHash -LiteralPath $zip).Hash)"
Write-Output "DLL SHA256: $pluginHash"
Write-Output "Verified exact five-file ZIP; configuration and credentials excluded. Guest status: $($manifest.validation.guest2026b)"
