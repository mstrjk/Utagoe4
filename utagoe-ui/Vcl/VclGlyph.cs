// 元実装 bitmap を読み、VCL の glyph 規則を適用する。
// Resources の BMP は元実装の byte 内容を維持しているので、VCL 特有の transparency / NumGlyphs 処理も再現する。
// transparent color は bitmap 左下 pixel。
// NumGlyphs=2 は横 2 枚で、左が enabled、右が disabled。

using System.Drawing.Imaging;
using System.Reflection;

namespace Utagoe.Vcl;

internal static class VclGlyph
{
    private static readonly Dictionary<string, Bitmap> Cache = new();

    /// embedded bitmap を VCL の auto-transparency 付きで load する。
    public static Bitmap Load(string name)
    {
        if (Cache.TryGetValue(name, out var cached)) return cached;

        var asm = Assembly.GetExecutingAssembly();
        using var s = asm.GetManifestResourceStream($"Utagoe.Resources.{name}.bmp")
                      ?? throw new FileNotFoundException($"embedded bitmap '{name}' is missing");
        using var raw = new Bitmap(s);

        // MakeTransparent で alpha を扱えるよう、4bpp source を 32bpp に変換する。
        var bmp = raw.Clone(new Rectangle(0, 0, raw.Width, raw.Height), PixelFormat.Format32bppArgb);
        bmp.MakeTransparent(bmp.GetPixel(0, bmp.Height - 1));

        Cache[name] = bmp;
        return bmp;
    }

    private static readonly Dictionary<string, Bitmap> AlphaCache = new();

    /// alpha 付きの 32-bit BMP (v4 の icon と logo) を読む。GDI+ は BMP の alpha を捨てるので自前で読む。
    /// name は Resources からの名前 (例 "LogoImg"、"Icons.standard")。
    public static Bitmap LoadAlpha(string name)
    {
        if (AlphaCache.TryGetValue(name, out var cached)) return cached;
        using var s = Assembly.GetExecutingAssembly().GetManifestResourceStream($"Utagoe.Resources.{name}.bmp")
                      ?? throw new FileNotFoundException($"embedded bitmap '{name}' is missing");
        using var r = new BinaryReader(s);
        byte[] all = r.ReadBytes((int)s.Length);
        int offset = BitConverter.ToInt32(all, 10);
        int width = BitConverter.ToInt32(all, 18), height = BitConverter.ToInt32(all, 22);
        bool bottomUp = height > 0;
        height = Math.Abs(height);
        var bmp = new Bitmap(width, height, PixelFormat.Format32bppArgb);
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++)
            {
                int row = bottomUp ? height - 1 - y : y;
                int i = offset + (row * width + x) * 4;
                bmp.SetPixel(x, y, Color.FromArgb(all[i + 3], all[i + 2], all[i + 1], all[i]));
            }
        return AlphaCache[name] = bmp;
    }

    /// NumGlyphs=2 の strip を enabled / disabled の 2 枚へ分割する。
    public static (Bitmap Enabled, Bitmap? Disabled) LoadPair(string name, int numGlyphs = 2)
    {
        var strip = Load(name);
        if (numGlyphs <= 1) return (strip, null);

        int w = strip.Width / numGlyphs;
        var en  = strip.Clone(new Rectangle(0, 0, w, strip.Height), strip.PixelFormat);
        var dis = strip.Clone(new Rectangle(w, 0, w, strip.Height), strip.PixelFormat);
        return (en, dis);
    }

    /// 今選ばれている app icon (色違いは About で選ぶ。App.AppIcons)。exe 自体の icon は Resources\Utagoe.ico。
    public static Icon AppIcon => App.AppIcons.CurrentIcon;

    public static Bitmap ThemeLogo()
    {
        try { return LoadAlpha("Logos." + App.Theme.Current.Name); }
        catch (FileNotFoundException) { return LoadAlpha("Logos.standard"); }
    }
}
