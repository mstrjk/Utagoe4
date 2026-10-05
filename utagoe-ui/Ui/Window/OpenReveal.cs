using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

using static Utagoe.Ui.Win32;

namespace Utagoe.Ui;

internal static class OpenReveal
{
    private const int DurationMs = 170;
    private const float StartScale = 0.94f;

    public static void Begin(Form form, bool keepTransitions)
    {
        IntPtr hwnd = form.Handle;
        SetDwm(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, 1);
        SetDwm(hwnd, DWMWA_CLOAK, 1);
        bool started = false;
        var fallback = new System.Windows.Forms.Timer { Interval = 600 };
        void Start()
        {
            if (started) return;
            started = true;
            Application.Idle -= OnIdle;
            fallback.Stop();
            fallback.Dispose();
            Run(form, hwnd, keepTransitions);
        }
        void OnIdle(object? sender, EventArgs e) => Start();
        fallback.Tick += (_, _) => Start();
        Application.Idle += OnIdle;
        fallback.Start();
    }

    private static void Uncloak(IntPtr hwnd, bool keepTransitions)
    {
        SetDwm(hwnd, DWMWA_CLOAK, 0);
        if (keepTransitions) SetDwm(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, 0);
    }

    private static void Run(Form form, IntPtr hwnd, bool keepTransitions)
    {
        if (form.IsDisposed || !IsWindowVisible(hwnd) || form.WindowState != FormWindowState.Normal)
        {
            Uncloak(hwnd, keepTransitions);
            return;
        }
        RedrawWindow(hwnd, IntPtr.Zero, IntPtr.Zero, 0x0001 | 0x0004 | 0x0400 | 0x0080 | 0x0100);
        GetWindowRect(hwnd, out RECT r);
        int w = r.Right - r.Left, h = r.Bottom - r.Top;
        Bitmap? shot = null;
        try
        {
            shot = new Bitmap(w, h, PixelFormat.Format24bppRgb);
            using (var g = Graphics.FromImage(shot))
            {
                IntPtr dc = g.GetHdc();
                bool ok = PrintWindow(hwnd, dc, 2);
                g.ReleaseHdc(dc);
                if (!ok) throw new InvalidOperationException();
            }
        }
        catch (Exception ex) when (ex is ArgumentException or InvalidOperationException or ExternalException)
        {
            shot?.Dispose();
            Uncloak(hwnd, keepTransitions);
            return;
        }

        var layer = new Layer(new Rectangle(r.Left, r.Top, w, h), IntPtr.Zero);
        var frame = new Bitmap(w, h, PixelFormat.Format32bppPArgb);
        var clock = System.Diagnostics.Stopwatch.StartNew();
        var timer = new System.Windows.Forms.Timer { Interval = 10 };

        void Draw(float t)
        {
            float e = 1 - (1 - t) * (1 - t) * (1 - t);
            float s = StartScale + (1 - StartScale) * e;
            using (var g = Graphics.FromImage(frame))
            {
                g.Clear(Color.Transparent);
                g.InterpolationMode = InterpolationMode.HighQualityBilinear;
                g.PixelOffsetMode = PixelOffsetMode.HighQuality;
                float dw = w * s, dh = h * s;
                g.DrawImage(shot!, (w - dw) / 2, (h - dh) / 2, dw, dh);
            }
            layer.Show(frame, (byte)Math.Round(255 * e));
        }

        void Finish()
        {
            timer.Stop();
            timer.Dispose();
            Uncloak(hwnd, keepTransitions);
            layer.Dispose();
            frame.Dispose();
            shot!.Dispose();
        }

        timer.Tick += (_, _) =>
        {
            if (form.IsDisposed || !IsWindowVisible(hwnd)) { Finish(); return; }
            float t = Math.Min(1f, clock.ElapsedMilliseconds / (float)DurationMs);
            if (t >= 1f) { Finish(); return; }
            Draw(t);
        };
        Draw(0.02f);
        timer.Start();
    }
}
