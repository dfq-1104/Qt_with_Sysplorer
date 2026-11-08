# Set only a protected DACL. Set-Acl may also request SACL privilege on some
# PowerShell/.NET versions; storing a credential must not require that privilege.
if(-not ('BatteryConsole.PrivateFile' -as [type])){
    Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
namespace BatteryConsole {
    public static class PrivateFile {
        [DllImport("advapi32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
        public static extern bool SetFileSecurity(string path, uint information, byte[] descriptor);
    }
}
'@
}
function Set-PrivateFile([string]$Path,[string]$Sid){
    $sddl='D:P'
    foreach($principal in @($Sid,'S-1-5-18','S-1-5-32-544')|Select-Object -Unique){$sddl+='(A;;FA;;;'+$principal+')'}
    $descriptor=New-Object Security.AccessControl.RawSecurityDescriptor($sddl)
    $bytes=New-Object byte[] $descriptor.BinaryLength;$descriptor.GetBinaryForm($bytes,0)
    if(-not [BatteryConsole.PrivateFile]::SetFileSecurity([IO.Path]::GetFullPath($Path),[uint32]2147483652,$bytes)){
        throw (New-Object ComponentModel.Win32Exception([Runtime.InteropServices.Marshal]::GetLastWin32Error()))
    }
}
