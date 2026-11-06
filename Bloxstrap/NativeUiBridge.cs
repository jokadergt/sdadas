using System.Reflection;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using Bloxstrap.UI.ViewModels.Settings;
using Microsoft.Win32;

namespace Bloxstrap;

// Private, inherited stdio handles connect one native UI to one engine process.
// All view-model operations run on WPF's STA dispatcher, including legacy dialogs.
internal sealed class NativeUiBridge
{
    private readonly Dictionary<string, object> _models = new();
    private readonly Dictionary<string, string> _titles = new()
    {
        ["Integrations"] = "Integrações", ["Behaviour"] = "Comportamento",
        ["Mods"] = "Modificações", ["Cursors"] = "Cursores", ["FastFlags"] = "FastFlags",
        ["Appearance"] = "Aparência", ["Shortcuts"] = "Atalhos",
        ["Bloxstrap"] = "cayb trap"
    };
    private Dictionary<string, Dictionary<string, string>> _labels = new();
    private readonly SweezyCursorCatalog _catalog = new();
    private SweezyCursorCatalog.Item[] _cursorItems = Array.Empty<SweezyCursorCatalog.Item>();
    private SweezyCursorCatalog.Assets? _cursorPreview;
    private string _cursorQuery = "", _cursorSelected = "";
    private bool _installed;
    private bool _dirty;
    private string _notice = "";
    private string _launchResult = "";
    private readonly Dictionary<string, string?> _diskHashes = new();

    public static void Start()
    {
        var bridge = new NativeUiBridge();
        try { bridge.Initialize(); }
        catch (Exception ex) { bridge.Write(new { ok = false, error = ex.Message }); App.Current.Shutdown(1); return; }
        bridge.Write(bridge.Snapshot());
        _ = Task.Run(async () =>
        {
            using var input = new StreamReader(Console.OpenStandardInput(), new UTF8Encoding(false));
            try
            {
                while (await input.ReadLineAsync() is string line)
                {
                    try
                    {
                        using var request = JsonDocument.Parse(line);
                        var result = await await App.Current.Dispatcher.InvokeAsync(() => bridge.Handle(request.RootElement));
                        bridge.Write(result);
                    }
                    catch (Exception ex) { bridge.Write(new { ok = false, error = (ex.InnerException ?? ex).Message }); }
                }
            }
            finally { await App.Current.Dispatcher.InvokeAsync(() => App.Current.Shutdown()); }
        });
    }

    private void Write(object value)
    {
        using var output = new StreamWriter(Console.OpenStandardOutput(), new UTF8Encoding(false), leaveOpen: true);
        output.WriteLine(JsonSerializer.Serialize(value));
        output.Flush();
    }

    private void Initialize()
    {
        using var key = Registry.CurrentUser.OpenSubKey(App.UninstallKey);
        var location = key?.GetValue("InstallLocation") as string;
        _installed = location is not null && Directory.Exists(location);
        if (!_installed) return;
        Paths.Initialize(location!);
        // Refuse corrupt configuration; do not invoke JsonManager.Load's recovery writes.
        CheckJson<Settings>(App.Settings.FileLocation);
        CheckJson<Models.Persistable.State>(App.State.FileLocation);
        CheckJson<Dictionary<string, object>>(App.FastFlags.FileLocation);
        App.Settings.Load(false);
        App.State.Load(false);
        App.FastFlags.Load(false);
        MinecraftFontPreset.ApplyOnce();
        AllowedFastFlagPreset.ApplyOnce();
        RememberDisk();
        if (Locale.SupportedLocales.ContainsKey(App.Settings.Prop.Locale)) Locale.Set(App.Settings.Prop.Locale);
        _models["Integrations"] = new IntegrationsViewModel();
        _models["Behaviour"] = new BehaviourViewModel();
        _models["Mods"] = new ModsViewModel();
        _models["Cursors"] = new object();
        _models["FastFlags"] = new FastFlagsViewModel();
        _models["Appearance"] = new AppearanceViewModel(new Page());
        _models["Shortcuts"] = new ShortcutsViewModel();
        _models["Bloxstrap"] = new BloxstrapViewModel();
        using var labels = new StreamReader(Resource.GetStream("NativeUiLabels.json"));
        _labels = JsonSerializer.Deserialize<Dictionary<string, Dictionary<string, string>>>(labels.ReadToEnd())!;
    }

    private static void CheckJson<T>(string path)
    {
        if (File.Exists(path) && JsonSerializer.Deserialize<T>(File.ReadAllText(path)) is null)
            throw new InvalidDataException($"Arquivo inválido: {path}");
    }

