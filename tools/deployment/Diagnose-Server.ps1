param(
    [Parameter(Mandatory=$true)][string]$SysplorerHome,
    [string]$OutputFile='',
    [string]$PluginPath=''
)
$ErrorActionPreference='Stop'
if([string]::IsNullOrWhiteSpace($OutputFile)){
    # Resolve in the script body. Some Windows PowerShell versions do not
    # populate PSScriptRoot while evaluating parameter default expressions.
    $diagnosticScriptPath=$MyInvocation.MyCommand.Path
    if([string]::IsNullOrWhiteSpace($diagnosticScriptPath)){
        throw 'Cannot determine the script directory. Supply an absolute -OutputFile path.'
    }
    $OutputFile=Join-Path (Split-Path -Parent $diagnosticScriptPath) 'server-diagnostic.json'
}
# This diagnostic reads PE files; it never loads or executes the plugin.
# It does not read remote.ini/access-key.txt or change any host configuration.
function Get-PeImage([string]$Path) {
    $data=[IO.File]::ReadAllBytes($Path)
    if($data.Length -lt 64 -or [BitConverter]::ToUInt16($data,0) -ne 0x5a4d){throw "Not a PE file: $Path"}
    $pe=[BitConverter]::ToInt32($data,60)
    if($pe -lt 0 -or $pe+24 -ge $data.Length -or [BitConverter]::ToUInt32($data,$pe) -ne 0x4550){throw "Invalid PE header: $Path"}
    $machine=[BitConverter]::ToUInt16($data,$pe+4)
    $count=[BitConverter]::ToUInt16($data,$pe+6)
    $optionalSize=[BitConverter]::ToUInt16($data,$pe+20)
    $optional=$pe+24
    $magic=[BitConverter]::ToUInt16($data,$optional)
    if($magic -eq 0x20b){$is64=$true;$directories=$optional+112}
    elseif($magic -eq 0x10b){$is64=$false;$directories=$optional+96}
    else{throw "Unsupported PE format: $Path"}
    $sections=@()
    for($i=0;$i -lt $count;$i++){
        $at=$optional+$optionalSize+$i*40
        $sections+=[PSCustomObject]@{VirtualSize=[BitConverter]::ToUInt32($data,$at+8);Rva=[BitConverter]::ToUInt32($data,$at+12);RawSize=[BitConverter]::ToUInt32($data,$at+16);Raw=[BitConverter]::ToUInt32($data,$at+20)}
    }
    return [PSCustomObject]@{Path=$Path;Bytes=$data;Is64=$is64;Machine=$machine;Sections=$sections;Headers=[BitConverter]::ToUInt32($data,$optional+60);ExportRva=[BitConverter]::ToUInt32($data,$directories);ImportRva=[BitConverter]::ToUInt32($data,$directories+8)}
}
function Get-PeOffset($Image,[long]$Rva) {
    if($Rva -ge 0 -and $Rva -lt $Image.Headers -and $Rva -lt $Image.Bytes.Length){return [int]$Rva}
    foreach($section in $Image.Sections){
        if($Rva -ge $section.Rva -and $Rva -lt ([long]$section.Rva+[Math]::Max($section.VirtualSize,$section.RawSize))){
            $offset=[long]$section.Raw+$Rva-$section.Rva
            if($offset -lt 0 -or $offset -ge $Image.Bytes.Length){throw "RVA out of file bounds: $($Image.Path)"}
            return [int]$offset
        }
    }
    throw "Unmapped RVA $Rva in $($Image.Path)"
}
function Get-PeString([byte[]]$Bytes,[int]$Offset) {
    $end=$Offset
    while($end -lt $Bytes.Length -and $Bytes[$end] -ne 0 -and $end-$Offset -lt 8192){$end++}
    if($Offset -lt 0 -or $end -ge $Bytes.Length -or $end-$Offset -ge 8192){throw 'Invalid PE string'}
    return [Text.Encoding]::ASCII.GetString($Bytes,$Offset,$end-$Offset)
}
function Get-PeImports($Image) {
    if(-not $Image.ImportRva){return}
    $descriptor=Get-PeOffset $Image $Image.ImportRva
    for($i=0;$i -lt 4096;$i++){
        $at=$descriptor+$i*20
        $original=[BitConverter]::ToUInt32($Image.Bytes,$at)
        $nameRva=[BitConverter]::ToUInt32($Image.Bytes,$at+12)
        $first=[BitConverter]::ToUInt32($Image.Bytes,$at+16)
        if(-not $original -and -not $nameRva -and -not $first){return}
        $dll=Get-PeString $Image.Bytes (Get-PeOffset $Image $nameRva)
        $tableRva=if($original){$original}else{$first}
        $table=Get-PeOffset $Image $tableRva
        $stride=if($Image.Is64){8}else{4}
        for($j=0;$j -lt 100000;$j++){
            $slot=$table+$j*$stride
            $value=if($Image.Is64){[BitConverter]::ToUInt64($Image.Bytes,$slot)}else{[BitConverter]::ToUInt32($Image.Bytes,$slot)}
            if(-not $value){break}
            if(($Image.Bytes[$slot+$stride-1] -band 128) -ne 0){
                [PSCustomObject]@{Dll=$dll;Name=$null;Ordinal=[int][BitConverter]::ToUInt16($Image.Bytes,$slot)}
            }else{
                $hintRva=[BitConverter]::ToUInt32($Image.Bytes,$slot)
                [PSCustomObject]@{Dll=$dll;Name=(Get-PeString $Image.Bytes ((Get-PeOffset $Image $hintRva)+2));Ordinal=$null}
            }
        }
    }
    throw 'Import descriptor limit exceeded'
}
function Get-PeExports($Image) {
    $names=New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
    if(-not $Image.ExportRva){return [PSCustomObject]@{Names=$names;Base=0;Count=0;Functions=0;Image=$Image}}
    $at=Get-PeOffset $Image $Image.ExportRva
    $base=[BitConverter]::ToUInt32($Image.Bytes,$at+16)
    $count=[BitConverter]::ToUInt32($Image.Bytes,$at+20)
    $named=[BitConverter]::ToUInt32($Image.Bytes,$at+24)
    if($named -gt 250000 -or $count -gt 250000){throw 'Export table limit exceeded'}
    $functions=if($count){Get-PeOffset $Image ([BitConverter]::ToUInt32($Image.Bytes,$at+28))}else{0}
    if($named){
        $table=Get-PeOffset $Image ([BitConverter]::ToUInt32($Image.Bytes,$at+32))
        for($i=0;$i -lt $named;$i++){
            $name=Get-PeString $Image.Bytes (Get-PeOffset $Image ([BitConverter]::ToUInt32($Image.Bytes,$table+$i*4)))
            [void]$names.Add($name)
        }
    }
    return [PSCustomObject]@{Names=$names;Base=$base;Count=$count;Functions=$functions;Image=$Image}
}

