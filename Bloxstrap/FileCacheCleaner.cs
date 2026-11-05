using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Security.Principal;
using Microsoft.Win32;

namespace Bloxstrap;

// Optional documented Windows file-cache flush. This does not purge the standby
// list, trim game working sets, change cache size limits, or promise a speedup.
internal static class FileCacheCleaner
{
    private const string MutexName = "Local\\CaybTrapFileCacheCleaner";
    private static bool GameRunning()
    {
        bool found = false;
        foreach (string name in new[] { App.RobloxPlayerAppName, App.RobloxStudioAppName })
        {
            var processes = Process.GetProcessesByName(name);
            found |= processes.Length != 0;
            foreach (var process in processes) process.Dispose();
        }
        return found;
    }

    public static void StartIfEnabled()
    {
        if (!App.Settings.Prop.BackgroundFileCacheCleaner || !GameRunning()) return;
        try
        {
            if (Mutex.TryOpenExisting(MutexName, out var running)) { running!.Dispose(); return; }
            Process.Start(new ProcessStartInfo(Paths.Process, "-cachecleaner") { UseShellExecute = true, Verb = "runas" })?.Dispose();
        }
        catch (Win32Exception ex) when (ex.NativeErrorCode == 1223)
        {
            Frontend.ShowMessageBox("A limpeza de cache não iniciou: a autorização de administrador foi cancelada.");
        }
        catch (Exception ex) { App.Logger.WriteException("FileCacheCleaner::Start", ex); }
    }

    public static async void Run()
    {
        using var singleton = new Mutex(false, MutexName, out bool created);
        if (!created) { App.Current.Shutdown(); return; }
        try
        {
            using var key = Registry.CurrentUser.OpenSubKey(App.UninstallKey);
            if (key?.GetValue("InstallLocation") is not string location || !Directory.Exists(location)) return;
            string settingsPath = Path.Combine(location, "Settings.json");
            EnablePrivilege();
            DateTime lastFlush = DateTime.UtcNow, lastGameSeen = DateTime.UtcNow;
            int readFailures = 0;
            while (true)
            {
                await Task.Delay(TimeSpan.FromSeconds(15));
                Settings? settings;
                try { settings = JsonSerializer.Deserialize<Settings>(File.ReadAllText(settingsPath)); readFailures = 0; }
                catch (Exception ex) when (ex is IOException or JsonException) { if (++readFailures >= 4) break; continue; } // A settings write may briefly hold the file.
                if (settings?.BackgroundFileCacheCleaner != true) break;
                DateTime now = DateTime.UtcNow;
                if (!GameRunning())
                {
                    if (now - lastGameSeen > TimeSpan.FromSeconds(60)) break;
                    continue;
                }
                lastGameSeen = now;
                var memory = new MemoryStatus { Length = (uint)Marshal.SizeOf<MemoryStatus>() };
                if (GlobalMemoryStatusEx(ref memory) && memory.Load >= 85 && now - lastFlush >= TimeSpan.FromMinutes(5))
                {
                    // -1/-1 flushes the cache; flags=0 preserves current limit policies.
                    if (!SetSystemFileCacheSize(new UIntPtr(ulong.MaxValue), new UIntPtr(ulong.MaxValue), 0))
                        throw new Win32Exception(Marshal.GetLastWin32Error());
                    lastFlush = now;
                }
            }
        }
        catch (Exception ex)
        {
            System.Windows.MessageBox.Show("A limpeza automática de cache foi interrompida: " + ex.Message, "cayb trap");
        }
        finally { App.Current.Shutdown(); }
    }

    private static void EnablePrivilege()
    {
        using var identity = WindowsIdentity.GetCurrent(TokenAccessLevels.Query | TokenAccessLevels.AdjustPrivileges);
        if (!LookupPrivilegeValue(null, "SeIncreaseQuotaPrivilege", out var luid)) throw new Win32Exception(Marshal.GetLastWin32Error());
        var privilege = new TokenPrivileges { Count = 1, Luid = luid, Attributes = 2 };
        if (!AdjustTokenPrivileges(identity.Token, false, ref privilege, 0, IntPtr.Zero, IntPtr.Zero) || Marshal.GetLastWin32Error() != 0)
            throw new Win32Exception(Marshal.GetLastWin32Error());
    }

    [StructLayout(LayoutKind.Sequential)] private struct Luid { public uint Low; public int High; }
    [StructLayout(LayoutKind.Sequential)] private struct TokenPrivileges { public uint Count; public Luid Luid; public uint Attributes; }
    [StructLayout(LayoutKind.Sequential)] private struct MemoryStatus
    {
        public uint Length, Load;
        public ulong TotalPhysical, AvailablePhysical, TotalPageFile, AvailablePageFile, TotalVirtual, AvailableVirtual, AvailableExtendedVirtual;
    }
    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool LookupPrivilegeValue(string? system, string name, out Luid luid);
    [DllImport("advapi32.dll", SetLastError = true)]
    private static extern bool AdjustTokenPrivileges(IntPtr token, bool disableAll, ref TokenPrivileges state, uint length, IntPtr previous, IntPtr returned);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool GlobalMemoryStatusEx(ref MemoryStatus status);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool SetSystemFileCacheSize(UIntPtr minimum, UIntPtr maximum, uint flags);
}
