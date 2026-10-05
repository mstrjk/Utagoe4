// app icon の色違い (v4)。About で選ぶと、開いている window の icon をその場で替え、Start menu の shortcut も合わせる。
// 絵は Resources\Icons の 32-bit BMP (alpha 付き、90x90 の pixel art)。GDI+ は BMP の alpha を捨てるので自前で読む。
// 大きさを変えるときは pixel art が崩れないよう、拡大は最近傍、縮小は面積平均にする。

using System.Diagnostics;
using System.Reflection;
using System.Runtime.InteropServices;
using Utagoe.Native;

namespace Utagoe.App;

internal static class AppIcons
{
    public const string Default = "standard";
    public static readonly string[] Names = { "standard", "army", "ice", "silver", "wine" };

    private static readonly int[] IconSizes = { 16, 20, 24, 32, 40, 48, 64, 96, 128, 256 };
    private static readonly Dictionary<string, Bitmap> Sources = new();
    private static readonly Dictionary<(string, int), Bitmap> Rendered = new();
    private static readonly Dictionary<string, Icon> Icons = new();

    public static string Current { get; private set; } = Default;

    public static Icon CurrentIcon => Get(Current);

    public static string DisplayName(string name) => L.T(char.ToUpperInvariant(name[0]) + name[1..]);

    public static string Normalize(string? name) =>
        name != null && Names.Contains(name, StringComparer.OrdinalIgnoreCase) ? name.ToLowerInvariant() : Default;

    /// 色違いを選ぶ。theme と開いている window の icon を替え、導入版なら Start menu の shortcut の icon も替える。
    public static void Apply(string name, bool updateShell)
    {
        Current = Normalize(name);
        var swap = Utagoe.Ui.Transition.Begin(Utagoe.Ui.Transition.OpenWindows());
        // icon の色違いと theme は 1 対 1。
        Theme.Set(Current);
        Icon icon = CurrentIcon;
        foreach (Form f in Application.OpenForms.Cast<Form>().ToList())
            if (f.ShowIcon && f.TopLevel) f.Icon = icon;
        swap.End(fade: false);
        if (updateShell) UpdateShortcut();
    }

    /// size x size に描いた絵 (使い回すので Dispose しない)。
    public static Bitmap Render(string name, int size)
    {
        name = Normalize(name);
        if (Rendered.TryGetValue((name, size), out var cached)) return cached;
        return Rendered[(name, size)] = Scale(Source(name), size);
    }

    public static Icon Get(string name)
    {
        name = Normalize(name);
        if (Icons.TryGetValue(name, out var icon)) return icon;
        using var ms = new MemoryStream(IcoBytes(name));
        return Icons[name] = new Icon(ms);
    }


    private static Bitmap Source(string name)
    {
        if (Sources.TryGetValue(name, out var bmp)) return bmp;
        return Sources[name] = Ui.Art.AppIconArt(name);
    }

    private static int[] Pixels(Bitmap b)
    {
        var rect = new Rectangle(0, 0, b.Width, b.Height);
        var data = b.LockBits(rect, System.Drawing.Imaging.ImageLockMode.ReadOnly, System.Drawing.Imaging.PixelFormat.Format32bppArgb);
        try
        {
            var px = new int[b.Width * b.Height];
            for (int y = 0; y < b.Height; y++)
                System.Runtime.InteropServices.Marshal.Copy(data.Scan0 + y * data.Stride, px, y * b.Width, b.Width);
            return px;
        }
        finally { b.UnlockBits(data); }
    }

    private static Bitmap FromPixels(int[] px, int size)
    {
        var b = new Bitmap(size, size, System.Drawing.Imaging.PixelFormat.Format32bppArgb);
        var data = b.LockBits(new Rectangle(0, 0, size, size), System.Drawing.Imaging.ImageLockMode.WriteOnly, b.PixelFormat);
        try
        {
            for (int y = 0; y < size; y++)
                System.Runtime.InteropServices.Marshal.Copy(px, y * size, data.Scan0 + y * data.Stride, size);
        }
        finally { b.UnlockBits(data); }
        return b;
    }

