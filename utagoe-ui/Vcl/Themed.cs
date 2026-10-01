// theme の色で描く標準部品 (v4)。Windows が描く部品は theme の色にならず (暗い theme では白く浮き、
// 灰色の文字は読めない)、theme ごとに見た目もそろわない。そこで文字・枠・tab・進捗棒・check / radio を
// どの theme でも同じ形で自前に描き、色だけを theme から取る。

using System.Drawing.Drawing2D;
using System.Runtime.InteropServices;
using System.Windows.Forms.VisualStyles;
using ContentAlignment = System.Drawing.ContentAlignment;
using Utagoe.App;

namespace Utagoe.Vcl;

/// 無効のときも theme の淡い文字色で読めるように描く label。
internal class ThemedLabel : Label
{
    protected override void OnPaint(PaintEventArgs e)
    {
        if (Enabled) { base.OnPaint(e); return; }
        TextRenderer.DrawText(e.Graphics, Text, Font, ClientRectangle, Theme.Current.Muted, Flags(TextAlign, AutoSize));
    }

    internal static TextFormatFlags Flags(ContentAlignment a, bool autoSize)
    {
        var f = autoSize ? TextFormatFlags.SingleLine : TextFormatFlags.WordBreak;
        f |= a switch
        {
            ContentAlignment.TopCenter or ContentAlignment.MiddleCenter or ContentAlignment.BottomCenter => TextFormatFlags.HorizontalCenter,
            ContentAlignment.TopRight or ContentAlignment.MiddleRight or ContentAlignment.BottomRight => TextFormatFlags.Right,
            _ => TextFormatFlags.Left,
        };
        f |= a switch
        {
            ContentAlignment.MiddleLeft or ContentAlignment.MiddleCenter or ContentAlignment.MiddleRight => TextFormatFlags.VerticalCenter,
            ContentAlignment.BottomLeft or ContentAlignment.BottomCenter or ContentAlignment.BottomRight => TextFormatFlags.Bottom,
            _ => TextFormatFlags.Top,
        };
        return f;
    }
}

/// check box と radio button の共通の描き方。
internal static class ToggleDraw
{
    public static void Paint(ButtonBase b, Graphics g, bool radio, bool isChecked, bool hot, bool pressed, bool keyboardCues, bool focusCues)
    {
        var t = Theme.Current;
        using (var back = new SolidBrush(b.BackColor)) g.FillRectangle(back, b.ClientRectangle);

        Size glyph = radio ? RadioButtonRenderer.GetGlyphSize(g, RadioButtonState.UncheckedNormal)
                           : CheckBoxRenderer.GetGlyphSize(g, CheckBoxState.UncheckedNormal);
        var at = new Point(0, (b.Height - glyph.Height) / 2);
        {
            var box = new Rectangle(at, new Size(glyph.Width - 1, glyph.Height - 1));
            Color edge = !b.Enabled ? Theme.Blend(t.Window, t.Text, 0.25) : hot ? t.Link : Theme.Blend(t.Window, t.Text, 0.55);
            Color fill = !b.Enabled ? t.Window : pressed ? Theme.Blend(t.Input, t.Text, 0.15) : t.Input;
            Color mark = b.Enabled ? t.Link : t.Muted;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            using var fb = new SolidBrush(fill);
            using var ep = new Pen(edge);
            if (radio)
            {
                g.FillEllipse(fb, box);
                g.DrawEllipse(ep, box);
                if (isChecked)
                {
                    using var mb = new SolidBrush(mark);
                    var dot = Rectangle.Inflate(box, -box.Width / 4 - 1, -box.Height / 4 - 1);
                    g.FillEllipse(mb, dot);
                }
            }
            else
            {
                g.FillRectangle(fb, box);
                g.DrawRectangle(ep, box);
                if (isChecked)
                {
                    using var mp = new Pen(mark, Math.Max(1.6f, box.Width / 7f));
                    g.DrawLines(mp, new[]
                    {
                        new PointF(box.Left + box.Width * 0.22f, box.Top + box.Height * 0.52f),
                        new PointF(box.Left + box.Width * 0.42f, box.Top + box.Height * 0.72f),
                        new PointF(box.Left + box.Width * 0.78f, box.Top + box.Height * 0.28f),
                    });
                }
            }
            g.SmoothingMode = SmoothingMode.Default;
        }


        var textRect = new Rectangle(glyph.Width + 3, 0, Math.Max(0, b.Width - glyph.Width - 3), b.Height);
        var flags = TextFormatFlags.VerticalCenter | TextFormatFlags.Left | TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis;
        if (!keyboardCues) flags |= TextFormatFlags.HidePrefix;
        TextRenderer.DrawText(g, b.Text, b.Font, textRect, b.Enabled ? b.ForeColor : t.Muted, flags);
        if (b.Focused && focusCues)
        {
            Size ts = TextRenderer.MeasureText(g, b.Text, b.Font, Size.Empty, flags);
            ControlPaint.DrawFocusRectangle(g, new Rectangle(textRect.X - 1, (b.Height - ts.Height) / 2, Math.Min(ts.Width + 2, textRect.Width), ts.Height));
        }
    }
}

