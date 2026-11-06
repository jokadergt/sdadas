using System.Buffers.Binary;
using System.Windows.Media.Imaging;

namespace Bloxstrap;

// Small in-process adapter for the site's public search and Roblox PNG downloads.
// No browser runtime, listening port, API key, JavaScript execution, or bulk crawl.
internal sealed class SweezyCursorCatalog
{
    internal record Item(string Slug, string Name, bool Animated);
    internal record Assets(string Cursor, string Pointer);
    private readonly Dictionary<string, (DateTime Time, Item[] Items)> _searchCache = new();
    private static readonly HttpClient Client = CreateClient();
    private static HttpClient CreateClient()
    {
        var client = new HttpClient(new HttpClientHandler
        {
            AllowAutoRedirect = false, UseCookies = false,
            AutomaticDecompression = DecompressionMethods.GZip | DecompressionMethods.Deflate | DecompressionMethods.Brotli,
            MaxConnectionsPerServer = 2
        }) { Timeout = TimeSpan.FromSeconds(20) };
        client.DefaultRequestHeaders.UserAgent.ParseAdd("cayb-trap/2.11.4 (+https://github.com/jokadergt/sdadas)");
        return client;
    }

    private static bool Allowed(Uri uri) => uri.Scheme == "https" && uri.IsDefaultPort &&
        string.IsNullOrEmpty(uri.UserInfo) && (uri.Host == "sweezy-cursors.com" ||
        uri.Host == "cdn.sweezy-cursors.com" || uri.Host.EndsWith(".r2.cloudflarestorage.com", StringComparison.OrdinalIgnoreCase));

    private static async Task<byte[]> Read(Uri uri, int maximum)
    {
        try { return await ReadCore(uri, maximum); }
        catch (OperationCanceledException) { throw new TimeoutException("O Sweezy demorou para responder. Tente novamente."); }
        catch (HttpRequestException) { throw new IOException("Não foi possível conectar ao Sweezy. Verifique a conexão e tente novamente."); }
    }

