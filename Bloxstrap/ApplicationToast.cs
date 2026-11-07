using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

namespace Bloxstrap;

internal static class ApplicationToast
{
    private static Window? _window;
    private static DateTime _last;
    private static string _cachedUser = "", _cachedName = "";
    private static BitmapSource? _cachedAvatar;

    public static async Task Show(string message)
    {
        try
        {
            if ((DateTime.UtcNow - _last).TotalSeconds < 3) return;
            _last = DateTime.UtcNow;
            var detected = LocalRobloxAccount.Detect();
            string username = detected.Id.ToString(CultureInfo.InvariantCulture);
            string name = detected.Id == 0 ? "cayb trap • conta não identificada" : "Roblox • ID " + username;
            BitmapSource? avatar = null;
            if (detected.Id > 0)
            {
                if (_cachedUser == username) { name = _cachedName; avatar = _cachedAvatar; }
                else
                {
                    try
                    {
                        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(5), MaxResponseContentBufferSize = 2 * 1024 * 1024 };
                        long id = detected.Id;
                        using var data = JsonDocument.Parse(await client.GetStringAsync($"https://users.roblox.com/v1/users/{id}"));
                        var user = data.RootElement;
                        name = user.GetProperty("displayName").GetString() + " (@" + user.GetProperty("name").GetString() + ")";
                        using var thumbs = JsonDocument.Parse(await client.GetStringAsync($"https://thumbnails.roblox.com/v1/users/avatar-headshot?userIds={id}&size=150x150&format=Png&isCircular=false"));
                        string? url = thumbs.RootElement.GetProperty("data")[0].GetProperty("imageUrl").GetString();
                        if (Uri.TryCreate(url, UriKind.Absolute, out var uri) && uri.Scheme == "https" && (uri.Host == "rbxcdn.com" || uri.Host.EndsWith(".rbxcdn.com", StringComparison.OrdinalIgnoreCase)))
                        {
                            byte[] bytes = await client.GetByteArrayAsync(uri);
                            using var stream = new MemoryStream(bytes);
                            var image = new BitmapImage(); image.BeginInit(); image.CacheOption = BitmapCacheOption.OnLoad; image.DecodePixelWidth = 64;
                            image.StreamSource = stream; image.EndInit(); image.Freeze(); avatar = image;
                        }
                        _cachedUser = username; _cachedName = name; _cachedAvatar = avatar;
                    }
                    catch { name = "Roblox • ID " + username + " • perfil indisponível"; }
                }
            }
            if (detected.Id > 0 && !detected.Running) name += " • última sessão";
            _window?.Close();
            var stack = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(16) };
            if (avatar != null) stack.Children.Add(new Image { Source = avatar, Width = 48, Height = 48, Margin = new Thickness(0, 0, 12, 0) });
            var text = new StackPanel { Width = 290 };
            text.Children.Add(new TextBlock { Text = name, Foreground = Brushes.White, FontSize = 15, FontWeight = FontWeights.SemiBold, TextTrimming = TextTrimming.CharacterEllipsis });
            text.Children.Add(new TextBlock { Text = message, Foreground = Brushes.LightGray, FontSize = 12, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 6, 0, 0) });
            stack.Children.Add(text);
            var window = new Window { Width = 400, SizeToContent = SizeToContent.Height, WindowStyle = WindowStyle.None,
                AllowsTransparency = true, Background = Brushes.Transparent, ShowActivated = false, ShowInTaskbar = false, Topmost = true,
                Content = new Border { Background = Brushes.Black, CornerRadius = new CornerRadius(18), Child = stack } };
            window.Loaded += (_, _) => { var area = SystemParameters.WorkArea; window.Left = area.Right - window.ActualWidth - 18; window.Top = area.Bottom - window.ActualHeight - 18; };
            _window = window; window.Show();
            var timer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(5) };
            timer.Tick += (_, _) => { timer.Stop(); window.Close(); if (_window == window) _window = null; };
            timer.Start();
        }
        catch (Exception ex) { App.Logger.WriteLine("ApplicationToast", ex.Message); }
    }
}
