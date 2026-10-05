using System.Runtime.InteropServices;

namespace Utagoe.Ui;

internal static class Win32
{
    public const int WM_SETREDRAW = 0x000B, WM_SETTEXT = 0x000C, WM_PAINT = 0x000F, WM_SHOWWINDOW = 0x0018, WM_GETMINMAXINFO = 0x0024,
                     WM_SETFONT = 0x0030, WM_SETICON = 0x0080, WM_NCCALCSIZE = 0x0083, WM_NCHITTEST = 0x0084, WM_NCPAINT = 0x0085,
                     WM_NCACTIVATE = 0x0086, WM_NCMOUSEMOVE = 0x00A0, WM_NCLBUTTONDOWN = 0x00A1, WM_NCLBUTTONUP = 0x00A2,
                     WM_NCLBUTTONDBLCLK = 0x00A3, WM_NCUAHDRAWCAPTION = 0x00AE, WM_NCUAHDRAWFRAME = 0x00AF, WM_NCMOUSELEAVE = 0x02A2,
                     WM_LBUTTONDOWN = 0x0201, WM_LBUTTONDBLCLK = 0x0203;

    public const int WM_SYSCOMMAND = 0x0112, SC_MINIMIZE = 0xF020, SC_MAXIMIZE = 0xF030, SC_RESTORE = 0xF120;

    public const int TCM_SETPADDING = 0x132B, TBM_SETTHUMBLENGTH = 0x041B;

    public const uint RDW_VALIDATE = 0x8, RDW_INVALIDATE = 0x0001, RDW_ERASE = 0x0004, RDW_ALLCHILDREN = 0x0080, RDW_UPDATENOW = 0x0100, RDW_FRAME = 0x0400;

    public const int DWMWA_NCRENDERING_POLICY = 2, DWMWA_TRANSITIONS_FORCEDISABLED = 3, DWMWA_CLOAK = 13, DWMWA_BORDER_COLOR = 34;