    private static string EnumLabel(Type type, string name)
    {
        if (name == "Default") return type == typeof(Theme) ? "Seguir o Windows" : "Automático / padrão";
        return name switch
        {
            "Light" => "Claro", "Dark" => "Escuro", "Production" => "Produção",
            "Staging" => "Homologação", "Dev" => "Desenvolvimento", "DevPizza" => "Desenvolvimento (Pizza)",
            "DevMatt" => "Desenvolvimento (Matt)", "Local" => "Local",
            "Windows11" => "Windows 11", "Windows10" => "Windows 10", "Windows8" => "Windows 8",
            "From2006" => "Roblox 2006", "From2013" => "Roblox 2013",
            "x1" => "1×", "x2" => "2×", "x4" => "4×",
            "Level0" => "Nível 0", "Level1" => "Nível 1", "Level2" => "Nível 2", "Level3" => "Nível 3",
            "IconBloxstrap" => "Capivara", "IconCustom" => "Ícone personalizado",
            "IconBloxstrapClassic" => "Bloxstrap clássico", "CustomDialog" => "Personalizado",
            "ProgressDialog" => "Barra de progresso", "FluentDialog" => "Fluent",
            "ClassicFluentDialog" => "Fluent clássico", "FluentAeroDialog" => "Fluent Aero",
            "VistaDialog" => "Windows Vista", "LegacyDialog2008" => "Clássico 2008", "LegacyDialog2011" => "Clássico 2011",
            _ => name.StartsWith("Icon", StringComparison.Ordinal) ? "Roblox " + name[4..] : Regex.Replace(name, "([a-z])([A-Z])", "$1 $2")
        };
    }

    private string Label(string page, string path)
    {
        if (page == "Mods" && path == "ManageCustomFontCommand")
            return string.IsNullOrEmpty(((ModsViewModel)_models[page]).TextFontTask.NewState) ? "Escolher fonte personalizada" : "Remover fonte personalizada";
        if (_labels.TryGetValue(page, out var labels) && labels.TryGetValue(path, out var label)) return label;
        return Regex.Replace(path.Replace(".NewState", "").Replace("Command", "").Replace("Task", ""), "([a-z])([A-Z])", "$1 $2");
    }

    private static (object owner, PropertyInfo property) Resolve(object model, string path)
    {
        var parts = path.Split('.');
        object owner = model;
        for (int i = 0; i < parts.Length - 1; i++)
            owner = owner.GetType().GetProperty(parts[i])?.GetValue(owner) ?? throw new ArgumentException("Campo desconhecido");
        var property = owner.GetType().GetProperty(parts[^1]) ?? throw new ArgumentException("Campo desconhecido");
        return (owner, property);
    }

    private IEnumerable<object> Fields(string page, object model)
    {
        if (page == "Cursors") yield break;
        foreach (var property in model.GetType().GetProperties())
        {
            // Collections/selection indexes are represented by native JSON editors or selectors below.
            if (property.Name is "SelectedCustomIntegration" or "SelectedCustomIntegrationIndex" or
                "SelectedCustomThemeIndex" or "Icons" or "UpdateCheckingEnabled" or "ConfirmLaunches" or "TextFontTask") continue;
            object? value = property.GetValue(model);
            if (value is ICommand)
            {
                if (page == "Integrations") continue; // Full collection editor replaces list-specific commands.
                yield return new { path = property.Name, label = Label(page, property.Name), kind = "command" };
                continue;
            }
            var path = property.Name;
            var type = property.PropertyType;
            if (value is BaseTask)
            {
                var state = value.GetType().GetProperty("NewState")!;
                path += ".NewState";
                type = state.PropertyType;
                value = state.GetValue(value);
            }
            else if (!property.CanWrite) continue;
            bool enabled = !(page == "FastFlags" && App.Settings.Prop.LowGraphicsMode &&
                path is "UseFastFlagManager" or "SelectedMSAALevel" or "SelectedTextureQuality");
            if (type == typeof(bool))
            {
                yield return new { path, label = Label(page, path), kind = "bool", value, enabled };
                if (page == "FastFlags" && path == "LowGraphicsMode")
                    yield return new { path = "$graphicsInfo", label = "Reduz a qualidade das texturas, usa MSAA 1× e zera a distância da grama do Terrain. Ícones, avatares e VFX dos mapas não são removidos por este perfil. Aplique na próxima abertura do Roblox.", kind = "info" };
            }
            else if (type.IsEnum)
                yield return new { path, label = Label(page, path), kind = "enum", value = value?.ToString(), choices = Enum.GetNames(type), choiceLabels = Enum.GetNames(type).Select(name => EnumLabel(type, name)).ToArray(), enabled };
            else if (property.Name == "SelectedCustomTheme")
                yield return new { path, label = "Tema personalizado selecionado", kind = "enum", value = value ?? "", choices = new[] { "" }.Concat(((AppearanceViewModel)model).CustomThemes).ToArray() };
            else if (property.Name == "SelectedLanguage")
                yield return new { path, label = Label(page, path), kind = "enum", value, choices = AppearanceViewModel.Languages };
            else if (type == typeof(string))
                yield return new { path, label = Label(page, path), kind = "text", value = value ?? "" };
            else if (property.Name == "CustomIntegrations")
                yield return new { path, label = "Integrações personalizadas (JSON)", kind = "json", value };
        }
        if (page == "FastFlags")
            yield return new { path = "$flags", label = "Editor de FastFlags (JSON)", kind = "json", value = App.FastFlags.Prop };
        if (page == "Bloxstrap")
        {
            yield return new { path = "$about", label = "Sobre, créditos e licenças originais", kind = "command" };
            yield return new { path = "$uninstall", label = "Desinstalar cayb trap", kind = "command" };
        }
    }