    private static async Task<byte[]> ReadCore(Uri uri, int maximum)
    {
        for (int redirects = 0; redirects < 5; redirects++)
        {
            if (!Allowed(uri)) throw new InvalidDataException("O site retornou um endereço de download não permitido.");
            using var response = await Client.GetAsync(uri, HttpCompletionOption.ResponseHeadersRead);
            int status = (int)response.StatusCode;
            if (status is 301 or 302 or 303 or 307 or 308)
            {
                uri = response.Headers.Location is Uri next ? new Uri(uri, next) : throw new InvalidDataException("Redirecionamento inválido.");
                continue;
            }
            if (status is 403 or 429)
                throw new InvalidDataException("O Sweezy limitou o acesso. Tente mais tarde ou abra o resultado no site.");
            if (!response.IsSuccessStatusCode) throw new InvalidDataException($"O Sweezy respondeu HTTP {status}. Tente novamente mais tarde.");
            if (response.Content.Headers.ContentLength > maximum) throw new InvalidDataException("O arquivo excede o limite de download.");
            using var source = await response.Content.ReadAsStreamAsync();
            using var memory = new MemoryStream();
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(20));
            var buffer = new byte[16384];
            int count;
            while ((count = await source.ReadAsync(buffer.AsMemory(), timeout.Token)) > 0)
            {
                if (memory.Length + count > maximum) throw new InvalidDataException("O arquivo excede o limite de download.");
                memory.Write(buffer, 0, count);
            }
            return memory.ToArray();
        }
        throw new InvalidDataException("O site retornou muitos redirecionamentos.");
    }

    internal static Item[] ParseSearch(string html)
    {
        var items = new List<Item>();
        var seen = new HashSet<string>();
        var cards = Regex.Matches(html, "<article\\b[^>]*data-cursor-pack-payload=\"(?<json>[^\"]+)\"[^>]*>(?<body>.*?)</article>",
            RegexOptions.Singleline | RegexOptions.IgnoreCase, TimeSpan.FromSeconds(2));
        foreach (Match card in cards)
        {
            try
            {
                using var data = JsonDocument.Parse(WebUtility.HtmlDecode(card.Groups["json"].Value));
                var config = data.RootElement.GetProperty("cursorConfig");
                var link = Regex.Match(card.Groups["body"].Value, "href=\"/cursor/(?<slug>[a-z0-9-]{1,180})/\"", RegexOptions.IgnoreCase, TimeSpan.FromSeconds(1));
                string slug = link.Groups["slug"].Value;
                string name = config.GetProperty("name").GetString() ?? "Cursor sem nome";
                if (!link.Success || !seen.Add(slug)) continue;
                items.Add(new(slug, name.Length > 200 ? name[..200] : name,
                    config.TryGetProperty("isAnimated", out var animated) && animated.ValueKind == JsonValueKind.True));
                if (items.Count == 24) break;
            }
            catch (JsonException) { }
            catch (KeyNotFoundException) { }
        }
        return items.ToArray();
    }

    public async Task<Item[]> Search(string query)
    {
        query = query.Trim();
        if (query.Length is < 2 or > 80) throw new ArgumentException("Digite entre 2 e 80 caracteres para pesquisar.");
        string key = query.ToLowerInvariant();
        if (_searchCache.TryGetValue(key, out var cached) && DateTime.UtcNow - cached.Time < TimeSpan.FromMinutes(5)) return cached.Items;
        var html = Encoding.UTF8.GetString(await Read(new Uri("https://sweezy-cursors.com/search/?s=" + Uri.EscapeDataString(query)), 3_000_000));
        var items = ParseSearch(html);
        if (items.Length == 0 && !html.Contains("search_results_list", StringComparison.OrdinalIgnoreCase) && !html.Contains("No cursors", StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("Não foi possível ler os resultados do Sweezy. O site pode ter mudado ou limitado o acesso.");
        if (_searchCache.Count >= 8) _searchCache.Remove(_searchCache.OrderBy(x => x.Value.Time).First().Key);
        _searchCache[key] = (DateTime.UtcNow, items);
        return items;
    }

    internal static void ValidatePng(byte[] bytes)
    {
        byte[] signature = { 137, 80, 78, 71, 13, 10, 26, 10 };
        if (bytes.Length < 24 || !bytes.AsSpan(0, 8).SequenceEqual(signature) ||
            BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(16, 4)) is 0 or > 512 ||
            BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(20, 4)) is 0 or > 512)
            throw new InvalidDataException("O download não é um PNG de cursor válido (máximo de 512 × 512 pixels).");
        using var stream = new MemoryStream(bytes);
        var decoder = new PngBitmapDecoder(stream, BitmapCreateOptions.PreservePixelFormat, BitmapCacheOption.OnLoad);
        _ = decoder.Frames[0].PixelWidth;
    }

    public async Task<Assets> Preview(Item item)
    {
        string root = Path.Combine(Paths.Temp, "Sweezy");
        string directory = Path.Combine(root, item.Slug);
        Directory.CreateDirectory(directory);
        async Task<string> Image(string kind, string name)
        {
            string file = Path.Combine(directory, name + ".png");
            if (File.Exists(file) && DateTime.UtcNow - File.GetLastWriteTimeUtc(file) < TimeSpan.FromHours(24))
            {
                var cached = File.ReadAllBytes(file); ValidatePng(cached); return file;
            }
            var bytes = await Read(new Uri($"https://sweezy-cursors.com/cursor/{item.Slug}/downloads/{kind}/"), 2_000_000);
            ValidatePng(bytes);
            File.WriteAllBytes(file, bytes);
            return file;
        }
        var images = await Task.WhenAll(Image("roblox-cursor", "cursor"), Image("roblox-pointer", "pointer"));
        Directory.SetLastWriteTimeUtc(directory, DateTime.UtcNow);
        foreach (var old in new DirectoryInfo(root).EnumerateDirectories().OrderByDescending(d => d.LastWriteTimeUtc).Skip(16))
        {
            if (old.FullName == directory) continue;
            try { old.Delete(true); } catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { }
        }
        return new(images[0], images[1]);
    }

    private static string CursorDirectory => Path.Combine(Paths.Modifications, "content", "textures", "Cursors", "KeyboardMouse");
    public static void Install(Assets images)
    {
        var source = new[] { images.Cursor, images.Pointer };
        var names = new[] { "ArrowCursor.png", "ArrowFarCursor.png" };
        Directory.CreateDirectory(CursorDirectory);
        var original = names.Select(n => File.Exists(Path.Combine(CursorDirectory, n)) ? File.ReadAllBytes(Path.Combine(CursorDirectory, n)) : null).ToArray();
        try
        {
            for (int i = 0; i < 2; i++)
            {
                byte[] bytes = File.ReadAllBytes(source[i]); ValidatePng(bytes);
                string destination = Path.Combine(CursorDirectory, names[i]);
                string backup = Path.Combine(Paths.Base, "Backups", "Cursors", names[i]);
                if (original[i] is not null && !File.Exists(backup)) { Directory.CreateDirectory(Path.GetDirectoryName(backup)!); File.WriteAllBytes(backup, original[i]!); }
                Filesystem.AssertReadOnly(destination);
                File.WriteAllBytes(destination, bytes);
            }
        }
        catch
        {
            for (int i = 0; i < 2; i++)
            {
                string path = Path.Combine(CursorDirectory, names[i]);
                if (original[i] is null) File.Delete(path); else File.WriteAllBytes(path, original[i]!);
            }
            throw;
        }
    }

    public static void Reset()
    {
        foreach (string name in new[] { "ArrowCursor.png", "ArrowFarCursor.png" })
        {
            string path = Path.Combine(CursorDirectory, name);
            Filesystem.AssertReadOnly(path); File.Delete(path);
        }
    }
}
