using System.ComponentModel;
using System.Runtime.InteropServices;

namespace Bloxstrap;

internal static class ExistingRoblox
{
    public static bool IsRunning(LaunchMode mode) => Find(mode, false);
    public static bool TryActivate(LaunchMode mode) => Find(mode, true);

    private static bool Find(LaunchMode mode, bool activate)
    {
        string? name = mode switch
        {
            LaunchMode.Player => App.RobloxPlayerAppName,
            LaunchMode.Studio => App.RobloxStudioAppName,
            _ => null
        };
        if (name is null) return false;
        using var current = Process.GetCurrentProcess();
        var processes = Process.GetProcessesByName(name);
        bool found = false;
        try
        {
            foreach (var process in processes)
            {
                try
                {
                    if (process.SessionId != current.SessionId) continue;
                    found = true; // A starting/minimized process may not yet expose a window.
                    if (!activate) return true;
                    IntPtr window = process.MainWindowHandle;
                    if (window == IntPtr.Zero) continue;
                    if (IsIconic(window)) ShowWindowAsync(window, 9); // SW_RESTORE; preserve maximized windows.
                    SetForegroundWindow(window);
                    return true;
                }
                catch (Exception ex) when (ex is InvalidOperationException or Win32Exception)
                {
                    // The process can exit between enumeration and the window query.
                }
            }
            return found;
        }
        finally { foreach (var process in processes) process.Dispose(); }
    }

    [DllImport("user32.dll")] private static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] private static extern bool ShowWindowAsync(IntPtr window, int command);
    [DllImport("user32.dll")] private static extern bool IsIconic(IntPtr window);
}
