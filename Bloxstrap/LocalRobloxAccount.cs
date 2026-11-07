namespace Bloxstrap;

// Reads only the numeric identity in a bounded Player log tail. Never reads
// browser storage, credentials, cookies, launch tickets or process memory.
internal static class LocalRobloxAccount
{
    internal sealed record Detection(long Id, bool Running)
    {
        public string Description => Id == 0 ? "Conta não identificada — entre em uma experiência do Roblox"
            : Running ? $"Perfil detectado na sessão do Roblox • ID {Id}" : $"Última sessão identificada • ID {Id}";
    }

    internal static long Parse(string text)
    {
        long id = 0;
        foreach (string line in text.Split('\n'))
        {
            if (!line.Contains("[FLog::GameJoinLoadTime] Report game_join_loadtime:", StringComparison.Ordinal)) continue;
            var match = Regex.Match(line, @"(?:^|[,\s])userid:([0-9]+)(?=[,\s]|$)", RegexOptions.CultureInvariant);
            if (match.Success && long.TryParse(match.Groups[1].Value, out long found) && found > 0) id = found;
        }
        return id;
    }

    internal static Detection Detect()
    {
        try
        {
            var players = Process.GetProcessesByName("RobloxPlayerBeta");
            var starts = new List<DateTime>();
            foreach (var player in players)
                using (player)
                    if (player.SessionId == Process.GetCurrentProcess().SessionId) starts.Add(player.StartTime.ToUniversalTime());
            if (starts.Count > 1) return new(0, true);
            bool running = starts.Count == 1;
            var directory = new DirectoryInfo(Path.Combine(Paths.LocalAppData, "Roblox", "logs"));
            if (!directory.Exists) return new(0, running);
            var log = directory.EnumerateFiles("*.log")
                .Where(file => file.Name.Contains("Player", StringComparison.OrdinalIgnoreCase)
                    && file.LastWriteTimeUtc > DateTime.UtcNow.AddHours(-24)
                    && (!running || file.CreationTimeUtc >= starts[0].AddSeconds(-5)))
                .OrderByDescending(file => file.CreationTimeUtc).FirstOrDefault();
            if (log is null) return new(0, running);
            using var stream = new FileStream(log.FullName, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            stream.Seek(Math.Max(0, stream.Length - 2 * 1024 * 1024), SeekOrigin.Begin);
            using var reader = new StreamReader(stream);
            return new(Parse(reader.ReadToEnd()), running);
        }
        catch { return new(0, false); }
    }
}
