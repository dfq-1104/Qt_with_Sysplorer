param([string]$RuntimeDirectory = '', [string]$SysplorerHome = '')
$ErrorActionPreference='Stop'
$root=(Resolve-Path "$PSScriptRoot\..\..").Path
$runtime=if($RuntimeDirectory){[IO.Path]::GetFullPath($RuntimeDirectory)}else{Join-Path $PSScriptRoot 'runtime'}
$qt='C:\Qt\Qt5.14.2\5.14.2\msvc2017_64'
New-Item -ItemType Directory -Force "$runtime" | Out-Null
if($SysplorerHome) {
    $hostHome=[IO.Path]::GetFullPath($SysplorerHome).Replace('\','/')
    if(-not(Test-Path -LiteralPath "$hostHome/Bin64/mworks.exe")){throw "Sysplorer executable missing: $hostHome"}
    $builtPlugin=Join-Path $root 'tools/sysplorer_udp_plugin/release/battery_udp_control.dll'
    $hostPlugin="$hostHome/Bin64/addins/BatteryUdpControl/battery_udp_control.dll"
    if((Get-FileHash -LiteralPath $builtPlugin).Hash -ne (Get-FileHash -LiteralPath $hostPlugin).Hash){throw 'Configured host plugin differs from the worktree build'}
}
function Copy-VerifiedBinary([string]$source,[string]$target) {
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
    if((Test-Path -LiteralPath $target) -and
       (Get-FileHash -LiteralPath $source).Hash -eq (Get-FileHash -LiteralPath $target).Hash) {return}
    [IO.File]::WriteAllBytes($target,[IO.File]::ReadAllBytes($source))
    if((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $target).Hash) {
        throw "Deployment hash mismatch: $target"
    }
}
Copy-VerifiedBinary "$PSScriptRoot\build\release\battery_console_ui.exe" "$runtime\battery_console_ui.exe"
foreach($name in @('Qt5Core.dll','Qt5Network.dll','Qt5Gui.dll','Qt5Widgets.dll','Qt5Svg.dll')) {
    Copy-VerifiedBinary "$qt\bin\$name" "$runtime\$name"
}
foreach($folder in @('platforms','styles','iconengines','imageformats')) {
    Get-ChildItem -LiteralPath "$qt\plugins\$folder" -Filter '*.dll' |
        Where-Object {$_.BaseName -notmatch 'd$'} |
        ForEach-Object {Copy-VerifiedBinary $_.FullName "$runtime\$folder\$($_.Name)"}
}
@"
[Paths]
Prefix=.
Plugins=.
"@ | Set-Content -LiteralPath "$runtime\qt.conf" -Encoding ascii
if($SysplorerHome) {
    "[Host]`r`nHome=$hostHome" | Set-Content -LiteralPath "$runtime/sysplorer.ini" -Encoding utf8
}
Write-Output "Deployed verified Qt binaries: $runtime"
