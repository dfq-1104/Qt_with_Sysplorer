param([string]$OutputDirectory='', [string]$PluginDll='', [switch]$LocalDocumentation, [string]$QtHome='C:\Qt\Qt5.14.2\5.14.2\msvc2017_64',
      [string]$VcRedist='C:\Program Files (x86)\Microsoft Visual Studio\2017\Professional\VC\Redist\MSVC\14.16.27012\x64\Microsoft.VC141.CRT')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$output=if($OutputDirectory){[IO.Path]::GetFullPath($OutputDirectory)}else{Join-Path $root 'dist\remote'}
if(-not $output.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Package output must stay inside this worktree'}
$client=Join-Path $output 'BatteryConsole-Client-win64'
$server=Join-Path $output 'BatteryConsole-Server-win64'
foreach($path in @($client,$server)){
    if(Test-Path -LiteralPath $path){throw "Output already exists; use a new worktree-local -OutputDirectory: $path"}
    [IO.Directory]::CreateDirectory($path)|Out-Null
}
& (Join-Path $root 'app\ui\deploy.ps1') -RuntimeDirectory $client -QtHome $QtHome
if(-not(Test-Path -LiteralPath (Join-Path $VcRedist 'vcruntime140.dll'))){throw 'VC++ x64 runtime DLLs missing; provide -VcRedist'}
Get-ChildItem -LiteralPath $VcRedist -Filter '*.dll' | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $client}
foreach($name in @('libEGL.dll','libGLESv2.dll','d3dcompiler_47.dll','opengl32sw.dll')){
    $source=Join-Path $QtHome "bin\$name";if(Test-Path -LiteralPath $source){Copy-Item -LiteralPath $source -Destination $client}
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Start-Client.ps1') -Destination $client
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Check-Local.ps1') -Destination $client
Copy-Item -LiteralPath (Join-Path $root 'app\ui\UDP运行说明.md') -Destination (Join-Path $client '本机运行说明.md')
"[Connection]`r`nMode=local`r`nAddress=127.0.0.1`r`nPort=39001`r`n`r`n[Host]`r`nHome=C:/Program Files/MWORKS/Sysplorer 2026a`r`nTimeoutMs=180000`r`n" | Set-Content -LiteralPath (Join-Path $client 'sysplorer.ini') -Encoding UTF8
$pluginSource=if($PluginDll){[IO.Path]::GetFullPath($PluginDll)}else{Join-Path $root 'tools\sysplorer_udp_plugin\release\battery_udp_control.dll'}
Copy-Item -LiteralPath $pluginSource -Destination (Join-Path $server 'battery_udp_control.dll')
foreach($name in @('Install-Server.ps1','Update-Plugin.ps1','Start-Sysplorer.ps1','Check-Server.ps1','Diagnose-Server.ps1','remote.ini.example')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $server}
foreach($folder in @($client,$server)){
    $deploymentDoc=if($LocalDocumentation){'app\ui\UDP运行说明.md'}else{'docs\远程连接部署说明.md'}
    $verificationDoc=if($LocalDocumentation){'docs\本机连接验证记录.md'}else{'docs\远程连接验证记录.md'}
    Copy-Item -LiteralPath (Join-Path $root $deploymentDoc) -Destination (Join-Path $folder '部署说明.md')
    Copy-Item -LiteralPath (Join-Path $root $verificationDoc) -Destination (Join-Path $folder '验证记录.md')
    if(-not $LocalDocumentation){
        Copy-Item -LiteralPath (Join-Path $root 'docs\远程部署实操与故障处理_2026b.md') -Destination $folder
        Copy-Item -LiteralPath (Join-Path $root 'docs\2026b插件加载修复说明.md') -Destination $folder
    }
    $manifest=@(Get-ChildItem -LiteralPath $folder -File -Recurse|Sort-Object FullName|ForEach-Object {
        [ordered]@{path=$_.FullName.Substring($folder.Length+1).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
    })
    [ordered]@{protocol='battery-tcp/1';architecture='win64';modelHash=(Get-FileHash -LiteralPath (Join-Path $root 'model\BatterySim.mo')).Hash.ToLowerInvariant();builtAt=(Get-Date).ToString('o');files=$manifest} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $folder 'manifest.json') -Encoding UTF8
    Compress-Archive -LiteralPath $folder -DestinationPath ($folder+'.zip') -CompressionLevel Optimal
}
Get-ChildItem -LiteralPath $output -Filter '*.zip' | Select-Object FullName,Length
