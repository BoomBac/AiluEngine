param([Parameter(Mandatory)][int]$TargetPid,
    [ValidateSet('minimize', 'restore', 'close')][string]$Action)
$ErrorActionPreference = 'Stop'
$requested_action = $Action
. "$PSScriptRoot/TaaM8WindowControl.ps1" -TargetPid $TargetPid -Action inspect | Out-Null
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class StreamlineWindow {
    [DllImport("user32.dll")] public static extern bool ShowWindowAsync(IntPtr handle, int command);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr handle);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr handle, uint message, IntPtr w, IntPtr l);
}
'@
$handle = [TaaWindow]::Find($TargetPid)
if ($handle -eq [IntPtr]::Zero) { throw 'Owned Editor window not found' }
$result = switch ($requested_action) {
    'minimize' { [StreamlineWindow]::ShowWindowAsync($handle, 6) }
    'restore' {
        [StreamlineWindow]::ShowWindowAsync($handle, 9) | Out-Null
        [StreamlineWindow]::SetForegroundWindow($handle)
    }
    'close' { [StreamlineWindow]::PostMessage($handle, 0x10, [IntPtr]::Zero, [IntPtr]::Zero) }
}
@{action=$requested_action; handle=$handle.ToInt64(); result=$result} | ConvertTo-Json -Compress
