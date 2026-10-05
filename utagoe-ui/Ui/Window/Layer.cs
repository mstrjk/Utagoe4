using static Utagoe.Ui.Win32;

namespace Utagoe.Ui;

internal sealed class Layer : NativeWindow, IDisposable
{
    private readonly Rectangle _bounds;

    public Layer(Rectangle bounds, IntPtr owner)
    {
        _bounds = bounds;
        CreateHandle(new CreateParams
        {
            Style = unchecked((int)0x80000000),
            ExStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            X = bounds.X, Y = bounds.Y, Width = bounds.Width, Height = bounds.Height,
            Parent = owner,
        });
    }

    public void Show(Bitmap frame, byte alpha)
    {
        IntPtr screen = GetDC(IntPtr.Zero);
        IntPtr mem = CreateCompatibleDC(screen);
        IntPtr bmp = frame.GetHbitmap(Color.FromArgb(0));
        IntPtr old = SelectObject(mem, bmp);
        try
        {
            var size = new SIZE { Width = _bounds.Width, Height = _bounds.Height };
            var dst = new POINT { X = _bounds.X, Y = _bounds.Y };
            var src = new POINT();
            var blend = new BLENDFUNCTION { BlendOp = 0, BlendFlags = 0, SourceConstantAlpha = alpha, AlphaFormat = 1 };
            UpdateLayeredWindow(Handle, screen, ref dst, ref size, mem, ref src, 0, ref blend, 2);
            ShowWindow(Handle, 4);
        }
        finally
        {
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            ReleaseDC(IntPtr.Zero, screen);
        }
    }

    public void Fade(byte alpha)
    {
        var blend = new BLENDFUNCTION { BlendOp = 0, BlendFlags = 0, SourceConstantAlpha = alpha, AlphaFormat = 1 };
        UpdateLayeredAlpha(Handle, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, 0, ref blend, 2);
    }

    public void Dispose() => DestroyHandle();
}