    private object Snapshot() => new
    {
        ok = true, installed = _installed, dirty = _dirty || App.PendingSettingTasks.Count > 0,
        notice = _notice, launchResult = _launchResult,
        playerRunning = ExistingRoblox.IsRunning(LaunchMode.Player),
        studioRunning = ExistingRoblox.IsRunning(LaunchMode.Studio),
        cursorCatalog = new { query = _cursorQuery, selected = _cursorSelected,
            results = _cursorItems.Select(item => new { slug = item.Slug, name = item.Name, animated = item.Animated }).ToArray(),
            cursorPreview = _cursorPreview?.Cursor ?? "", pointerPreview = _cursorPreview?.Pointer ?? "",
            installedName = App.State.Prop.SweezyCursorName },
        version = App.Version, bubbleSounds = App.Settings.Prop.BubbleSounds, theme = _installed ? App.Settings.Prop.Theme.GetFinal().ToString() : "Dark",
        pages = _models.Select(pair => new { id = pair.Key, title = _titles[pair.Key], fields = Fields(pair.Key, pair.Value).ToArray() }).ToArray()
    };

    private void Apply(JsonElement changes)
    {
        // Resolve and type-check the entire batch before mutating any setting.
        var pending = new List<Action>();
        foreach (var change in changes.EnumerateArray())
        {
            string page = change.GetProperty("page").GetString()!;
            string path = change.GetProperty("path").GetString()!;
            var model = _models[page];
            var val = change.GetProperty("value");
            if (!Fields(page, model).Any(field => JsonSerializer.SerializeToElement(field).GetProperty("path").GetString() == path))
                throw new ArgumentException("Campo não editável");
            if (path == "$flags")
            {
                var flags = val.Deserialize<Dictionary<string, object>>() ?? throw new ArgumentException("FastFlags inválidas");
                foreach (var item in flags)
                {
                    if (item.Value is JsonElement e && e.ValueKind is not (JsonValueKind.String or JsonValueKind.Number or JsonValueKind.True or JsonValueKind.False))
                        throw new ArgumentException("FastFlags aceitam somente valores escalares");
                }
                pending.Add(() => App.FastFlags.Prop = flags);
                continue;
            }
            var (owner, property) = Resolve(model, path);
            var type = property.PropertyType;
            object? value;
            if (type.IsEnum)
            {
                value = Enum.Parse(type, val.GetString()!, false);
                if (!Enum.IsDefined(type, value)) throw new ArgumentException("Opção inválida");
            }
            else value = val.Deserialize(type);
            if (page == "Appearance" && path == "Theme")
                pending.Add(() => App.Settings.Prop.Theme = (Theme)value!); // Native window applies its own theme.
            else if (page == "Appearance" && path == "SelectedCustomTheme")
                pending.Add(() => { var vm = (AppearanceViewModel)model; vm.SelectedCustomTheme = string.IsNullOrEmpty((string?)value) ? null : (string?)value; vm.SelectedCustomThemeName = (string?)value ?? ""; });
            else pending.Add(() => property.SetValue(owner, value));
        }
        foreach (var action in pending) action();
        _dirty |= pending.Count > 0;
    }

    private async Task Save()
    {
        if (_diskHashes.Any(pair => pair.Value != DiskHash(pair.Key)))
            throw new IOException("A configuração foi alterada por outro processo. Feche e reabra antes de salvar.");
        foreach (var task in App.PendingSettingTasks.Values.ToArray())
        {
            if (!task.Changed) continue;
            if (task is EmojiModPresetTask emoji) await emoji.ExecuteAsync();
            else task.Execute();
            if (task.Changed) throw new IOException($"A tarefa {task.Name} não foi concluída.");
        }
        AllowedFastFlagPreset.Normalize();
        App.Settings.Save(); App.State.Save(); App.FastFlags.Save();
        Verify(App.Settings); Verify(App.State); Verify(App.FastFlags);
        RememberDisk();
        App.PendingSettingTasks.Clear();
        _dirty = false;
        FileCacheCleaner.StartIfEnabled();
        _notice = ExistingRoblox.IsRunning(LaunchMode.Player) || ExistingRoblox.IsRunning(LaunchMode.Studio)
            ? "Salvo • fonte/flags na próxima abertura do jogo" : "Configurações salvas";
    }

