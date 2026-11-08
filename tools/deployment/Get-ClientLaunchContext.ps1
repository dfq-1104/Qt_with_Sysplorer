param([Parameter(Mandatory=$true)][int]$ProcessId,[Parameter(Mandatory=$true)][string]$OutputFile)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$destination=[IO.Path]::GetFullPath($OutputFile)
if(-not $destination.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Diagnostic output must stay in this worktree'}
$client=Get-Process -Id $ProcessId
if($client.ProcessName -ne 'battery_console_ui' -or -not $client.Path.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Only this worktree client may be inspected'}
if(-not [Environment]::Is64BitProcess){throw 'Use x64 PowerShell for the x64 client'}
if(-not ('BatteryLaunchProbe.Native' -as [type])){
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Security.Principal;
namespace BatteryLaunchProbe {
 public static class Native {
  [StructLayout(LayoutKind.Sequential)] struct Basic {public IntPtr Reserved, Peb, R1, R2, Pid, Parent;}
  [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr OpenProcess(uint access,bool inherit,int pid);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
  [DllImport("ntdll.dll")] static extern int NtQueryInformationProcess(IntPtr h,int kind,out Basic data,int size,out int returned);
  [DllImport("kernel32.dll",SetLastError=true)] static extern bool ReadProcessMemory(IntPtr h,IntPtr address,byte[] data,IntPtr size,out IntPtr read);
  [DllImport("advapi32.dll",SetLastError=true)] static extern bool OpenProcessToken(IntPtr h,uint access,out IntPtr token);
  [DllImport("advapi32.dll",SetLastError=true)] static extern bool GetTokenInformation(IntPtr token,int kind,IntPtr buffer,int size,out int returned);
  [DllImport("advapi32.dll")] static extern IntPtr GetSidSubAuthorityCount(IntPtr sid);
  [DllImport("advapi32.dll")] static extern IntPtr GetSidSubAuthority(IntPtr sid,uint index);
  static byte[] Read(IntPtr h,long at,int count){var b=new byte[count];IntPtr got;if(!ReadProcessMemory(h,new IntPtr(at),b,new IntPtr(count),out got)||got.ToInt64()!=count)throw new Exception("Read-only process metadata unavailable");return b;}
  static string UString(IntPtr h,long at){var b=Read(h,at,16);int n=BitConverter.ToUInt16(b,0);if(n==0)return "";if(n>32766)throw new Exception("Invalid metadata length");return Encoding.Unicode.GetString(Read(h,BitConverter.ToInt64(b,8),n));}
  static int TokenInt(IntPtr t,int kind){var p=Marshal.AllocHGlobal(4);try{int n;if(!GetTokenInformation(t,kind,p,4,out n))return -1;return Marshal.ReadInt32(p);}finally{Marshal.FreeHGlobal(p);}}
  public static Dictionary<string,object> Inspect(int pid){
   var o=new Dictionary<string,object>();var h=OpenProcess(0x1010,false,pid);if(h==IntPtr.Zero)throw new Exception("OpenProcess read denied");
   try{
    Basic b;int n;if(NtQueryInformationProcess(h,0,out b,Marshal.SizeOf(typeof(Basic)),out n)!=0)throw new Exception("Process metadata query failed");
    o["parentPid"]=b.Parent.ToInt64();
    var pp=BitConverter.ToInt64(Read(h,b.Peb.ToInt64()+0x20,8),0);
    o["workingDirectory"]=UString(h,pp+0x38);o["imagePath"]=UString(h,pp+0x60);
    var args=UString(h,pp+0x70);var diagnosticFlags=new List<string>();foreach(var flag in new[]{"--verify-custom","--check-model","--smoke-test","--verify-local-connection","--verify-remote-connection"})if(args.Contains(flag))diagnosticFlags.Add(flag);o["diagnosticFlags"]=diagnosticFlags;
    // Never return the full command line/environment, credentials or tokens.
    var allow=new HashSet<string>(new[]{"PATH","SYSPLORER_HOME","BATTERY_MODEL_FILE","BATTERY_TEST_CONFIG_ROOT","BATTERY_SIM_WORKSPACE","BATTERY_DISCOVERY_DIR","BATTERY_LOCAL_CONFIG","BATTERY_WORKTREE_ROOT","BATTERY_CONNECTION_MODE","BATTERY_MODEL_DIAGNOSTICS","QT_PLUGIN_PATH","QT_QPA_PLATFORM_PLUGIN_PATH","QT_QPA_PLATFORM","PYTHONHOME","PYTHONPATH"},StringComparer.OrdinalIgnoreCase);
    var env=new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);long address=BitConverter.ToInt64(Read(h,pp+0x80,8),0);var bytes=new List<byte>();
    for(int offset=0;offset<131072;offset+=256){bytes.AddRange(Read(h,address+offset,256));var text=Encoding.Unicode.GetString(bytes.ToArray());int end=text.IndexOf("\0\0",StringComparison.Ordinal);if(end<0)continue;foreach(var line in text.Substring(0,end).Split('\0')){int equals=line.IndexOf('=');if(equals>0&&allow.Contains(line.Substring(0,equals)))env[line.Substring(0,equals)]=line.Substring(equals+1);}break;}
    o["environment"]=env;
    IntPtr token;if(OpenProcessToken(h,8,out token)){try{
     o["elevationType"]=TokenInt(token,18);o["elevated"]=TokenInt(token,20)==1;
     using(var identity=new WindowsIdentity(token))o["sameUserAsObserver"]=identity.User==WindowsIdentity.GetCurrent().User;
     GetTokenInformation(token,25,IntPtr.Zero,0,out n);var label=Marshal.AllocHGlobal(n);try{if(GetTokenInformation(token,25,label,n,out n)){var sid=Marshal.ReadIntPtr(label);uint count=Marshal.ReadByte(GetSidSubAuthorityCount(sid));o["integrityRid"]=Marshal.ReadInt32(GetSidSubAuthority(sid,count-1));}}finally{Marshal.FreeHGlobal(label);}
    }finally{CloseHandle(token);}}
    return o;
   }finally{CloseHandle(h);}
  }
 }
}
'@
}
$context=[BatteryLaunchProbe.Native]::Inspect($ProcessId)
$context['schema']=1;$context['pid']=$ProcessId;$context['exe']=$client.Path;$context['exeSha256']=(Get-FileHash -LiteralPath $client.Path).Hash
$context['started']=$client.StartTime.ToString('o');$context['sessionId']=$client.SessionId
$parent=Get-Process -Id ([int]$context.parentPid) -ErrorAction SilentlyContinue
$context['parent']=if($parent -and $parent.StartTime -le $client.StartTime){@{name=$parent.ProcessName;path=$parent.Path;alive=$true}}else{@{alive=$false}}
$context['modules']=@(foreach($module in $client.Modules){if($module.ModuleName -match '^(Qt5(Core|Gui|Widgets|Network)|qwindows|TyEnc64|intefsctrl64)\.dll$'){@{name=$module.ModuleName;path=$module.FileName;version=$module.FileVersionInfo.FileVersion;sha256=(Get-FileHash -LiteralPath $module.FileName).Hash}}})
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination))|Out-Null
[IO.File]::WriteAllText($destination,($context|ConvertTo-Json -Depth 7),(New-Object Text.UTF8Encoding($true)))
Write-Output "Launch context saved: $destination (allowlisted fields only; no credentials)"