    public const int WS_EX_COMPOSITED = 0x02000000, WS_EX_LAYERED = 0x00080000, WS_EX_TRANSPARENT = 0x00000020,
                     WS_EX_NOACTIVATE = 0x08000000, WS_EX_TOOLWINDOW = 0x00000080;

    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct COMBOBOXINFO
    {
        public int cbSize;
        public RECT rcItem, rcButton;
        public int stateButton;
        public IntPtr hwndCombo, hwndItem, hwndList;
    }
    [DllImport("user32.dll")] public static extern bool GetComboBoxInfo(IntPtr h, ref COMBOBOXINFO info);
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] public struct SIZE { public int Width, Height; }
    [StructLayout(LayoutKind.Sequential, Pack = 1)] public struct BLENDFUNCTION { public byte BlendOp, BlendFlags, SourceConstantAlpha, AlphaFormat; }
    [StructLayout(LayoutKind.Sequential)] public struct TRACKMOUSEEVENT { public int cbSize; public int dwFlags; public IntPtr hwndTrack; public int dwHoverTime; }

    [StructLayout(LayoutKind.Sequential)]
    public struct MINMAXINFO
    {
        public int ReservedX, ReservedY, MaxSizeX, MaxSizeY, MaxPosX, MaxPosY, MinTrackX, MinTrackY, MaxTrackX, MaxTrackY;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct TEXTMETRICW
    {
        public int tmHeight, tmAscent, tmDescent, tmInternalLeading, tmExternalLeading,
                   tmAveCharWidth, tmMaxCharWidth, tmWeight, tmOverhang,
                   tmDigitizedAspectX, tmDigitizedAspectY;
        public char tmFirstChar, tmLastChar, tmDefaultChar, tmBreakChar;
        public byte tmItalic, tmUnderlined, tmStruckOut, tmPitchAndFamily, tmCharSet;
    }

    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, int msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool RedrawWindow(IntPtr h, IntPtr rect, IntPtr rgn, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] public static extern IntPtr GetDC(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetWindowDC(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetDCEx(IntPtr h, IntPtr clip, uint flags);
    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")] public static extern IntPtr GetWindowLongPtr(IntPtr h, int index);
    [DllImport("user32.dll", EntryPoint = "SetWindowLongPtrW")] public static extern IntPtr SetWindowLongPtr(IntPtr h, int index, IntPtr value);
    public const int GWL_STYLE = -16;
    public const long WS_VISIBLE = 0x10000000;
    public const uint DCX_WINDOW = 0x1, DCX_CACHE = 0x2;
    [DllImport("user32.dll")] public static extern int ReleaseDC(IntPtr h, IntPtr dc);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool TrackMouseEvent(ref TRACKMOUSEEVENT e);
    [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(Point p);
    [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h, uint flags);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
    [DllImport("user32.dll")]
    public static extern bool UpdateLayeredWindow(IntPtr h, IntPtr dst, ref POINT pt, ref SIZE size, IntPtr src, ref POINT srcPt, int key, ref BLENDFUNCTION blend, int flags);
    [DllImport("user32.dll", EntryPoint = "UpdateLayeredWindow")]
    public static extern bool UpdateLayeredAlpha(IntPtr h, IntPtr dst, IntPtr pt, IntPtr size, IntPtr src, IntPtr srcPt, int key, ref BLENDFUNCTION blend, int flags);

    [DllImport("gdi32.dll")] public static extern IntPtr SelectObject(IntPtr dc, IntPtr obj);
    [DllImport("gdi32.dll")] public static extern int SetTextColor(IntPtr dc, int color);
    [DllImport("gdi32.dll")] public static extern int SetBkColor(IntPtr dc, int color);
    [DllImport("gdi32.dll")] public static extern IntPtr CreateSolidBrush(int color);
    [DllImport("gdi32.dll")] public static extern bool DeleteObject(IntPtr obj);
    [DllImport("gdi32.dll")] public static extern IntPtr CreateCompatibleDC(IntPtr dc);
    [DllImport("gdi32.dll")] public static extern bool DeleteDC(IntPtr dc);
    [DllImport("gdi32.dll", CharSet = CharSet.Unicode)] public static extern bool GetTextExtentPoint32W(IntPtr dc, string s, int n, out SIZE size);
    [DllImport("gdi32.dll", CharSet = CharSet.Unicode)] public static extern bool GetTextMetricsW(IntPtr dc, out TEXTMETRICW tm);
    [DllImport("gdi32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr CreateFontW(int h, int w, int esc, int orient, int weight, uint italic, uint underline, uint strike,
                                            uint charset, uint outPrec, uint clipPrec, uint quality, uint pitch, string face);

    [DllImport("dwmapi.dll")] public static extern int DwmSetWindowAttribute(IntPtr h, int attr, ref int value, int size);
    [DllImport("uxtheme.dll", CharSet = CharSet.Unicode)] public static extern int SetWindowTheme(IntPtr h, string? app, string? ids);

    [StructLayout(LayoutKind.Sequential)]
    public struct PAINTSTRUCT
    {
        public IntPtr hdc;
        public bool fErase;
        public RECT rcPaint;
        public bool fRestore, fIncUpdate;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)] public byte[] rgbReserved;
    }

    [DllImport("user32.dll")] public static extern IntPtr BeginPaint(IntPtr h, out PAINTSTRUCT ps);
    [DllImport("user32.dll")] public static extern bool EndPaint(IntPtr h, ref PAINTSTRUCT ps);
    [DllImport("user32.dll")] public static extern bool SetLayeredWindowAttributes(IntPtr h, uint key, byte alpha, uint flags);
    [DllImport("user32.dll")] public static extern bool UpdateWindow(IntPtr h);
    [DllImport("gdi32.dll")] public static extern bool BitBlt(IntPtr dst, int x, int y, int w, int h, IntPtr src, int sx, int sy, int rop);
    [DllImport("gdi32.dll")] public static extern bool StretchBlt(IntPtr dst, int x, int y, int w, int h, IntPtr src, int sx, int sy, int sw, int sh, uint rop);
    [DllImport("gdi32.dll")] public static extern int SetStretchBltMode(IntPtr dc, int mode);

    public const int WM_ERASEBKGND = 0x0014;
    public const uint LWA_ALPHA = 0x2, SRCCOPY = 0x00CC0020;
    public const uint SWP_NOSIZE = 0x0001, SWP_NOMOVE = 0x0002, SWP_NOZORDER = 0x0004, SWP_NOACTIVATE = 0x0010, SWP_SHOWWINDOW = 0x0040;

    [StructLayout(LayoutKind.Sequential)]
    public struct DWM_THUMBNAIL_PROPERTIES
    {
        public int dwFlags;
        public RECT rcDestination;
        public RECT rcSource;
        public byte opacity;
        [MarshalAs(UnmanagedType.Bool)] public bool fVisible;
        [MarshalAs(UnmanagedType.Bool)] public bool fSourceClientAreaOnly;
    }

    public const int DWM_TNP_RECTDESTINATION = 0x1, DWM_TNP_RECTSOURCE = 0x2, DWM_TNP_OPACITY = 0x4, DWM_TNP_VISIBLE = 0x8, DWM_TNP_SOURCECLIENTAREAONLY = 0x10;
    public const int WS_EX_NOREDIRECTIONBITMAP = 0x00200000;

    [DllImport("dwmapi.dll")] public static extern int DwmRegisterThumbnail(IntPtr dest, IntPtr src, out IntPtr thumb);
    [DllImport("dwmapi.dll")] public static extern int DwmUnregisterThumbnail(IntPtr thumb);
    [DllImport("dwmapi.dll")] public static extern int DwmUpdateThumbnailProperties(IntPtr thumb, ref DWM_THUMBNAIL_PROPERTIES props);
    [DllImport("dwmapi.dll")] public static extern int DwmFlush();

    public static void SetDwm(IntPtr h, int attr, int value) => DwmSetWindowAttribute(h, attr, ref value, sizeof(int));

    [DllImport("dwmapi.dll")] private static extern int DwmGetWindowAttribute(IntPtr h, int attr, out RECT value, int size);
    [DllImport("dwmapi.dll")] private static extern int DwmGetWindowAttribute(IntPtr h, int attr, out int value, int size);
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint cmd);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();

    public static bool IsCloaked(IntPtr h) => DwmGetWindowAttribute(h, 14, out int v, sizeof(int)) == 0 && v != 0;

    public static Rectangle VisibleBounds(IntPtr h)
    {
        if (DwmGetWindowAttribute(h, 9, out RECT r, Marshal.SizeOf<RECT>()) != 0) return Rectangle.Empty;
        return Rectangle.FromLTRB(r.Left, r.Top, r.Right, r.Bottom);
    }

    public static void Freeze(Control c) { if (c.IsHandleCreated) SendMessage(c.Handle, WM_SETREDRAW, IntPtr.Zero, IntPtr.Zero); }

    public static void Thaw(Control c, uint flags = RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW)
    {
        if (!c.IsHandleCreated) return;
        SendMessage(c.Handle, WM_SETREDRAW, new IntPtr(1), IntPtr.Zero);
        RedrawWindow(c.Handle, IntPtr.Zero, IntPtr.Zero, flags);
    }

    public static int TextMetricHeight(Font font)
    {
        IntPtr dc = GetDC(IntPtr.Zero);
        IntPtr hfont = font.ToHfont();
        try
        {
            IntPtr old = SelectObject(dc, hfont);
            GetTextMetricsW(dc, out TEXTMETRICW tm);
            SelectObject(dc, old);
            return tm.tmHeight;
        }
        finally
        {
            DeleteObject(hfont);
            ReleaseDC(IntPtr.Zero, dc);
        }
    }
}
