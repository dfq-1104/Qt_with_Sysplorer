param([string]$RunName = (Get-Date -Format 'yyyyMMdd_HHmmss'))
$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path "$PSScriptRoot\..\..").Path
$sdkBin = 'C:\Program Files\MWORKS\Sysplorer SDK 2026a(vs2017)\bin\win_msvc2017x64'
# The SDK's generated MSVC response files do not reliably support Chinese paths.
$scratch = 'C:\Users\TR\.codex\visualizations\2026\11\03\01a1a14e-d533-7a73-8c71-2fa02f587ae3\sdk_probe'
$runRoot = Join-Path $scratch $RunName
if (Test-Path -LiteralPath $runRoot) { throw 'RunName must identify a fresh directory.' }
New-Item -ItemType Directory -Path $runRoot | Out-Null
$model = Join-Path $runRoot 'BatterySim.mo'
Get-Content -LiteralPath "$workspace\model\BatterySim.mo" -Raw -Encoding utf8 |
    Set-Content -LiteralPath $model -Encoding utf8
$env:PATH = "$sdkBin\Release;" + $env:PATH
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_QPA_PLATFORM_PLUGIN_PATH = "$sdkBin\Release\platforms"
$cases = @(@{Name='a'; SOC='0.3'; Current='1.0'}, @{Name='b'; SOC='0.7'; Current='2.0'})
foreach ($case in $cases) {
    $out = Join-Path $runRoot $case.Name
    $log = Join-Path $workspace "logs\${RunName}_$($case.Name).log"
    & "$PSScriptRoot\release\sdk_probe.exe" $model $out $case.SOC $case.Current vc > $log 2>&1
    $code = $LASTEXITCODE
    Get-Content -LiteralPath $log | Select-String '^(PARAM |COMPILE=|STOP |RESULT |FAIL|CORE compiler|CLEANUP license)'
    if ($code -ne 0) { throw "Case $($case.Name) failed: exit=$code; log=$log" }
}
Write-Output "VERIFIED: $runRoot"
