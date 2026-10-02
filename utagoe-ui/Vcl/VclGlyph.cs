using System.Drawing.Imaging;
using System.Reflection;

namespace Utagoe.Vcl;

internal static class VclGlyph
{
    private static readonly Dictionary<string, Bitmap> Sheets = new();
    private static readonly Dictionary<(string, Rectangle), Bitmap> Pieces = new();

    private static readonly string[] ButtonRoles = { "check", "x", "question", "folder", "play", "settings", "info", "close" };
    private static readonly string[] ButtonThemes = { "army", "ice", "teal", "white", "wine" };
    private static readonly string[] MediaButtons = { "MPPLAY", "MPPAUSE", "MPSTOP", "MPPREV" };
    private static readonly (int Enabled, int Disabled)[] MediaWidths = { (17, 17), (13, 12), (17, 17), (20, 20) };

    private static Bitmap Sheet(string name)
    {
        if (Sheets.TryGetValue(name, out var cached)) return cached;
        using var s = Assembly.GetExecutingAssembly().GetManifestResourceStream($"Utagoe.Resources.{name}.bmp")
                      ?? throw new FileNotFoundException($"embedded bitmap '{name}' is missing");
        using var r = new BinaryReader(s);
        byte[] all = r.ReadBytes((int)s.Length);
        int offset = BitConverter.ToInt32(all, 10);
        int width = BitConverter.ToInt32(all, 18), height = BitConverter.ToInt32(all, 22);
        bool bottomUp = height > 0;
        height = Math.Abs(height);
        var bmp = new Bitmap(width, height, PixelFormat.Format32bppArgb);
        var data = bmp.LockBits(new Rectangle(0, 0, width, height), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
        for (int y = 0; y < height; y++)
        {
            int row = bottomUp ? height - 1 - y : y;
            System.Runtime.InteropServices.Marshal.Copy(all, offset + row * width * 4, data.Scan0 + y * data.Stride, width * 4);
        }
        bmp.UnlockBits(data);
        return Sheets[name] = bmp;
    }

    private static Bitmap Piece(string sheet, Rectangle area)
    {
        if (Pieces.TryGetValue((sheet, area), out var cached)) return cached;
        return Pieces[(sheet, area)] = Sheet(sheet).Clone(area, PixelFormat.Format32bppArgb);
    }

    private static int ThemeIndex(string theme)
    {
        int i = Array.IndexOf(App.AppIcons.Names, theme);
        return i < 0 ? 0 : i;
    }

    public static Bitmap AppIconArt(string theme) => Piece("appicons", new Rectangle(90 * ThemeIndex(theme), 0, 90, 90));

    public static Bitmap ThemeLogo() => Piece("logos", new Rectangle(0, 83 * ThemeIndex(App.Theme.Current.Name), 248, 83));

    public static (Bitmap Enabled, Bitmap Disabled) Button(string role)
    {
        string theme = App.Theme.Current.Name switch { "standard" => "wine", "silver" => "white", var t => t };
        int row = Math.Max(0, Array.IndexOf(ButtonThemes, theme));
        int col = Array.IndexOf(ButtonRoles, role);
        if (col < 0) throw new ArgumentException($"unknown button glyph '{role}'");
        bool large = role is "check" or "x" or "question";
        int w = large ? 18 : 16, h = large ? 18 : 16;
        return (Piece("buttons", new Rectangle(36 * col, 18 * row, w, h)),
                Piece("buttons", new Rectangle(36 * col + w, 18 * row, w, h)));
    }

    public static (Bitmap Enabled, Bitmap Disabled) Media(string name)
    {
        int i = Array.IndexOf(MediaButtons, name);
        if (i < 0) throw new ArgumentException($"unknown media glyph '{name}'");
        int y = 18 * ButtonThemes.Length;
        return (Piece("buttons", new Rectangle(40 * i, y, MediaWidths[i].Enabled, 16)),
                Piece("buttons", new Rectangle(40 * i + 20, y, MediaWidths[i].Disabled, 16)));
    }

    public static Icon AppIcon => App.AppIcons.CurrentIcon;
}
