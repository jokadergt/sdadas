namespace Bloxstrap;

internal static class AllowedFastFlagPreset
{
    // Public Roblox list checked 2026-11-05. The client/channel remains authoritative.
    // https://devforum.roblox.com/t/3966569
    private static readonly Lazy<Dictionary<string, string>> Preset = new(() =>
    {
        using var reader = new StreamReader(Resource.GetStream("AllowedFastFlags.json"));
        return JsonSerializer.Deserialize<Dictionary<string, string>>(reader.ReadToEnd())!;
    });

    public static void Normalize()
    {
        if (!App.Settings.Prop.AutomaticAllowedFastFlags) return;
        foreach (var key in App.FastFlags.Prop.Keys.ToArray())
            if (!Preset.Value.ContainsKey(key)) App.FastFlags.Prop.Remove(key);
        foreach (var entry in Preset.Value)
            if (!App.FastFlags.Prop.ContainsKey(entry.Key)) App.FastFlags.Prop[entry.Key] = entry.Value;

        // Multiple preferred graphics backends must never be enabled together.
        var backends = new[] { "FFlagDebugGraphicsPreferD3D11", "FFlagDebugGraphicsPreferVulkan", "FFlagDebugGraphicsPreferOpenGL" };
        bool selected = false;
        foreach (string key in backends)
        {
            if (!string.Equals(App.FastFlags.Prop[key]?.ToString(), "True", StringComparison.OrdinalIgnoreCase)) continue;
            if (selected) App.FastFlags.Prop[key] = "False";
            selected = true;
        }
    }

    public static void ApplyOnce()
    {
        if (!App.Settings.Prop.AutomaticAllowedFastFlags || App.State.Prop.AllowedFastFlagsPresetVersion >= 1) return;
        if (File.Exists(App.FastFlags.FileLocation))
        {
            string backup = Path.Combine(Paths.Base, "Backups", "ClientAppSettings.before-allowed-preset.json");
            Directory.CreateDirectory(Path.GetDirectoryName(backup)!);
            if (!File.Exists(backup)) File.Copy(App.FastFlags.FileLocation, backup);
        }
        // Use the complete explicit preset for its first application.
        App.FastFlags.Prop = Preset.Value.ToDictionary(entry => entry.Key, entry => (object)entry.Value);
        App.Settings.Prop.UseFastFlagManager = true;
        App.FastFlags.Save();
        if (!File.Exists(App.FastFlags.FileLocation)) throw new IOException("Não foi possível gravar o preset de FastFlags.");
        App.State.Prop.AllowedFastFlagsPresetVersion = 1;
        App.State.Save();
        App.Settings.Save();
    }
}
