param([int]$TargetPid, [ValidateSet('inspect','resize','click','key')][string]$Action = 'inspect',
    [int]$X = 0, [int]$Y = 0)
$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class TaaWindow {
    public delegate bool Callback(IntPtr h, IntPtr p);
    [DllImport("user32.dll")] public static extern bool EnumWindows(Callback c, IntPtr p);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out Rect r);
    [DllImport("user32.dll")]
    public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int w, int z, uint f);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out Point p);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref Point p);
    public struct Point { public int x, y; }
    public struct Rect { public int left, top, right, bottom; }
    public static IntPtr Find(uint target) {
        IntPtr result = IntPtr.Zero;
        int largest = 0;
        EnumWindows((h,p) => { uint owner; GetWindowThreadProcessId(h,out owner);
            Rect rect; GetClientRect(h,out rect);
            int area = rect.right * rect.bottom;
            if(owner == target && area > largest) { result=h; largest=area; } return true; },IntPtr.Zero);
        return result;
    }
}
'@
$handle = [TaaWindow]::Find($TargetPid)
if ($handle -eq [IntPtr]::Zero) { throw 'Editor window not found' }
if ($Action -eq 'resize') {
    if (-not [TaaWindow]::SetWindowPos($handle, [IntPtr]::Zero, 0, 0, $X, $Y, 0x16)) { throw 'Resize failed' }
}
if ($Action -eq 'click') {
    $original_cursor = New-Object TaaWindow+Point
    [void][TaaWindow]::GetCursorPos([ref]$original_cursor)
    $screen_cursor = New-Object TaaWindow+Point
    $screen_cursor.x = $X; $screen_cursor.y = $Y
    [void][TaaWindow]::ClientToScreen($handle, [ref]$screen_cursor)
    [void][TaaWindow]::SetCursorPos($screen_cursor.x, $screen_cursor.y)
    try {
    $coordinates = [IntPtr](($Y -shl 16) -bor ($X -band 0xffff))
    [void][TaaWindow]::SendMessage($handle, 0x200, [IntPtr]::Zero, $coordinates)
    [void][TaaWindow]::SendMessage($handle, 0x201, [IntPtr]1, $coordinates)
    Start-Sleep -Milliseconds 100
    [void][TaaWindow]::SendMessage($handle, 0x202, [IntPtr]::Zero, $coordinates)
    } finally { [void][TaaWindow]::SetCursorPos($original_cursor.x, $original_cursor.y) }
}
if ($Action -eq 'key') {
    [void][TaaWindow]::SendMessage($handle, 0x100, [IntPtr]$X, [IntPtr]1)
    [void][TaaWindow]::SendMessage($handle, 0x101, [IntPtr]$X, [IntPtr]0xc0000001L)
}
$rect = New-Object TaaWindow+Rect
[void][TaaWindow]::GetClientRect($handle, [ref]$rect)
@{handle=$handle.ToInt64(); width=$rect.right; height=$rect.bottom; action=$Action; x=$X; y=$Y} |
    ConvertTo-Json -Compress