if(-not [Environment]::Is64BitProcess){throw 'Use 64-bit Windows PowerShell (C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe)'}
$hostRoot=[IO.Path]::GetFullPath($SysplorerHome)
$bin=Join-Path $hostRoot 'Bin64'
$exe=Join-Path $bin 'mworks.exe'
$plugin=if($PluginPath){[IO.Path]::GetFullPath($PluginPath)}else{Join-Path $bin 'addins\BatteryUdpControl\battery_udp_control.dll'}
if(-not(Test-Path -LiteralPath $exe) -or -not(Test-Path -LiteralPath $plugin)){throw 'Host executable or installed plugin file not found'}
$image=Get-PeImage $plugin
$modulePaths=@{};$hostProcesses=@();$loaded=@();$inspectionErrors=@()
foreach($process in @(Get-Process mworks -ErrorAction SilentlyContinue)){
    try{
        if($process.Path -ne $exe){continue}
        $hostProcesses+=$process.Id
        foreach($module in $process.Modules){
            $modulePaths[$module.ModuleName]=$module.FileName
            if($module.ModuleName -match '^(battery_udp_control|Qt[56]Core|mw_develop|mw_extension_system)\.dll$'){
                $loaded+=[PSCustomObject]@{Pid=$process.Id;Name=$module.ModuleName;Path=$module.FileName;Version=$module.FileVersionInfo.FileVersion}
            }
        }
    }catch{$inspectionErrors+=$_.Exception.Message}
}
function Find-Provider([string]$Name){
    if($modulePaths.ContainsKey($Name)){return $modulePaths[$Name]}
    foreach($directory in @($bin,(Split-Path -Parent $plugin),[Environment]::SystemDirectory)){
        $candidate=Join-Path $directory $Name
        if(Test-Path -LiteralPath $candidate){return $candidate}
    }
    return $null
}
$dependencies=@()
foreach($group in @(Get-PeImports $image | Group-Object Dll)){
    $name=$group.Name
    if($name -match '^(api-ms-|ext-ms-)'){
        $dependencies+=[PSCustomObject]@{Dll=$name;Status='OS_API_SET_NOT_INSPECTED';ImportCount=$group.Count};continue
    }
    $provider=Find-Provider $name
    if(-not $provider){
        $dependencies+=[PSCustomObject]@{Dll=$name;Status='DLL_NOT_FOUND_IN_CHECKED_LOCATIONS';ImportCount=$group.Count};continue
    }
    try{
        $providerImage=Get-PeImage $provider
        if($providerImage.Machine -ne $image.Machine){throw 'PE machine type does not match plugin'}
        $exports=Get-PeExports $providerImage
        $missing=@()
        foreach($entry in $group.Group){
            if($null -ne $entry.Ordinal){
                $index=$entry.Ordinal-$exports.Base
                if($index -lt 0 -or $index -ge $exports.Count -or [BitConverter]::ToUInt32($providerImage.Bytes,$exports.Functions+$index*4) -eq 0){$missing+='#'+$entry.Ordinal}
            }elseif(-not $exports.Names.Contains($entry.Name)){$missing+=$entry.Name}
        }
        $dependencies+=[PSCustomObject]@{Dll=$name;Path=$provider;Version=(Get-Item -LiteralPath $provider).VersionInfo.FileVersion;Status=$(if($missing.Count){'MISSING_EXPORTS'}else{'DIRECT_IMPORTS_FOUND'});ImportCount=$group.Count;MissingCount=$missing.Count;MissingExports=@($missing|Select-Object -First 40)}
    }catch{
        $dependencies+=[PSCustomObject]@{Dll=$name;Path=$provider;Status='INSPECTION_ERROR';Error=$_.Exception.Message}
    }
}
# Required exact runtime SDK lookups used by control_plugin.cpp. No calls are made.
$runtimeSpecs=ConvertFrom-Json @'
[
  {
    "dll": "modelica_post_processor.dll",
    "exports": [
      "?SetCommuStepSizeDenominator@MwModelSimConfig@@QEAAXH@Z",
      "?SetCommunicationStepSize@MwModelSimConfig@@QEAAXN@Z",
      "?SetSlowdownFactor@MwModelSimConfig@@QEAAXN@Z",
      "?SetStopTime@MwModelSimConfig@@QEAAXN@Z",
      "?SimConfig@MwModelSimInstance@@QEBAPEAVMwModelSimConfig@@XZ"
    ]
  },
  {
    "dll": "mw_develop.dll",
    "exports": [
      "?GetCore@MwWorkbench@@QEAAPEAVMwWorkbenchCore@@XZ",
      "?GetSimControl@MwSimInstPanel@@QEAAPEAVMwSimControl@@XZ",
      "?Instance@MwApp@@SAPEAV1@XZ"
    ]
  },
  {
    "dll": "mw_sim_inst.dll",
    "exports": [
      "?GetCurSimTime@MwSimControl@@QEAANXZ",
      "?GetSimData@MwSimControl@@QEBAPEAVMwSimData@@XZ",
      "?IsSimIdle@MwSimControl@@QEAA_NXZ",
      "?IsSimPaused@MwSimControl@@QEAA_NXZ",
      "?IsSimPausing@MwSimControl@@QEAA_NXZ",
      "?IsSimStarting@MwSimControl@@QEAA_NXZ",
      "?IsSimStopping@MwSimControl@@QEAA_NXZ"
    ]
  }
]
'@
$runtimeHostExports=@()
foreach($spec in $runtimeSpecs){
    $provider=Find-Provider $spec.dll
    if(-not $provider){$runtimeHostExports+=[PSCustomObject]@{Dll=$spec.dll;Status='DLL_NOT_FOUND';MissingExports=@($spec.exports)};continue}
    try{
        $exports=Get-PeExports (Get-PeImage $provider)
        $missing=@($spec.exports|Where-Object {-not $exports.Names.Contains($_)})
        $runtimeHostExports+=[PSCustomObject]@{Dll=$spec.dll;Status=$(if($missing.Count){'MISSING_RUNTIME_EXPORTS'}else{'RUNTIME_EXPORTS_FOUND'});MissingExports=$missing}
    }catch{$runtimeHostExports+=[PSCustomObject]@{Dll=$spec.dll;Status='INSPECTION_ERROR';Error=$_.Exception.Message}}
}
$pluginExports=Get-PeExports $image
$compatibility=[ordered]@{taskStatusSupported=$false;supportedTaskStatusExports=@();availableTaskStatusExports=@();nativePlotSupported=$false;availableNativePlotExports=@();inspectionErrors=@()}
try{
    $globalPath=Find-Provider 'mw_global.dll'
    if($globalPath){
        $globalExports=Get-PeExports (Get-PeImage $globalPath)
        $compatibility.availableTaskStatusExports=@($globalExports.Names|Where-Object {$_ -match '^\?(IsRunning|GetRunningThread)@MwTaskManager@@'}|Sort-Object)
        $compatibility.supportedTaskStatusExports=@(@('?IsRunning@MwTaskManager@@QEAA_NXZ','?IsRunning@MwTaskManager@@QEBA_NXZ','?GetRunningThread@MwTaskManager@@QEAAPEAVMwThread@@XZ','?GetRunningThread@MwTaskManager@@QEBAPEAVMwThread@@XZ')|Where-Object {$globalExports.Names.Contains($_)})
        $compatibility.taskStatusSupported=$compatibility.supportedTaskStatusExports.Count -gt 0
    }
    $developPath=Find-Provider 'mw_develop.dll'
    if($developPath){
        $developExports=Get-PeExports (Get-PeImage $developPath)
        $compatibility.availableNativePlotExports=@($developExports.Names|Where-Object {$_ -match '^\?CreatePlotWin@MwPlotWindowManager@@'}|Sort-Object)
        $compatibility.nativePlotSupported=$developExports.Names.Contains('?CreatePlotWin@MwPlotWindowManager@@QEAAPEAVMwSimPlotWindow@@_NH0@Z')
    }
}catch{$compatibility.inspectionErrors+= $_.Exception.Message}
$report=[ordered]@{
    schema=1;createdAt=(Get-Date).ToString('o');sysplorerHome=$hostRoot
    hostVersion=(Get-Item -LiteralPath $exe).VersionInfo.FileVersion
    pluginPath=$plugin;pluginSha256=(Get-FileHash -LiteralPath $plugin -Algorithm SHA256).Hash
    pluginMachine=('0x{0:X4}' -f $image.Machine)
    qtPluginEntryPointsPresent=($pluginExports.Names.Contains('qt_plugin_instance') -and $pluginExports.Names.Contains('qt_plugin_query_metadata'))
    hostPids=$hostProcesses;pluginLoaded=(@($loaded|Where-Object {$_.Name -eq 'battery_udp_control.dll'}).Count -gt 0)
    selectedLoadedModules=$loaded;processInspectionErrors=$inspectionErrors;directDependencies=$dependencies;hostCompatibility=$compatibility;runtimeHostExports=$runtimeHostExports
    scope='Read-only PE import and exact runtime symbol presence checks. No plugin execution. Does not prove recursive dependencies, plugin discovery, enablement or general ABI compatibility. OS API sets are not inspected. Credentials are not read.'
}
$destination=[IO.Path]::GetFullPath($OutputFile)
[IO.File]::WriteAllText($destination,($report|ConvertTo-Json -Depth 8),(New-Object Text.UTF8Encoding($false)))
Write-Output "Report: $destination"
Write-Output "Host version: $($report.hostVersion)"
Write-Output "Plugin loaded: $($report.pluginLoaded)"
$loaded|Format-List Name,Version,Path
[PSCustomObject]$compatibility|Format-List
$runtimeHostExports|Where-Object {$_.Status -ne 'RUNTIME_EXPORTS_FOUND'}|Format-List
$problems=@($dependencies|Where-Object {$_.Status -notin @('DIRECT_IMPORTS_FOUND','OS_API_SET_NOT_INSPECTED')})
if($problems.Count){$problems|Format-List Dll,Status,Path,Version,MissingCount,MissingExports,Error}
else{Write-Output 'No missing direct DLL imports found in checked locations. Plugin discovery/enablement and dynamic SDK ABI still need checking.'}
Write-Output 'The JSON report contains no access key or authentication token.'