    private static Bitmap Scale(Bitmap src, int size)
    {
        int n = src.Width;
        int[] from = Pixels(src);
        var to = new int[size * size];
        if (size >= n)
        {
            for (int y = 0; y < size; y++)
                for (int x = 0; x < size; x++)
                    to[y * size + x] = from[(y * n / size) * n + x * n / size];
            return FromPixels(to, size);
        }
        double f = (double)n / size;
        for (int y = 0; y < size; y++)
            for (int x = 0; x < size; x++)
            {
                double x0 = x * f, x1 = x0 + f, y0 = y * f, y1 = y0 + f, r = 0, g = 0, bl = 0, a = 0, area = 0;
                for (int sy = (int)y0; sy < Math.Min(n, (int)Math.Ceiling(y1)); sy++)
                    for (int sx = (int)x0; sx < Math.Min(n, (int)Math.Ceiling(x1)); sx++)
                    {
                        double w = (Math.Min(x1, sx + 1) - Math.Max(x0, sx)) * (Math.Min(y1, sy + 1) - Math.Max(y0, sy));
                        if (w <= 0) continue;
                        int c = from[sy * n + sx];
                        int ca = (c >> 24) & 255, cr = (c >> 16) & 255, cg = (c >> 8) & 255, cb = c & 255;
                        double wa = w * ca / 255.0;
                        r += cr * wa; g += cg * wa; bl += cb * wa; a += wa; area += w;
                    }
                to[y * size + x] = a <= 0 ? Color.Transparent.ToArgb()
                    : Color.FromArgb((int)Math.Round(255 * a / area), (int)Math.Round(r / a), (int)Math.Round(g / a), (int)Math.Round(bl / a)).ToArgb();
            }
        return FromPixels(to, size);
    }


    private static byte[] IcoBytes(string name)
    {
        var images = IconSizes.Select(size => Dib(Render(name, size))).ToArray();
        using var ms = new MemoryStream();
        using var w = new BinaryWriter(ms);
        w.Write((short)0); w.Write((short)1); w.Write((short)images.Length);
        int offset = 6 + 16 * images.Length;
        for (int i = 0; i < images.Length; i++)
        {
            int size = IconSizes[i];
            w.Write((byte)(size >= 256 ? 0 : size)); w.Write((byte)(size >= 256 ? 0 : size));
            w.Write((byte)0); w.Write((byte)0); w.Write((short)1); w.Write((short)32);
            w.Write(images[i].Length); w.Write(offset);
            offset += images[i].Length;
        }
        foreach (var img in images) w.Write(img);
        w.Flush();
        return ms.ToArray();
    }

    private static byte[] Dib(Bitmap b)
    {
        int n = b.Width, maskStride = (n + 31) / 32 * 4;
        int[] px = Pixels(b);
        using var ms = new MemoryStream();
        using var w = new BinaryWriter(ms);
        w.Write(40); w.Write(n); w.Write(n * 2); w.Write((short)1); w.Write((short)32);
        w.Write(0); w.Write(n * n * 4 + maskStride * n); w.Write(0); w.Write(0); w.Write(0); w.Write(0);
        for (int y = n - 1; y >= 0; y--)
            for (int x = 0; x < n; x++) w.Write(px[y * n + x]);
        for (int y = n - 1; y >= 0; y--)
        {
            var row = new byte[maskStride];
            for (int x = 0; x < n; x++) if (((px[y * n + x] >> 24) & 255) < 128) row[x >> 3] |= (byte)(0x80 >> (x & 7));
            w.Write(row);
        }
        w.Flush();
        return ms.ToArray();
    }


    /// 導入版だけ: 選んだ icon を icons\<name>.ico に書き、Start menu の shortcut と「アプリ」一覧の icon をそれに向ける。
    private static void UpdateShortcut()
    {
        if (!AppPaths.IsInstalledCopy) return;
        try
        {
            string dir = Path.Combine(AppPaths.Root, "icons");
            Directory.CreateDirectory(dir);
            string ico = Path.Combine(dir, Current + ".ico");
            if (!File.Exists(ico)) File.WriteAllBytes(ico, IcoBytes(Current));
            if (Installer.SetShellIcon(ico)) SHChangeNotify(0x08000000, 0, IntPtr.Zero, IntPtr.Zero);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or COMException)
        {
            LogHub.Exception("could not update the Start menu icon", ex);
        }
    }

    [DllImport("shell32.dll")] private static extern void SHChangeNotify(int eventId, uint flags, IntPtr item1, IntPtr item2);
}
