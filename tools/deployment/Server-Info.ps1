param([string]$SysplorerHome='',[switch]$NonInteractive,[string]$StateDirectory='')
$ErrorActionPreference='Stop'
if(-not $StateDirectory){$StateDirectory=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'BatteryConsole\server'}
if(-not $SysplorerHome){$SysplorerHome=([IO.File]::ReadAllText((Join-Path $StateDirectory 'last-server.json'))|ConvertFrom-Json).sysplorerHome}
$config=Join-Path $SysplorerHome 'Bin64\addins\BatteryUdpControl\remote.ini'
$text=[IO.File]::ReadAllText($config)
function Value([string]$Name){return [regex]::Match($text,'(?m)^\s*'+$Name+'\s*=\s*([^\r\n]*)').Groups[1].Value.Trim()}
$key=Value 'AccessKey';if($key.Length -lt 32){throw 'Server access credential is missing; run Setup-Server.cmd again.'}
$private=[IO.Path]::GetFullPath($StateDirectory)
[IO.Directory]::CreateDirectory($private)|Out-Null
$digest=[Security.Cryptography.SHA256]::Create()
try{$id=[BitConverter]::ToString($digest.ComputeHash([Text.Encoding]::UTF8.GetBytes([IO.Path]::GetFullPath($SysplorerHome).ToLowerInvariant()))).Replace('-','').Substring(0,12)}finally{$digest.Dispose()}
$keyFile=Join-Path $private ($id+'-access-key.txt')
if(-not(Test-Path -LiteralPath $keyFile)){[IO.File]::WriteAllText($keyFile,'')}
. (Join-Path $PSScriptRoot 'PrivateFile.Common.ps1')
Set-PrivateFile $keyFile ([Security.Principal.WindowsIdentity]::GetCurrent().User.Value)
[IO.File]::WriteAllText($keyFile,$key,(New-Object Text.UTF8Encoding($false)))
$addresses=@([Net.NetworkInformation.NetworkInterface]::GetAllNetworkInterfaces()|Where-Object {$PSItem.OperationalStatus -eq 'Up'}|ForEach-Object {$PSItem.GetIPProperties().UnicastAddresses}|Where-Object {$PSItem.Address.AddressFamily -eq 'InterNetwork' -and -not [Net.IPAddress]::IsLoopback($PSItem.Address) -and -not $PSItem.Address.ToString().StartsWith('169.254.')}|ForEach-Object {$PSItem.Address.ToString()}|Sort-Object -Unique)
$receipt=[ordered]@{sysplorerHome=$SysplorerHome;workspace=(Value 'Workspace');port=[int](Value 'Port');keyFile=$keyFile;ipv4=$addresses;configuredAt=(Get-Date).ToString('o')}
$receipt|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $private 'last-server.json') -Encoding UTF8
Write-Output ('Server IPv4 candidates: '+($addresses -join ', '))
Write-Output ('TCP port: '+$receipt.port)
Write-Output ('Private credential file: '+$keyFile)
Write-Output 'Configuration complete. Start Sysplorer with its ordinary desktop icon, wait for initialization, then double-click Check-Server.cmd.'
if(-not $NonInteractive){
    Add-Type -AssemblyName System.Windows.Forms
    $form=New-Object Windows.Forms.Form;$form.Text='服务器配置完成';$form.Width=760;$form.Height=360;$form.StartPosition='CenterScreen'
    $label=New-Object Windows.Forms.TextBox;$label.Multiline=$true;$label.ReadOnly=$true;$label.Left=15;$label.Top=15;$label.Width=710;$label.Height=230
    $label.Text="配置已完成，尚未验证仿真引擎。`r`n服务器 IPv4：$($addresses -join ' / ')`r`nTCP 端口：$($receipt.port)`r`n凭据文件：$keyFile`r`n`r`n1. 通过原桌面图标启动 Sysplorer，等主窗口初始化完成。`r`n2. 双击本包 Check-Server.cmd。显示「可以连接」后再连接 Qt。`r`n3. 在 Qt 连接设置中填写服务器网卡 IPv4、端口及本凭据。`r`n多个网卡时选择客户端可达的服务器地址，不能填写默认网关。"
    $copy=New-Object Windows.Forms.Button;$copy.Text='复制访问凭据';$copy.Left=20;$copy.Top=255;$copy.Width=180
    $copy.Add_Click({[Windows.Forms.Clipboard]::SetText($key);[Windows.Forms.MessageBox]::Show('已复制，请粘贴到客户端连接设置。请勿发送到日志或公开聊天。')|Out-Null})
    $done=New-Object Windows.Forms.Button;$done.Text='关闭';$done.Left=600;$done.Top=255;$done.DialogResult='OK'
    $form.Controls.AddRange(@($label,$copy,$done));[void]$form.ShowDialog();$form.Dispose()
}
$key=$null
