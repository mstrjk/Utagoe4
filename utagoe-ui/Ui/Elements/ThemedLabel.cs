using System.Windows.Forms.VisualStyles;
using ContentAlignment = System.Drawing.ContentAlignment;
using Utagoe.App;

namespace Utagoe.Ui;

/// 無効のときも theme の淡い文字色で読めるように描く label。
internal class ThemedLabel : Label
{
    private VScrollBar? _bar;
    private Font? _fit;
    private int _need;

    protected override void OnPaint(PaintEventArgs e)
    {
        if (AutoSize)
        {
            if (Enabled) { base.OnPaint(e); return; }
            TextRenderer.DrawText(e.Graphics, Text, Font, ClientRectangle, Theme.Current.Muted, Flags(TextAlign, AutoSize));
            return;
        }
        var flags = Flags(TextAlign, false) | TextFormatFlags.TextBoxControl;
        if (!UseMnemonic) flags |= TextFormatFlags.NoPrefix;
        if (AutoEllipsis && _bar is not { Visible: true }) flags |= TextFormatFlags.EndEllipsis;
        int width = TextWidth;
        int offset = _bar is { Visible: true } ? _bar.Value : 0;
        var rect = new Rectangle(0, -offset, width, Math.Max(ClientSize.Height, _need));
        TextRenderer.DrawText(e.Graphics, Text, _fit ?? Font, rect, Enabled ? ForeColor : Theme.Current.Muted, flags);
    }

    private int TextWidth => Math.Max(1, ClientSize.Width - (_bar is { Visible: true } ? _bar.Width + 2 : 0));

    private void Reflow()
    {
        if (IsDisposed) return;
        _fit?.Dispose();
        _fit = null;
        if (_bar != null) _bar.Visible = false;
        if (AutoSize) { Invalidate(); return; }
        if (string.IsNullOrEmpty(Text) || ClientSize.Width <= 0 || ClientSize.Height <= 0) { Invalidate(); return; }
        var flags = TextFormatFlags.WordBreak | TextFormatFlags.TextBoxControl | (UseMnemonic ? 0 : TextFormatFlags.NoPrefix);
        int Measure(Font f, int w) => TextRenderer.MeasureText(Text, f, new Size(w, int.MaxValue), flags).Height;
        _need = Measure(Font, ClientSize.Width);
        if (_need > ClientSize.Height)
        {
            for (float size = Font.Size - 0.5f; size >= Font.Size * 0.85f; size -= 0.5f)
            {
                var f = new Font(Font.FontFamily, size, Font.Style, Font.Unit);
                int h = Measure(f, ClientSize.Width);
                if (h <= ClientSize.Height) { _fit = f; _need = h; break; }
                f.Dispose();
            }
        }
        if (_fit == null && _need > ClientSize.Height)
        {
            if (_bar == null)
            {
                _bar = new VScrollBar { Dock = DockStyle.Right, Width = SystemInformation.VerticalScrollBarWidth };
                _bar.Scroll += (_, _) => Invalidate();
                _bar.HandleCreated += (_, _) => DarkScroll.Apply(_bar, Theme.Current.Dark);
                Controls.Add(_bar);
            }
            _bar.Visible = true;
            _need = Measure(Font, TextWidth);
            _bar.Minimum = 0;
            _bar.LargeChange = Math.Max(1, ClientSize.Height);
            _bar.SmallChange = Math.Max(1, Font.Height);
            _bar.Maximum = Math.Max(0, _need - 1);
            _bar.Value = Math.Min(_bar.Value, Math.Max(0, _need - ClientSize.Height));
        }
        Invalidate();
    }

    protected override void OnMouseWheel(MouseEventArgs e)
    {
        if (_bar is { Visible: true })
        {
            int max = Math.Max(0, _bar.Maximum - _bar.LargeChange + 1);
            _bar.Value = Math.Clamp(_bar.Value - Math.Sign(e.Delta) * _bar.SmallChange * 2, 0, max);
            Invalidate();
        }
        base.OnMouseWheel(e);
    }

    protected override void OnTextChanged(EventArgs e) { base.OnTextChanged(e); Reflow(); }
    protected override void OnAutoSizeChanged(EventArgs e) { base.OnAutoSizeChanged(e); Reflow(); }
    protected override void OnResize(EventArgs e) { base.OnResize(e); Reflow(); }
    protected override void OnFontChanged(EventArgs e) { base.OnFontChanged(e); Reflow(); }

    protected override void Dispose(bool disposing)
    {
        if (disposing) _fit?.Dispose();
        base.Dispose(disposing);
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
