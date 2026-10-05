using static Utagoe.Ui.Win32;

namespace Utagoe.Ui;

internal static class WindowFade
{
    private const int DurationMs = 220;

    private static float Smooth(double t)
    {
        float x = (float)Math.Clamp(t / DurationMs, 0, 1);
        return x * x * (3 - 2 * x);
    }

    private static Rectangle Area(Form f) => Rectangle.Intersect(f.Bounds, Screen.FromHandle(f.Handle).WorkingArea);

    public static void Out(Form f, Action hide)
    {
        if (Morph.Join(f, leaving: true, hide)) return;
        if (f.IsDisposed || !f.IsHandleCreated || !f.Visible || f.WindowState == FormWindowState.Minimized)
        {
            hide();
            return;
        }
        Rectangle area = Area(f);
        if (area.Width <= 0 || area.Height <= 0)
        {
            hide();
            return;
        }
        var stage = new Stage(area);
        IntPtr thumb = stage.Add(f.Handle, f.Bounds, 255);
        stage.Show();
        DwmFlush();
        DwmFlush();
        SetDwm(f.Handle, DWMWA_CLOAK, 1);
        Morph.Animate(f, DurationMs, t => stage.Move(thumb, f.Bounds, (byte)Math.Round(255 * (1 - Smooth(t)))), () =>
        {
            hide();
            if (!f.IsDisposed && f.IsHandleCreated) SetDwm(f.Handle, DWMWA_CLOAK, 0);
            stage.Dispose();
        });
    }

    public static void In(Form f, Action show)
    {
        if (Morph.Join(f, leaving: false, show)) return;
        if (f.IsDisposed)
        {
            show();
            return;
        }
        if (f.IsHandleCreated) SetDwm(f.Handle, DWMWA_CLOAK, 1);
        show();
        if (!f.IsHandleCreated || !f.Visible)
        {
            if (f.IsHandleCreated) SetDwm(f.Handle, DWMWA_CLOAK, 0);
            return;
        }
        SetDwm(f.Handle, DWMWA_CLOAK, 1);
        RedrawWindow(f.Handle, IntPtr.Zero, IntPtr.Zero, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
        Rectangle area = Area(f);
        if (area.Width <= 0 || area.Height <= 0)
        {
            SetDwm(f.Handle, DWMWA_CLOAK, 0);
            return;
        }
        var stage = new Stage(area);
        IntPtr thumb = stage.Add(f.Handle, f.Bounds, 0);
        stage.Show();
        Morph.Animate(f, DurationMs, t => stage.Move(thumb, f.Bounds, (byte)Math.Round(255 * Smooth(t))), () =>
        {
            if (!f.IsDisposed && f.IsHandleCreated)
            {
                SetDwm(f.Handle, DWMWA_CLOAK, 0);
                DwmFlush();
            }
            stage.Dispose();
        });
    }
}