internal sealed class ThemedCheckBox : CheckBox
{
    private bool _hot, _pressed;

    public ThemedCheckBox() => SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);

    protected override void OnPaint(PaintEventArgs e) => ToggleDraw.Paint(this, e.Graphics, radio: false, Checked, _hot, _pressed, ShowKeyboardCues, ShowFocusCues);
    protected override void OnMouseEnter(EventArgs e) { _hot = true; Invalidate(); base.OnMouseEnter(e); }
    protected override void OnMouseLeave(EventArgs e) { _hot = false; _pressed = false; Invalidate(); base.OnMouseLeave(e); }
    protected override void OnMouseDown(MouseEventArgs e) { _pressed = true; Invalidate(); base.OnMouseDown(e); }
    protected override void OnMouseUp(MouseEventArgs e) { _pressed = false; Invalidate(); base.OnMouseUp(e); }
    protected override void OnEnabledChanged(EventArgs e) { Invalidate(); base.OnEnabledChanged(e); }
}

internal sealed class ThemedRadioButton : RadioButton
{
    private bool _hot, _pressed;

    public ThemedRadioButton() => SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);

    protected override void OnPaint(PaintEventArgs e) => ToggleDraw.Paint(this, e.Graphics, radio: true, Checked, _hot, _pressed, ShowKeyboardCues, ShowFocusCues);
    protected override void OnMouseEnter(EventArgs e) { _hot = true; Invalidate(); base.OnMouseEnter(e); }
    protected override void OnMouseLeave(EventArgs e) { _hot = false; _pressed = false; Invalidate(); base.OnMouseLeave(e); }
    protected override void OnMouseDown(MouseEventArgs e) { _pressed = true; Invalidate(); base.OnMouseDown(e); }
    protected override void OnMouseUp(MouseEventArgs e) { _pressed = false; Invalidate(); base.OnMouseUp(e); }
    protected override void OnEnabledChanged(EventArgs e) { Invalidate(); base.OnEnabledChanged(e); }
}

/// tab の見出しと枠を theme の色で描く。中身の page はそれぞれが描く。
internal sealed class ThemedTabControl : TabControl
{
    public ThemedTabControl() =>
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        var t = Theme.Current;
        using (var back = new SolidBrush(Parent?.BackColor ?? t.Window)) g.FillRectangle(back, ClientRectangle);
        using var pen = new Pen(t.Line);

        Rectangle body = DisplayRectangle;
        body.Inflate(2, 2);
        using (var page = new SolidBrush(t.Window)) g.FillRectangle(page, body);
        g.DrawRectangle(pen, body.X, body.Y, body.Width - 1, body.Height - 1);

        for (int i = 0; i < TabCount; i++)
        {
            Rectangle r = GetTabRect(i);
            bool selected = i == SelectedIndex;
            if (selected) r = Rectangle.FromLTRB(r.Left - 1, r.Top - 2, r.Right + 1, body.Top + 1);
            using (var fill = new SolidBrush(selected ? t.Window : Theme.Blend(t.Window, t.Text, t.Dark ? 0.10 : 0.06)))
                g.FillRectangle(fill, r);
            g.DrawLine(pen, r.Left, r.Bottom - 1, r.Left, r.Top);
            g.DrawLine(pen, r.Left, r.Top, r.Right - 1, r.Top);
            g.DrawLine(pen, r.Right - 1, r.Top, r.Right - 1, r.Bottom - 1);
            if (!selected) g.DrawLine(pen, r.Left, r.Bottom - 1, r.Right - 1, r.Bottom - 1);
            TextRenderer.DrawText(g, TabPages[i].Text, Font, r, t.Text,
                TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine);
        }
    }

    protected override void OnSelectedIndexChanged(EventArgs e) { Invalidate(); base.OnSelectedIndexChanged(e); }
}

