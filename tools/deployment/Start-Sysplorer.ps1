param([string]$SysplorerHome='', [string]$Workspace='', [switch]$PassThru, [switch]$Hidden)
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -ErrorAction Stop
$receipt=Join-Path $PSScriptRoot 'deployment.json'
if(Test-Path -LiteralPath $receipt){
    $installed=Get-Content -LiteralPath $receipt -Raw -Encoding UTF8|ConvertFrom-Json
    if(-not $SysplorerHome){$SysplorerHome=$installed.sysplorerHome}
    if(-not $Workspace){$Workspace=$installed.workspace}
}
if(-not $SysplorerHome){throw 'Run Install-Server.ps1 first or supply -SysplorerHome'}
if(-not $Workspace){$Workspace='C:\BatteryJobs'}
$hostRoot=[IO.Path]::GetFullPath($SysplorerHome)
$bin=Join-Path $hostRoot 'Bin64'
$hostExe=Join-Path $bin 'mworks.exe'
if(-not(Test-Path -LiteralPath $hostExe)){throw "Sysplorer not found: $hostExe"}
$work=Join-Path $Workspace 'host'
[IO.Directory]::CreateDirectory($work)|Out-Null
$cleared=@('QT_QPA_PLATFORM','QT_QPA_PLATFORM_PLUGIN_PATH','QT_PLUGIN_PATH','PYTHONHOME','PYTHONPATH')
$saved=@{}
foreach($name in $cleared+@('PATH','BATTERY_SIM_WORKSPACE')){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
try{
    foreach($name in $cleared){Remove-Item "Env:$name" -ErrorAction SilentlyContinue}
    # The solver executable lives in Simulator\Bin64, while some required
    # runtime DLLs live in Bin64. Both the host and its solver children must
    # inherit these paths; loading mworks.exe by absolute path is insufficient.
    $env:PATH=$bin+';'+(Join-Path $hostRoot 'Simulator\Bin64')+';'+$saved['PATH']
    $env:BATTERY_SIM_WORKSPACE=$Workspace
    # Normal starts the user's interactive Sysplorer; Hidden is for isolated tests.
    $options=@{FilePath=$hostExe;ArgumentList=@('-d',('"'+$work+'"'),'-w',('"'+$work+'"'));WorkingDirectory=$work;PassThru=$true;WindowStyle='Normal'}
    if($Hidden){$options.WindowStyle='Hidden'}
    $started=Start-Process @options
    if($PassThru){return $started}
}finally{
    foreach($name in $saved.Keys){[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}
}
