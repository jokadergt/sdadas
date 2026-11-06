namespace Bloxstrap;

// Only adjusts published local flags. It does not delete Roblox/game assets.
internal static class PerformanceGraphics
{
    private static readonly Dictionary<string, string> Values = new()
    {
        ["DFFlagTextureQualityOverrideEnabled"] = "True",
        ["DFIntTextureQualityOverride"] = "0",
        ["FIntDebugForceMSAASamples"] = "1",
        ["FIntFRMMinGrassDistance"] = "0",
        ["FIntFRMMaxGrassDistance"] = "0"
    };

    public static void SetEnabled(bool enabled)
    {
        if (App.Settings.Prop.LowGraphicsMode == enabled) return;
        App.Settings.Prop.LowGraphicsMode = enabled;
        if (enabled) Apply();
        else
        {
            if (App.State.Prop.PerformanceOriginalFlags is { } original)
            {
                foreach (var entry in original)
                    if (entry.Value is null) App.FastFlags.Prop.Remove(entry.Key);
                    else App.FastFlags.Prop[entry.Key] = entry.Value;
            }
            if (App.State.Prop.PerformanceOriginalManager is bool manager) App.Settings.Prop.UseFastFlagManager = manager;
            App.State.Prop.PerformanceOriginalFlags = null;
            App.State.Prop.PerformanceOriginalManager = null;
        }
    }

    public static bool Apply()
    {
        if (!App.Settings.Prop.LowGraphicsMode) return false;
        bool changed = false;
        if (App.State.Prop.PerformanceOriginalFlags is null)
        {
            App.State.Prop.PerformanceOriginalFlags = Values.Keys.ToDictionary(key => key,
                key => App.FastFlags.Prop.TryGetValue(key, out var value) ? value?.ToString() : null);
            App.State.Prop.PerformanceOriginalManager = App.Settings.Prop.UseFastFlagManager;
            changed = true;
        }
        foreach (var entry in Values)
        {
            if (!App.FastFlags.Prop.TryGetValue(entry.Key, out var current) || current?.ToString() != entry.Value)
            {
                App.FastFlags.Prop[entry.Key] = entry.Value;
                changed = true;
            }
        }
        if (!App.Settings.Prop.UseFastFlagManager) { App.Settings.Prop.UseFastFlagManager = true; changed = true; }
        return changed;
    }
}