    private static string? DiskHash(string path) => File.Exists(path) ? MD5Hash.FromFile(path) : null;

    private void RememberDisk()
    {
        foreach (var path in new[] { App.Settings.FileLocation, App.State.FileLocation, App.FastFlags.FileLocation })
            _diskHashes[path] = DiskHash(path);
    }

    private static void Verify<T>(JsonManager<T> manager) where T : class, new()
    {
        var saved = JsonSerializer.Deserialize<T>(File.ReadAllText(manager.FileLocation));
        if (JsonSerializer.Serialize(saved) != JsonSerializer.Serialize(manager.Prop))
            throw new IOException($"Não foi possível confirmar a gravação de {manager.FileName}.");
    }

    private async Task<object> Handle(JsonElement request)
    {
        _notice = "";
        _launchResult = "";
        string op = request.GetProperty("op").GetString()!;
        if (op == "snapshot") return Snapshot();
        if (!_installed) throw new InvalidOperationException("Instale o motor original antes de editar configurações.");
        if (request.TryGetProperty("changes", out var changes)) Apply(changes);
        switch (op)
        {
            case "cursor.search":
                _cursorQuery = request.GetProperty("query").GetString() ?? "";
                _cursorItems = await _catalog.Search(_cursorQuery);
                _cursorSelected = ""; _cursorPreview = null;
                _notice = _cursorItems.Length == 0 ? "Nenhum cursor encontrado. Tente outro termo." : $"{_cursorItems.Length} resultados do Sweezy — selecione uma prévia";
                break;
            case "cursor.preview":
            case "cursor.install":
                string slug = request.GetProperty("slug").GetString() ?? "";
                var item = _cursorItems.FirstOrDefault(item => item.Slug == slug) ?? throw new ArgumentException("Pesquise e selecione um resultado válido antes de continuar.");
                _cursorPreview = await _catalog.Preview(item);
                _cursorSelected = item.Slug;
                if (op == "cursor.install")
                {
                    await Save();
                    SweezyCursorCatalog.Install(_cursorPreview);
                    App.State.Prop.SweezyCursorSlug = item.Slug; App.State.Prop.SweezyCursorName = item.Name;
                    App.State.Save(); Verify(App.State); RememberDisk();
                    _models["Mods"] = new ModsViewModel();
                    _notice = "Cursor preparado — será aplicado na próxima abertura do Roblox";
                }
                else _notice = "Prévia estática pronta. Clique em Usar no Roblox para aplicar.";
                break;
            case "cursor.reset":
                await Save(); SweezyCursorCatalog.Reset();
                App.State.Prop.SweezyCursorSlug = ""; App.State.Prop.SweezyCursorName = "";
                App.State.Save(); Verify(App.State); RememberDisk(); _models["Mods"] = new ModsViewModel();
                _notice = "Cursor padrão preparado para a próxima abertura do Roblox";
                break;
            case "apply": break;
            case "save": await Save(); break;
            case "launch":
                var modeName = request.GetProperty("mode").GetString();
                LaunchMode mode = modeName switch
                {
                    "Player" => LaunchMode.Player,
                    "Studio" => LaunchMode.Studio,
                    _ => throw new ArgumentException("Tipo de lançamento inválido")
                };
                await Save(); // The native launch buttons also commit pending edits.
                if (ExistingRoblox.TryActivate(mode))
                {
                    _launchResult = "existing";
                    _notice = "Instância já aberta • fonte/flags no próximo início";
                }
                else
                {
                    Process.Start(new ProcessStartInfo(Paths.Process, mode == LaunchMode.Player ? "-player" : "-studio") { UseShellExecute = false })?.Dispose();
                    _launchResult = "started";
                    _notice = "Configurações salvas • iniciando o jogo";
                }
                break;
            case "command":
                string page = request.GetProperty("page").GetString()!;
                string name = request.GetProperty("path").GetString()!;
                if (page == "Bloxstrap" && name == "$about")
                {
                    new UI.Elements.About.MainWindow().ShowDialog();
                    break;
                }
                if (name == "OpenFastFlagEditorCommand") break; // Native JSON editor is present on this page.
                var model = _models[page];
                if (model.GetType().GetProperty(name)?.GetValue(model) is not ICommand command)
                    throw new ArgumentException("Comando desconhecido");
                if (!command.CanExecute(null)) throw new InvalidOperationException("Comando indisponível");
                command.Execute(null);
                _dirty = true;
                break;
            default: throw new ArgumentException("Operação desconhecida");
        }
        return Snapshot();
    }
}
