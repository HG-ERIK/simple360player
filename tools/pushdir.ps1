# Uploads a folder (recursively) to the console through xbdm, creating folders as needed.
#   .\tools\pushdir.ps1 build\god\* hdd:\Content\0000000000000000\4D580360\00007000
param([Parameter(Mandatory)] [string]$Local, [Parameter(Mandatory)] [string]$Remote)

if ([Environment]::Is64BitProcess) {
    & "$env:WINDIR\SysWOW64\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -Local $Local -Remote $Remote
    exit $LASTEXITCODE
}

$ErrorActionPreference = 'Stop'
$bin = "$([Environment]::GetEnvironmentVariable('XEDK', 'Machine'))\bin\win32"
Add-Type -TypeDefinition @"
using System; using System.Runtime.InteropServices;
public static class XbdmDir {
    [DllImport(@"$bin\xbdm.dll", CharSet = CharSet.Ansi)] public static extern int DmSetXboxName(string name);
    [DllImport(@"$bin\xbdm.dll", CharSet = CharSet.Ansi)] public static extern int DmSendFile(string local, string remote);
    [DllImport(@"$bin\xbdm.dll", CharSet = CharSet.Ansi)] public static extern int DmMkdir(string remote);
}
"@
[void][XbdmDir]::DmSetXboxName((Get-ItemProperty 'HKCU:\Software\Microsoft\XenonSDK').XboxName)

# Make every parent folder of the destination (DmMkdir fails harmlessly if it exists).
$parts = $Remote.TrimEnd('\').Split('\')
for ($i = 1; $i -lt $parts.Count; $i++) { [void][XbdmDir]::DmMkdir(($parts[0..$i] -join '\')) }

function Send($item, $dest) {
    if ($item.PSIsContainer) {
        [void][XbdmDir]::DmMkdir($dest)
        foreach ($c in Get-ChildItem -LiteralPath $item.FullName) { Send $c "$dest\$($c.Name)" }
    } else {
        $hr = [XbdmDir]::DmSendFile($item.FullName, $dest)
        if ($hr -lt 0) { throw ("Upload {0} failed: 0x{1:X8}" -f $dest, $hr) }
        Write-Host "Uploaded $dest"
    }
}
foreach ($item in Get-ChildItem -Path $Local) { Send $item "$($Remote.TrimEnd('\'))\$($item.Name)" }