internal sealed class ThemedProgressBar : ProgressBar
{
    public ThemedProgressBar() => SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        var t = Theme.Current;
        var r = ClientRectangle;
        using (var track = new SolidBrush(t.Input)) g.FillRectangle(track, r);
        int span = Math.Max(1, Maximum - Minimum);
        int w = (int)((long)(r.Width - 2) * Math.Clamp(Value - Minimum, 0, span) / span);
        if (w > 0)
        {
            var fill = new Rectangle(r.X + 1, r.Y + 1, w, r.Height - 2);
            using var b = new SolidBrush(t.Progress);
            g.FillRectangle(b, fill);
        }
        using var pen = new Pen(t.Line);
        g.DrawRectangle(pen, r.X, r.Y, r.Width - 1, r.Height - 1);
    }
}

internal static class DarkScroll
{
    /// scroll bar などを Windows の暗い見た目にする (暗い theme のときだけ)。
    public static void Apply(Control c, bool dark)
    {
        if (!c.IsHandleCreated) { c.HandleCreated += (_, _) => Apply(c, Theme.Current.Dark); return; }
        try { SetWindowTheme(c.Handle, dark ? "DarkMode_Explorer" : null, null); c.Invalidate(); }
        catch (DllNotFoundException) { }
        catch (EntryPointNotFoundException) { }
    }

    [DllImport("uxtheme.dll", CharSet = CharSet.Unicode)]
    private static extern int SetWindowTheme(IntPtr hwnd, string? appName, string? idList);
}

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
        const int WM_NCCALCSIZE = 0x0083, WM_NCPAINT = 0x0085, WM_PAINT = 0x000F;
        if (_frame && m.Msg == WM_NCCALCSIZE)
        {
            base.WndProc(ref m);
            var r = Marshal.PtrToStructure<RECT>(m.LParam);
            r.B -= 1;
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
        GetWindowRect(Handle, out RECT wr);
        int w = wr.R - wr.L, h = wr.B - wr.T;
        IntPtr dc = GetWindowDC(Handle);
        if (dc == IntPtr.Zero) return;
        try
        {
            using var g = Graphics.FromHdc(dc);
            var t = Theme.Current;
            using (var back = new SolidBrush(_control.BackColor)) g.FillRectangle(back, 1, h - 2, w - 2, 1);
            using (var edge = new Pen(_control.Enabled ? t.Line : Theme.Blend(t.Window, t.Text, 0.25))) g.DrawRectangle(edge, 0, 0, w - 1, h - 1);
            if (Focused)
                using (var accent = new SolidBrush(t.InputFocus)) g.FillRectangle(accent, 0, h - 2, w, 2);
        }
        finally { ReleaseDC(Handle, dc); }
    }

    private void PaintLine()
    {
        GetWindowRect(Handle, out RECT wr);
        int w = wr.R - wr.L, h = wr.B - wr.T;
        IntPtr dc = GetWindowDC(Handle);
        if (dc == IntPtr.Zero) return;
        try
        {
            using var g = Graphics.FromHdc(dc);
            using var accent = new SolidBrush(Theme.Current.InputFocus);
            g.FillRectangle(accent, 0, h - 2, w, 2);
        }
        finally { ReleaseDC(Handle, dc); }
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct RECT { public int L, T, R, B; }

    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] private static extern IntPtr GetWindowDC(IntPtr h);
    [DllImport("user32.dll")] private static extern int ReleaseDC(IntPtr h, IntPtr dc);
    [DllImport("user32.dll")] private static extern bool RedrawWindow(IntPtr h, IntPtr r, IntPtr rgn, uint flags);
    [DllImport("user32.dll")] private static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
}
