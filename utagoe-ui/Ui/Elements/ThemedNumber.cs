using Utagoe.App;

namespace Utagoe.Ui;

internal class ThemedNumber : NumericUpDown
{
    private const int WM_NCPAINT = 0x0085;
    private ButtonsPainter? _buttons;

    protected override void OnHandleCreated(EventArgs e)
    {
        base.OnHandleCreated(e);
        if (_buttons == null && Controls.Count > 0) _buttons = new ButtonsPainter(this, Controls[0]);
    }

    protected override void OnEnabledChanged(EventArgs e)
    {
        base.OnEnabledChanged(e);
        Invalidate(true);
        if (IsHandleCreated) Win32.RedrawWindow(Handle, IntPtr.Zero, IntPtr.Zero, 0x0400 | 0x0001 | 0x0080);
    }

    internal static Color Edge(bool enabled)
    {
        var t = Theme.Current;
        return enabled ? t.Line : Theme.Blend(t.Window, t.Text, 0.25);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        var t = Theme.Current;
        using (var back = new Pen(t.Input)) e.Graphics.DrawRectangle(back, 1, 1, Width - 3, Height - 3);
        using var pen = new Pen(Edge(Enabled));
        e.Graphics.DrawRectangle(pen, 0, 0, Width - 1, Height - 1);
    }

    protected override void WndProc(ref Message m)
    {
        base.WndProc(ref m);
        if (m.Msg == WM_NCPAINT && BorderStyle == BorderStyle.FixedSingle && IsHandleCreated)
        {
            IntPtr dc = Win32.GetWindowDC(Handle);
            try
            {
                using var g = Graphics.FromHdc(dc);
                using var pen = new Pen(Edge(Enabled));
                g.DrawRectangle(pen, 0, 0, Width - 1, Height - 1);
            }
            finally { Win32.ReleaseDC(Handle, dc); }
        }
    }

    private sealed class ButtonsPainter : NativeWindow
    {
        private readonly ThemedNumber _owner;
        private readonly Control _buttons;

        public ButtonsPainter(ThemedNumber owner, Control buttons)
        {
            _owner = owner;
            _buttons = buttons;
            if (buttons.IsHandleCreated) AssignHandle(buttons.Handle);
            buttons.HandleCreated += (_, _) => AssignHandle(buttons.Handle);
            buttons.HandleDestroyed += (_, _) => ReleaseHandle();
        }

        protected override void WndProc(ref Message m)
        {
            if (m.Msg == Win32.WM_PAINT)
            {
                IntPtr dc = Win32.BeginPaint(Handle, out var ps);
                try
                {
                    var r = _buttons.ClientRectangle;
                    using var buffer = BufferedGraphicsManager.Current.Allocate(dc, r);
                    Paint(buffer.Graphics, r);
                    buffer.Render();
                }
                finally { Win32.EndPaint(Handle, ref ps); }
                m.Result = IntPtr.Zero;
                return;
            }
            if (m.Msg is 0x0317 or 0x0318)
            {
                using (var g = Graphics.FromHdc(m.WParam)) Paint(g, _buttons.ClientRectangle);
                m.Result = IntPtr.Zero;
                return;
            }
            if (m.Msg == 0x0014)
            {
                m.Result = (IntPtr)1;
                return;
            }
            base.WndProc(ref m);
        }

        private void Paint(Graphics g, Rectangle r)
        {
            var t = Theme.Current;
            bool on = _owner.Enabled;
            using (var back = new SolidBrush(on ? t.Input : t.Window)) g.FillRectangle(back, r);
            using (var edge = new Pen(Edge(on)))
            {
                g.DrawLine(edge, 0, 0, 0, r.Height);
                g.DrawLine(edge, 0, r.Height / 2, r.Width, r.Height / 2);
            }
            int rows = Math.Max(2, _owner.LogicalToDeviceUnits(2)) + 1;
            int cx = (r.Width + 1) / 2;
            int half = r.Height / 2;
            int upTop = (half - rows) / 2, downTop = half + (r.Height - half - rows) / 2;
            using var arrow = new SolidBrush(on ? t.InputText : t.Muted);
            for (int i = 0; i < rows; ++i)
            {
                int upWidth = i, downWidth = rows - 1 - i;
                g.FillRectangle(arrow, cx - upWidth, upTop + i, upWidth * 2 + 1, 1);
                g.FillRectangle(arrow, cx - downWidth, downTop + i, downWidth * 2 + 1, 1);
            }
        }
    }
}
