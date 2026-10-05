using System.Drawing.Imaging;
using static Utagoe.Ui.Win32;

namespace Utagoe.Ui;

internal sealed class Transition
{
    private static Transition? s_active;
    private static readonly List<Action> s_after = new();

    private readonly List<Form> _forms = new();
    private readonly bool _real;

    private Transition(bool real) => _real = real;

    public static IEnumerable<Form> OpenWindows() =>
        Application.OpenForms.Cast<Form>().Where(f => f.TopLevel && f.Visible && f.IsHandleCreated && f.WindowState != FormWindowState.Minimized &&
                                                     f is not InfoTip && IsWindowVisible(f.Handle)).ToList();

    public static void After(Action action)
    {
        if (s_active == null) action();
        else s_after.Add(action);
    }

    public static Bitmap? Shot(Form f)
    {
        if (!f.IsHandleCreated || f.Width <= 0 || f.Height <= 0) return null;
        var bmp = new Bitmap(f.Width, f.Height, PixelFormat.Format24bppRgb);
        using var g = Graphics.FromImage(bmp);
        IntPtr dc = g.GetHdc();
        bool ok = PrintWindow(f.Handle, dc, 2);
        g.ReleaseHdc(dc);
        if (ok) return bmp;
        bmp.Dispose();
        return null;
    }

    public static Transition Begin(IEnumerable<Form> forms)
    {
        if (s_active != null) return new Transition(false);
        var t = new Transition(true);
        t._forms.AddRange(forms.Where(f => !f.IsDisposed && f.IsHandleCreated));
        foreach (var f in t._forms) SendMessage(f.Handle, WM_SETREDRAW, IntPtr.Zero, IntPtr.Zero);
        s_active = t;
        return t;
    }

    public void End(bool fade)
    {
        if (!_real) return;
        foreach (var f in _forms)
            if (!f.IsDisposed && f.IsHandleCreated) SendMessage(f.Handle, WM_SETREDRAW, (IntPtr)1, IntPtr.Zero);
        var ready = _forms.Select(Render).Where(r => r != null).Select(r => r!).ToList();
        if (ready.Count > 0) DwmFlush();
        foreach (var r in ready) r.Blit();
        foreach (var r in ready) r.Dispose();
        if (s_active == this) s_active = null;
        var after = s_after.ToArray();
        s_after.Clear();
        foreach (var a in after) a();
    }

    private sealed class Rendered : IDisposable
    {
        public required Form Form;
        public required IntPtr Bitmap, Memory, Old;
        public required bool Frame;
        public required Rectangle Client;
        public required Point Offset;

        public void Blit()
        {
            if (Form.IsDisposed || !Form.IsHandleCreated) return;
            IntPtr dc = Frame ? GetDCEx(Form.Handle, IntPtr.Zero, DCX_CACHE | DCX_WINDOW) : GetDCEx(Form.Handle, IntPtr.Zero, DCX_CACHE);
            try
            {
                if (Frame) BitBlt(dc, 0, 0, Form.Width, Form.Height, Memory, 0, 0, (int)SRCCOPY);
                else BitBlt(dc, 0, 0, Client.Width, Client.Height, Memory, Offset.X, Offset.Y, (int)SRCCOPY);
            }
            finally { ReleaseDC(Form.Handle, dc); }
            RedrawWindow(Form.Handle, IntPtr.Zero, IntPtr.Zero, RDW_VALIDATE | RDW_ALLCHILDREN);
        }

        public void Dispose()
        {
            SelectObject(Memory, Old);
            DeleteDC(Memory);
            DeleteObject(Bitmap);
        }
    }

    private static Rendered? Render(Form f)
    {
        if (f.IsDisposed || !f.IsHandleCreated || !f.Visible || f.WindowState == FormWindowState.Minimized) return null;
        Size client = f.ClientSize;
        if (client.Width <= 0 || client.Height <= 0 || f.Width <= 0 || f.Height <= 0) return null;
        f.PerformLayout();
        using var image = new Bitmap(f.Width, f.Height, PixelFormat.Format24bppRgb);
        f.DrawToBitmap(image, new Rectangle(Point.Empty, f.Size));
        Point origin = f.PointToScreen(Point.Empty);
        var offset = new Point(origin.X - f.Left, origin.Y - f.Top);
        bool frame;
        using (var g = Graphics.FromImage(image))
        {
            g.ExcludeClip(new Rectangle(offset, client));
            frame = ThemedFrame.Draw(f, g);
        }
        IntPtr bitmap = image.GetHbitmap();
        IntPtr screen = GetDC(IntPtr.Zero);
        IntPtr memory = CreateCompatibleDC(screen);
        ReleaseDC(IntPtr.Zero, screen);
        IntPtr old = SelectObject(memory, bitmap);
        return new Rendered { Form = f, Bitmap = bitmap, Memory = memory, Old = old, Frame = frame, Client = new Rectangle(Point.Empty, client), Offset = offset };
    }
}
