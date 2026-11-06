using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace Bloxstrap;

// Offline integration check invoked by the Windows build; never touches Roblox.
internal static class CursorCatalogChecks
{
    public static void Run()
    {
        string json = "{\"cursorConfig\":{\"name\":\"Test & cursor\",\"isAnimated\":true}}";
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
            var bitmap = BitmapSource.Create(64, 64, 96, 96, PixelFormats.Bgra32, null, new byte[64 * 64 * 4], 64 * 4);
            var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(bitmap));
            using (var stream = File.Create(image)) encoder.Save(stream);
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
