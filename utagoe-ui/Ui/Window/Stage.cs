using static Utagoe.Ui.Win32;

namespace Utagoe.Ui;

internal sealed class Stage : NativeWindow, IDisposable
{
    private readonly Rectangle _area;
    private readonly List<IntPtr> _thumbs = new();

    public Stage(Rectangle area)
    {
        _area = area;
        CreateHandle(new CreateParams
        {
            Style = unchecked((int)0x80000000),
            ExStyle = WS_EX_NOREDIRECTIONBITMAP | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            X = area.X, Y = area.Y, Width = area.Width, Height = area.Height,
        });
        SetDwm(Handle, DWMWA_TRANSITIONS_FORCEDISABLED, 1);
    }

    public IntPtr Add(IntPtr source, Rectangle at, byte opacity)
    {
        if (DwmRegisterThumbnail(Handle, source, out IntPtr thumb) != 0) return IntPtr.Zero;
        _thumbs.Add(thumb);
        Move(thumb, at, opacity);
        return thumb;
    }

    public void Move(IntPtr thumb, Rectangle at, byte opacity)
    {
        if (thumb == IntPtr.Zero) return;
        var props = new DWM_THUMBNAIL_PROPERTIES
        {
            dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_OPACITY | DWM_TNP_VISIBLE | DWM_TNP_SOURCECLIENTAREAONLY,
            rcDestination = new RECT { Left = at.Left - _area.Left, Top = at.Top - _area.Top, Right = at.Right - _area.Left, Bottom = at.Bottom - _area.Top },
            opacity = opacity,
            fVisible = true,
            fSourceClientAreaOnly = false,
        };
        DwmUpdateThumbnailProperties(thumb, ref props);
    }

    public void Crop(IntPtr thumb, Rectangle source, Rectangle at, byte opacity)
    {
        if (thumb == IntPtr.Zero) return;
        var props = new DWM_THUMBNAIL_PROPERTIES
        {
            dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_RECTSOURCE | DWM_TNP_OPACITY | DWM_TNP_VISIBLE | DWM_TNP_SOURCECLIENTAREAONLY,
            rcDestination = new RECT { Left = at.Left - _area.Left, Top = at.Top - _area.Top, Right = at.Right - _area.Left, Bottom = at.Bottom - _area.Top },
            rcSource = new RECT { Left = source.Left, Top = source.Top, Right = source.Right, Bottom = source.Bottom },
            opacity = opacity,
            fVisible = opacity > 0,
            fSourceClientAreaOnly = false,
        };
        DwmUpdateThumbnailProperties(thumb, ref props);
    }

    public void Show() => ShowWindow(Handle, 4);

    public void KeepOnly(IEnumerable<IntPtr> keep)
    {
        var set = new HashSet<IntPtr>(keep);
        foreach (var t in _thumbs.Where(t => !set.Contains(t)).ToList())
        {
            DwmUnregisterThumbnail(t);
            _thumbs.Remove(t);
        }
    }

    public void Remove(IntPtr thumb)
    {
        if (thumb == IntPtr.Zero) return;
        DwmUnregisterThumbnail(thumb);
        _thumbs.Remove(thumb);
    }

    public void Dispose()
    {
        foreach (var t in _thumbs) DwmUnregisterThumbnail(t);
        _thumbs.Clear();
        DestroyHandle();
    }
}
