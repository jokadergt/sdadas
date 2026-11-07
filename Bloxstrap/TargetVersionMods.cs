namespace Bloxstrap;

// Applies prepared file overrides to an existing installation, without downloading
// or launching an obsolete client. Backups belong to this version only.
internal static class TargetVersionMods
{
    internal const string Version = "version-f0ea5da9412f4e35";

    public static string Apply()
    {
        var roots = new[] { Paths.Versions, Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Roblox", "Versions") };
        var targets = roots.Select(root => Path.Combine(root, Version))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .Where(path => File.Exists(Path.Combine(path, "RobloxPlayerBeta.exe"))).ToArray();
        if (targets.Length == 0)
            return $"Salvo • {Version} não encontrada nas pastas de versões do cayb trap e Roblox";
        if (ExistingRoblox.IsRunning(LaunchMode.Player))
            return $"Salvo • feche o Roblox e clique em Salvar para aplicar em {Version}";
        foreach (var target in targets) ApplyTo(target);
        return $"Aplicado em {Version} • abra novamente essa versão do Roblox";
    }

    internal static void ApplyTo(string target)
    {
        if (!File.Exists(Path.Combine(target, "RobloxPlayerBeta.exe")))
            throw new DirectoryNotFoundException($"Instalação Roblox inválida: {target}");
        // Separate backups for stock and launcher installations of the same GUID.
        string key = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(Encoding.UTF8.GetBytes(Path.GetFullPath(target).ToUpperInvariant())));
        string backup = Path.Combine(Paths.Base, "Backups", Version, key);
        string manifest = Path.Combine(backup, "manifest.json");
        var previous = File.Exists(manifest)
            ? JsonSerializer.Deserialize<Dictionary<string, bool>>(File.ReadAllText(manifest))! : new Dictionary<string, bool>();
        var desired = new Dictionary<string, byte[]>(StringComparer.OrdinalIgnoreCase);
        if (Directory.Exists(Paths.Modifications))
            foreach (string file in Directory.GetFiles(Paths.Modifications, "*", SearchOption.AllDirectories))
            {
                string relative = Path.GetRelativePath(Paths.Modifications, file);
                if (relative.Equals("README.txt", StringComparison.OrdinalIgnoreCase) || relative.EndsWith(".lock")) continue;
                if (relative.Equals(@"ClientSettings\ClientAppSettings.json", StringComparison.OrdinalIgnoreCase) && !App.Settings.Prop.UseFastFlagManager) continue;
                desired[relative] = File.ReadAllBytes(file);
            }
        if (File.Exists(Paths.CustomFont))
        {
            string families = Path.Combine(target, "content", "fonts", "families");
            if (Directory.Exists(families))
                foreach (string file in Directory.GetFiles(families, "*.json"))
                {
                    var family = JsonSerializer.Deserialize<FontFamily>(File.ReadAllText(file));
                    if (family is null) continue;
                    foreach (var face in family.Faces) face.AssetId = "rbxasset://fonts/CustomFont.ttf";
                    desired[Path.GetRelativePath(target, file)] = JsonSerializer.SerializeToUtf8Bytes(family);
                }
        }
        foreach (var entry in desired)
        {
            string destination = SafePath(target, entry.Key);
            if (!previous.ContainsKey(entry.Key))
            {
                bool existed = File.Exists(destination);
                if (existed)
                {
                    string original = SafePath(Path.Combine(backup, "original"), entry.Key);
                    Directory.CreateDirectory(Path.GetDirectoryName(original)!);
                    File.Copy(destination, original, true);
                }
                previous[entry.Key] = existed;
                Directory.CreateDirectory(backup);
                File.WriteAllText(manifest, JsonSerializer.Serialize(previous));
            }
            Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
            File.WriteAllBytes(destination, entry.Value);
        }
        foreach (string relative in previous.Keys.Except(desired.Keys, StringComparer.OrdinalIgnoreCase).ToArray())
        {
            string destination = SafePath(target, relative);
            if (previous[relative]) File.Copy(SafePath(Path.Combine(backup, "original"), relative), destination, true);
            else File.Delete(destination);
            previous.Remove(relative);
        }
        Directory.CreateDirectory(backup);
        File.WriteAllText(manifest, JsonSerializer.Serialize(previous));
    }

    private static string SafePath(string root, string relative)
    {
        string prefix = Path.GetFullPath(root) + Path.DirectorySeparatorChar;
        string path = Path.GetFullPath(Path.Combine(root, relative));
        if (!path.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) throw new IOException("Caminho de mod inválido.");
        return path;
    }
}
