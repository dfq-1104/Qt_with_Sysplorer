param([Parameter(Mandatory=$true)][string]$OutputDirectory,[string]$VerificationFile='')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
. (Join-Path $PSScriptRoot 'LocalWorktree.Common.ps1')
$output=Get-WorktreeFile $root $OutputDirectory
if(Test-Path -LiteralPath $output){throw 'Output already exists; use a new current-worktree directory'}
$context=& (Join-Path $PSScriptRoot 'Start-LocalWorktree.ps1') -EnvironmentOnly
$client=Join-Path $output 'BatteryConsole-Client-win64'
[IO.Directory]::CreateDirectory($output)|Out-Null
Copy-Item -LiteralPath $context.client -Destination $client -Recurse
if(-not(Test-Path -LiteralPath (Join-Path $client 'local-worktree.json'))){throw 'Prepared client has no worktree binding'}
$binding=Get-Content -LiteralPath (Join-Path $client 'local-worktree.json') -Raw -Encoding UTF8|ConvertFrom-Json
if($binding.worktreeRoot -ne $root){throw 'Prepared client points to another worktree'}
foreach($doc in @('启动与连接纠错说明_20261108.md')){
    Copy-Item -LiteralPath (Join-Path $root ('docs\'+$doc)) -Destination $client
}
$state=Get-Content -LiteralPath (Join-Path $root '.local-sysplorer\state.json') -Raw -Encoding UTF8|ConvertFrom-Json
$verification=$null
if($VerificationFile){
    $verificationPath=Get-WorktreeFile $root $VerificationFile
    $verification=Get-Content -LiteralPath $verificationPath -Raw -Encoding UTF8|ConvertFrom-Json
    if($verification.clientSha256 -ne $state.clientSha256){throw 'Verification belongs to another client build'}
}
$files=@(Get-ChildItem -LiteralPath $client -Recurse -File|Where-Object Name -ne 'manifest.json'|Sort-Object FullName|ForEach-Object {
    $relative=$_.FullName.Substring($client.Length+1).Replace('\','/')
    if($relative -match '(^|/)(access-key\.txt|remote\.ini|RemoteConnection\.ini|deployment\.json)$'){throw 'Private runtime data in client package'}
    [ordered]@{path=$relative;bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()}
})
[ordered]@{architecture='win64';deployment='current-worktree-bound';worktreeRoot=$root;clientSha256=$state.clientSha256;modelHash=$state.modelHash;verification=$verification;files=$files}|
    ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $client 'manifest.json') -Encoding UTF8
$zip=Join-Path $output 'BatteryConsole-Client-current-worktree.zip'
Compress-Archive -LiteralPath $client -DestinationPath $zip -CompressionLevel Optimal
Write-Output $zip
