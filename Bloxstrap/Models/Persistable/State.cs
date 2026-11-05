namespace Bloxstrap.Models.Persistable
{
    public class State
    {
        public bool PromptWebView2Install { get; set; } = true;

        // Records one-time application; removing the font in Mods will stay respected.
        public int AllowedFastFlagsPresetVersion { get; set; } = 0;

        public bool MinecraftFontApplied { get; set; } = false;

        public bool ForceReinstall { get; set; } = false;

        public WindowState SettingsWindow { get; set; } = new();
    }
}
