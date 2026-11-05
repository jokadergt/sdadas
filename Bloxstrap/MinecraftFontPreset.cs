namespace Bloxstrap;

// Uses the upstream CustomFont.ttf mod, which the bootstrapper maps into Roblox's
// font-family manifests before copying modifications into the installed version.
internal static class MinecraftFontPreset
{
    public static void ApplyOnce()
    {
        if (App.State.Prop.MinecraftFontApplied) return;

        Directory.CreateDirectory(Path.GetDirectoryName(Paths.CustomFont)!);
        string temporary = Paths.CustomFont + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            using (var source = Resource.GetStream("Fonts.Minecraft.ttf"))
            using (var target = File.Create(temporary))
                source.CopyTo(target);

            Filesystem.AssertReadOnly(Paths.CustomFont);
            File.Move(temporary, Paths.CustomFont, true);
            App.State.Prop.MinecraftFontApplied = true;
            App.State.Save();
        }
        finally
        {
            if (File.Exists(temporary)) File.Delete(temporary);
        }
    }
}
