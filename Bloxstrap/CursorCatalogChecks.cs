using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace Bloxstrap;

// Offline integration check invoked by the Windows build; never touches Roblox.
internal static class CursorCatalogChecks
{
    public static void Run()
    {
        string asset = "{\"path\":\"https://cdn.sweezy-cursors.com/cursor_packs/test/image.png\",\"offsetX\":2,\"offsetY\":3}";
        string json = "{\"cursorConfig\":{\"name\":\"Test & cursor\",\"isAnimated\":true,\"cursor\":" + asset + ",\"pointer\":" + asset + "}}";
        string html = "<article data-cursor-pack-payload=\"" + WebUtility.HtmlEncode(json) +
            "\"><a href=\"/cursor/test-cursor/\">Test</a></article>";
        var results = SweezyCursorCatalog.ParseSearch(html);
        if (results.Length != 1 || results[0].Name != "Test & cursor" || !results[0].Animated || results[0].Slug != "test-cursor")
            throw new InvalidDataException("Catalog parser check failed.");
        if (SweezyCursorCatalog.ParseSearch(html.Replace("/cursor/test-cursor/", "/cursor/../../outside/")).Length != 0)
            throw new InvalidDataException("Unsafe cursor link was accepted.");
        try { SweezyCursorCatalog.ValidatePng(Encoding.UTF8.GetBytes("not a PNG")); throw new Exception("Invalid PNG was accepted."); }
        catch (InvalidDataException) { }

        string root = Path.Combine(Path.GetTempPath(), "cayb-cursor-check-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            Paths.Initialize(root);
            string image = Path.Combine(root, "test.png");
            var bitmap = BitmapSource.Create(64, 64, 96, 96, PixelFormats.Bgra32, null, Enumerable.Repeat((byte)255, 64 * 64 * 4).ToArray(), 64 * 4);
            var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(bitmap));
            using (var stream = File.Create(image)) encoder.Save(stream);
            var converted = SweezyCursorCatalog.RobloxPng(File.ReadAllBytes(image), 0, 0);
            using (var convertedStream = new MemoryStream(converted))
            {
                var decoded = new PngBitmapDecoder(convertedStream, BitmapCreateOptions.PreservePixelFormat, BitmapCacheOption.OnLoad).Frames[0];
                if (decoded.PixelWidth != 64 || decoded.PixelHeight != 64) throw new Exception("Roblox PNG dimensions are invalid.");
                var pixels = new byte[64 * 64 * 4]; decoded.CopyPixels(pixels, 64 * 4, 0);
                if (pixels[(40 * 64 + 40) * 4 + 3] == 0 || pixels[(8 * 64 + 8) * 4 + 3] != 0) throw new Exception("Cursor hotspot placement is invalid.");
            }
            SweezyCursorCatalog.Install(new(image, image));
            string directory = Path.Combine(Paths.Modifications, "content", "textures", "Cursors", "KeyboardMouse");
            if (!File.ReadAllBytes(image).SequenceEqual(File.ReadAllBytes(Path.Combine(directory, "ArrowCursor.png"))) ||
                !File.Exists(Path.Combine(directory, "ArrowFarCursor.png"))) throw new IOException("Cursor installation check failed.");
            SweezyCursorCatalog.Install(new(image, image));
            if (!File.Exists(Path.Combine(root, "Backups", "Cursors", "ArrowCursor.png"))) throw new IOException("Cursor backup check failed.");
            SweezyCursorCatalog.Reset();
            if (File.Exists(Path.Combine(directory, "ArrowCursor.png"))) throw new IOException("Cursor reset check failed.");
        }
        finally { Directory.Delete(root, true); }
    }
}
