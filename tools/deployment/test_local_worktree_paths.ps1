$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'LocalWorktree.Common.ps1')
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$folder=Get-WorktreeFile $root ('logs\local-worktree\paths-'+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($folder)|Out-Null
function Assert-That($condition,[string]$message){if(-not $condition){throw $message}}
$rejected=$false
try{[void](Get-WorktreeFile $root '..\outside.txt')}catch{$rejected=$true}
Assert-That $rejected 'Path escape was accepted'
$external=Join-Path $folder 'other'
[IO.Directory]::CreateDirectory($external)|Out-Null
$link=Join-Path $folder 'link'
New-Item -ItemType Junction -Path $link -Target $external|Out-Null
$rejected=$false
try{[void](Get-WorktreeFile $root ($link.Substring($root.Length+1)+'\test.txt'))}catch{$rejected=$true}
Assert-That $rejected 'Junction output was accepted'
$client=Join-Path $folder 'client'
[IO.Directory]::CreateDirectory($client)|Out-Null
$ini=Join-Path $client 'sysplorer.ini'
[IO.File]::WriteAllText($ini,"[Connection]`r`nMode=local`r`nAddress=127.0.0.1`r`nPort=39001`r`n[Host]`r`nHome=C:/old`r`nTimeoutMs=240000`r`n[Replay]`r`nKeepHistory=true`r`n",(New-Object Text.UTF8Encoding $true))
Write-LocalHostConfiguration $client 'Z:\private-host'
$text=[IO.File]::ReadAllText($ini,[Text.Encoding]::UTF8)
foreach($expected in @('Mode=local','Address=127.0.0.1','Port=39001','TimeoutMs=240000','KeepHistory=true','Home=Z:/private-host')){
    Assert-That $text.Contains($expected) ('Configuration was lost: '+$expected)
}
Write-LocalHostConfiguration $client 'Y:\private-host'
$text=[IO.File]::ReadAllText($ini,[Text.Encoding]::UTF8)
Assert-That ($text.Contains('Home=Y:/private-host') -and -not $text.Contains('Home=Z:') -and [regex]::Matches($text,'\[Host\]').Count -eq 1) 'Host configuration was not updated cleanly'
$first=$null;$second=$null
try{
    $first=Connect-WorktreeDrive $folder
    $again=Connect-WorktreeDrive $folder $first.letter
    Assert-That ($again.letter -eq $first.letter -and -not $again.created) 'Existing matching mapping was not reused'
    $second=Connect-WorktreeDrive $external $first.letter
    Assert-That ($second.letter -ne $first.letter) 'Occupied mapping was replaced'
    Assert-That ((Get-WorktreeDriveTarget $first.letter) -eq $folder) 'Foreign mapping was modified'
    $rejected=$false
    try{Remove-WorktreeDrive $external $first.letter}catch{$rejected=$true}
    Assert-That $rejected 'Foreign mapping removal was accepted'
}finally{
    if($second -and $second.created){Remove-WorktreeDrive $external $second.letter}
    if($first -and $first.created){Remove-WorktreeDrive $folder $first.letter}
}
Save-WorktreeJson ([ordered]@{passed=$true;checks=@('path-boundary','junction-refusal','configuration-preserved','mapping-reuse','occupied-mapping-preserved','foreign-removal-refused','mapping-cleanup')}) (Join-Path $folder 'result.json')
Write-Output "PASS: $folder\result.json"
