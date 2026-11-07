namespace Bloxstrap.UI.ViewModels.Settings;

public enum AppCorner { Suave, Medio, Redondo, MuitoRedondo }
public enum AppTypeface { SegoeUI, Arial, Consolas, Minecraft }

public class AppPreferencesViewModel
{
    public string InstalledVersion
    {
        get => App.Settings.Prop.TargetVersionPath;
        set
        {
            value = value == "" ? "" : Path.GetFullPath(value);
            if (value != "" && !TargetVersionMods.Installed().Contains(value, StringComparer.OrdinalIgnoreCase))
                throw new ArgumentException("Selecione uma versão instalada da lista.");
            App.Settings.Prop.TargetVersionPath = value;
        }
    }
    public AppTypeface AppFont { get => App.Settings.Prop.AppFont; set => App.Settings.Prop.AppFont = value; }
    public AppCorner Corner { get => App.Settings.Prop.AppCorner; set => App.Settings.Prop.AppCorner = value; }
    public string ProfileUsername
    {
        get => App.Settings.Prop.ProfileUsername;
        set
        {
            if (value.Length > 20 || (value.Length > 0 && !Regex.IsMatch(value, "^[A-Za-z0-9_]+$")))
                throw new ArgumentException("Informe o nome de usuário Roblox (sem @), ou deixe vazio.");
            App.Settings.Prop.ProfileUsername = value;
        }
    }
    public bool ApplicationNotifications { get => App.Settings.Prop.ApplicationNotifications; set => App.Settings.Prop.ApplicationNotifications = value; }
}
