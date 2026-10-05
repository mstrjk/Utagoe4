using static Utagoe.Ui.Win32;

namespace Utagoe.Ui;

internal sealed class Picture : NativeWindow, IDisposable
{
    private readonly IntPtr _bitmap;
    private readonly IntPtr _memory;
    private readonly IntPtr _old;
    private readonly int _w, _h;

    public Picture(Bitmap image, Rectangle at)
    {
        _w = image.Width;
        _h = image.Height;
        _bitmap = image.GetHbitmap();
        IntPtr screen = GetDC(IntPtr.Zero);
        _memory = CreateCompatibleDC(screen);
        ReleaseDC(IntPtr.Zero, screen);
        _old = SelectObject(_memory, _bitmap);
        CreateHandle(new CreateParams
        {
            Style = unchecked((int)0x80000000),
            ExStyle = WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            X = at.X, Y = at.Y, Width = at.Width, Height = at.Height,
        });
        SetDwm(Handle, DWMWA_CLOAK, 1);
        SetDwm(Handle, DWMWA_TRANSITIONS_FORCEDISABLED, 1);
        ShowWindow(Handle, 4);
        UpdateWindow(Handle);
    }

    protected override void WndProc(ref Message m)
    {
        if (m.Msg == WM_ERASEBKGND) { m.Result = new IntPtr(1); return; }
        if (m.Msg == WM_PAINT)
        {
            IntPtr dc = BeginPaint(Handle, out PAINTSTRUCT ps);
            GetClientRect(Handle, out RECT r);
            StretchBlt(dc, 0, 0, r.Right, r.Bottom, _memory, 0, 0, _w, _h, SRCCOPY);
            EndPaint(Handle, ref ps);
            m.Result = IntPtr.Zero;
            return;
        }
        base.WndProc(ref m);
    }

    public void Dispose()
    {
        DestroyHandle();
        SelectObject(_memory, _old);
        DeleteDC(_memory);
        DeleteObject(_bitmap);
    }
}
