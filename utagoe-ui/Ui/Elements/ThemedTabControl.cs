using System.Runtime.InteropServices;
using System.Windows.Forms.VisualStyles;
using Utagoe.App;

using static Utagoe.Ui.Win32;

namespace Utagoe.Ui;

/// tab の見出しと枠を theme の色で描く。中身の page はそれぞれが描く。
internal sealed class ThemedTabControl : TabControl
{
    public ThemedTabControl() =>
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);

    protected override void OnPaint(PaintEventArgs e)
    {
        FixItemSize();
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

    private Transition? _switch;

    protected override void OnSelecting(TabControlCancelEventArgs e)
    {
        base.OnSelecting(e);
        if (e.Cancel || !IsHandleCreated || !Visible || TopLevelControl is not Form top) return;
        _switch = Transition.Begin(new[] { top });
    }

    protected override void OnSelectedIndexChanged(EventArgs e)
    {
        Invalidate();
        base.OnSelectedIndexChanged(e);
        _switch?.End(fade: false);
        _switch = null;
    }


    public override Rectangle DisplayRectangle
    {
        get
        {
            Rectangle r = base.DisplayRectangle;
            if (!Multiline || !IsHandleCreated || TabCount == 0) return r;
            int tabsBottom = 0;
            for (int i = 0; i < TabCount; i++) tabsBottom = Math.Max(tabsBottom, GetTabRect(i).Bottom);
            int top = tabsBottom + 1;
            return Rectangle.FromLTRB(r.Left, top, r.Right, r.Bottom);
        }
    }

    private bool _fitQueued;
    private IntPtr _hfont;
    private Font? _hfontSource;

    protected override void OnHandleCreated(EventArgs e) { base.OnHandleCreated(e); _fitQueued = false; FixItemSize(); QueueFit(); }
    protected override void OnSizeChanged(EventArgs e) { base.OnSizeChanged(e); QueueFit(); }
    protected override void OnFontChanged(EventArgs e) { base.OnFontChanged(e); FixItemSize(); QueueFit(); }

    public void Refit()
    {
        FixItemSize();
        QueueFit();
        Invalidate();
    }

    private void FixItemSize()
    {
        if (Multiline || TabCount == 0) return;
        int w = 0;
        foreach (TabPage p in TabPages) w = Math.Max(w, TextRenderer.MeasureText(p.Text, Font).Width);
        var size = new Size(w + Font.Height, Font.Height * 11 / 7);
        if (SizeMode != TabSizeMode.Fixed) SizeMode = TabSizeMode.Fixed;
        if (ItemSize != size) ItemSize = size;
    }

    protected override void Dispose(bool disposing)
    {
        if (_hfont != IntPtr.Zero) { DeleteObject(_hfont); _hfont = IntPtr.Zero; }
        base.Dispose(disposing);
    }

    private int _minRows = 1;

    [System.ComponentModel.DesignerSerializationVisibility(System.ComponentModel.DesignerSerializationVisibility.Hidden)]
    public int MinRows
    {
        get => _minRows;
        set
        {
            if (_minRows == value) return;
            _minRows = value;
            QueueFit();
        }
    }

    private void SetTabPadding(int x)
    {
        SendMessage(Handle, TCM_SETPADDING, IntPtr.Zero, (IntPtr)((Padding.Y << 16) | (x & 0xFFFF)));
        IntPtr old = _hfont;
        _hfont = Font.ToHfont();
        SendMessage(Handle, WM_SETFONT, _hfont, (IntPtr)1);
        if (old != IntPtr.Zero) DeleteObject(old);
    }


    private void QueueFit()
    {
        if (!Multiline || !IsHandleCreated || _fitQueued) return;
        _fitQueued = true;
        BeginInvoke(() =>
        {
            _fitQueued = false;
            if (IsDisposed) return;
            if (!ReferenceEquals(_hfontSource, Font))
            {
                IntPtr old = _hfont;
                _hfont = Font.ToHfont();
                _hfontSource = Font;
                if (old != IntPtr.Zero) DeleteObject(old);
            }
            int pad = Padding.X;
            SetTabPadding(pad);
            while (RowCount < MinRows && pad < 400)
                SetTabPadding(pad += 2);
            Rectangle page = DisplayRectangle;
            foreach (TabPage p in TabPages)
                if (p.Bounds != page) p.Bounds = page;
            Invalidate();
        });
    }
}
