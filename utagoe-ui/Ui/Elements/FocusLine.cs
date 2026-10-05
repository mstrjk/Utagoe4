using System.Runtime.InteropServices;
using Utagoe.App;

using static Utagoe.Ui.Win32;

namespace Utagoe.Ui;

internal sealed class FocusLine : NativeWindow
{
    private static readonly System.Runtime.CompilerServices.ConditionalWeakTable<Control, FocusLine> Lines = new();

    private readonly Control _control;
    private readonly bool _frame;

    private FocusLine(Control control)
    {
        _control = control;
        _frame = control is TextBox tb && !tb.Multiline;
        control.Enter += (_, _) => Refresh();
        control.Leave += (_, _) => Refresh();
        control.EnabledChanged += (_, _) => Refresh();
        control.HandleCreated += (_, _) => Hook();
        control.HandleDestroyed += (_, _) => ReleaseHandle();
        if (control.IsHandleCreated) Hook();
    }

    public static void Attach(Control c)
    {
        if (c.Parent is UpDownBase) return;
        if (!Lines.TryGetValue(c, out _)) Lines.Add(c, new FocusLine(c));
        else if (Lines.TryGetValue(c, out var line)) line.Refresh();
    }

    private bool Focused => _control.Enabled && _control.ContainsFocus;

    private void Hook()
    {
        if (Handle != IntPtr.Zero) ReleaseHandle();
        AssignHandle(_control.Handle);
        if (_frame) SetWindowPos(Handle, IntPtr.Zero, 0, 0, 0, 0, 0x0037);
    }

    private void Refresh()
    {
        if (Handle == IntPtr.Zero) return;
        if (_frame) RedrawWindow(Handle, IntPtr.Zero, IntPtr.Zero, 0x0400 | 0x0001);
        else _control.Invalidate(true);
    }

    protected override void WndProc(ref Message m)
    {
        const int WM_NCCALCSIZE = 0x0083, WM_NCPAINT = 0x0085, WM_PAINT = 0x000F, WM_PRINT = 0x0317, PRF_NONCLIENT = 0x4;
        if (m.Msg == WM_PRINT)
        {
            base.WndProc(ref m);
            bool nonClient = ((long)m.LParam & PRF_NONCLIENT) != 0;
            if (_frame && nonClient) PaintBottom(m.WParam);
            else if (!_frame && nonClient && Focused) PaintLine(m.WParam);
            return;
        }
        if (_frame && m.Msg == WM_NCCALCSIZE)
        {
            base.WndProc(ref m);
            var r = Marshal.PtrToStructure<RECT>(m.LParam);
            r.Bottom -= 1;
            Marshal.StructureToPtr(r, m.LParam, false);
            return;
        }
        if (_frame && m.Msg == WM_NCPAINT)
        {
            PaintFrame();
            m.Result = IntPtr.Zero;
            return;
        }
        base.WndProc(ref m);
        if (!_frame && (m.Msg == WM_PAINT || m.Msg == WM_NCPAINT) && Focused) PaintLine();
    }

    private void PaintFrame()
    {
        IntPtr dc = GetWindowDC(Handle);
        if (dc == IntPtr.Zero) return;
        try { PaintFrame(dc); }
        finally { ReleaseDC(Handle, dc); }
    }

    private void PaintFrame(IntPtr dc)
    {
        GetWindowRect(Handle, out RECT wr);
        int w = wr.Right - wr.Left, h = wr.Bottom - wr.Top;
        {
            using var g = Graphics.FromHdc(dc);
            var t = Theme.Current;
            using (var back = new SolidBrush(_control.BackColor)) g.FillRectangle(back, 1, h - 2, w - 2, 1);
            using (var edge = new Pen(_control.Enabled ? t.Line : Theme.Blend(t.Window, t.Text, 0.25))) g.DrawRectangle(edge, 0, 0, w - 1, h - 1);
            if (Focused)
                using (var accent = new SolidBrush(t.InputFocus)) g.FillRectangle(accent, 0, h - 2, w, 2);
        }
    }

    private void PaintBottom(IntPtr dc)
    {
        GetWindowRect(Handle, out RECT wr);
        int w = wr.Right - wr.Left, h = wr.Bottom - wr.Top;
        var t = Theme.Current;
        Color c = Focused ? t.InputFocus : _control.Enabled ? t.Line : Theme.Blend(t.Window, t.Text, 0.25);
        using var g = Graphics.FromHdc(dc);
        using var brush = new SolidBrush(c);
        g.FillRectangle(brush, 0, h - 1, w, 1);
    }

    private void PaintLine()
    {
        IntPtr dc = GetWindowDC(Handle);
        if (dc == IntPtr.Zero) return;
        try { PaintLine(dc); }
        finally { ReleaseDC(Handle, dc); }
    }

    private void PaintLine(IntPtr dc)
    {
        GetWindowRect(Handle, out RECT wr);
        int w = wr.Right - wr.Left, h = wr.Bottom - wr.Top;
        using var g = Graphics.FromHdc(dc);
        using var accent = new SolidBrush(Theme.Current.InputFocus);
        g.FillRectangle(accent, 0, h - 2, w, 2);
    }
}
