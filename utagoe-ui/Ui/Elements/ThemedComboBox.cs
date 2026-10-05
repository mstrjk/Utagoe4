using Utagoe.App;

namespace Utagoe.Ui;

internal class ThemedComboBox : ComboBox
{
    private IntPtr _editBrush;
    private Color _editBrushColor;

    protected override void Dispose(bool disposing)
    {
        if (_editBrush != IntPtr.Zero) Win32.DeleteObject(_editBrush);
        _editBrush = IntPtr.Zero;
        base.Dispose(disposing);
    }

    protected override void WndProc(ref Message m)
    {
        const int WM_PRINT = 0x0317, WM_PRINTCLIENT = 0x0318, WM_CTLCOLOREDIT = 0x0133, WM_CTLCOLORSTATIC = 0x0138;
        if ((m.Msg == WM_CTLCOLOREDIT || m.Msg == WM_CTLCOLORSTATIC) && DropDownStyle != ComboBoxStyle.DropDownList)
        {
            var t = Theme.Current;
            if (_editBrush == IntPtr.Zero || _editBrushColor != t.Input)
            {
                if (_editBrush != IntPtr.Zero) Win32.DeleteObject(_editBrush);
                _editBrush = Win32.CreateSolidBrush(ColorTranslator.ToWin32(t.Input));
                _editBrushColor = t.Input;
            }
            Win32.SetTextColor(m.WParam, ColorTranslator.ToWin32(Enabled ? t.InputText : SystemColors.GrayText));
            Win32.SetBkColor(m.WParam, ColorTranslator.ToWin32(t.Input));
            m.Result = _editBrush;
            return;
        }
        if ((m.Msg == WM_PRINT || m.Msg == WM_PRINTCLIENT) && FlatStyle == FlatStyle.Flat && IsHandleCreated)
        {
            if (DropDownStyle != ComboBoxStyle.DropDownList) base.WndProc(ref m);
            using var g = Graphics.FromHdc(m.WParam);
            if (DropDownStyle == ComboBoxStyle.DropDownList) PaintWhole(g);
            else PaintChrome(g);
            m.Result = IntPtr.Zero;
            return;
        }
        if (m.Msg == Win32.WM_PAINT && FlatStyle == FlatStyle.Flat && DropDownStyle == ComboBoxStyle.DropDownList && IsHandleCreated)
        {
            IntPtr dc = Win32.BeginPaint(Handle, out var ps);
            try
            {
                using var buffer = BufferedGraphicsManager.Current.Allocate(dc, ClientRectangle);
                PaintWhole(buffer.Graphics);
                buffer.Render();
            }
            finally { Win32.EndPaint(Handle, ref ps); }
            m.Result = IntPtr.Zero;
            return;
        }
        base.WndProc(ref m);
        if (m.Msg == Win32.WM_PAINT && FlatStyle == FlatStyle.Flat && IsHandleCreated)
        {
            using var g = Graphics.FromHwnd(Handle);
            PaintChrome(g);
        }
    }

    protected override void OnEnabledChanged(EventArgs e) { base.OnEnabledChanged(e); Invalidate(); }
    protected override void OnGotFocus(EventArgs e) { base.OnGotFocus(e); Invalidate(); }
    protected override void OnLostFocus(EventArgs e) { base.OnLostFocus(e); Invalidate(); }
    protected override void OnSelectedIndexChanged(EventArgs e) { base.OnSelectedIndexChanged(e); Invalidate(); }
    protected override void OnDropDownClosed(EventArgs e) { base.OnDropDownClosed(e); Invalidate(); }

    private Rectangle Button
    {
        get
        {
            int buttonWidth = SystemInformation.GetHorizontalScrollBarArrowWidthForDpi(DeviceDpi);
            return Rectangle.FromLTRB(Width - buttonWidth - 1, 1, Width - 1, Height - 1);
        }
    }

    private void PaintWhole(Graphics g)
    {
        var t = Theme.Current;
        using (var back = new SolidBrush(t.Input)) g.FillRectangle(back, ClientRectangle);
        var text = Rectangle.FromLTRB(3, 3, Button.Left - 2, Height - 3);
        Color color = Enabled ? t.InputText : SystemColors.GrayText;
        if (Focused && !DroppedDown)
        {
            g.FillRectangle(SystemBrushes.Highlight, text);
            color = SystemColors.HighlightText;
        }
        string value = (SelectedIndex >= 0 ? GetItemText(SelectedItem) : Text) ?? "";
        TextRenderer.DrawText(g, value, Font, Rectangle.FromLTRB(text.Left - 2, text.Top, text.Right, text.Bottom), color,
            TextFormatFlags.VerticalCenter | TextFormatFlags.Left | TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis | TextFormatFlags.NoPrefix);
        PaintChrome(g);
    }

    private void PaintChrome(Graphics g)
    {
        var t = Theme.Current;
        var outer = new Rectangle(0, 0, Width - 1, Height - 1);
        var button = Button;
        using (var back = new Pen(t.Input))
        {
            g.DrawRectangle(back, 1, 1, Width - 3, Height - 3);
            g.DrawRectangle(back, 2, 2, Width - 5, Height - 5);
        }
        if (DropDownStyle != ComboBoxStyle.DropDownList && IsHandleCreated)
        {
            var info = new Win32.COMBOBOXINFO { cbSize = System.Runtime.InteropServices.Marshal.SizeOf<Win32.COMBOBOXINFO>() };
            if (Win32.GetComboBoxInfo(Handle, ref info) && info.rcItem.Right < button.Left)
                using (var gap = new SolidBrush(t.Input))
                    g.FillRectangle(gap, Rectangle.FromLTRB(info.rcItem.Right, 1, button.Left, Height - 1));
        }
        using (var fill = new SolidBrush(Enabled ? t.Input : t.Window)) g.FillRectangle(fill, button);
        using (var edge = new Pen(Enabled ? t.Line : Theme.Blend(t.Window, t.Text, 0.25)))
        {
            g.DrawRectangle(edge, outer);
            g.DrawLine(edge, button.Left, button.Top, button.Left, button.Bottom - 1);
        }
        int cx = button.Left + button.Width / 2, cy = button.Top + button.Height / 2;
        int s = Math.Max(2, LogicalToDeviceUnits(2));
        using var arrow = new SolidBrush(Enabled ? t.InputText : t.Muted);
        g.FillPolygon(arrow, new[] { new Point(cx - s - 1, cy - s / 2), new Point(cx + s + 1, cy - s / 2), new Point(cx, cy + s / 2 + 1) });
    }
}
