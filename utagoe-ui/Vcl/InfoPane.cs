// 最大化したときの右側の説明欄 (v4)。最初は歓迎の文、見出しの link に mouse を乗せるとその説明に替わり、
// 別の link に乗せるまでそのまま残る。文は 1 行目を太字の見出し、残りを本文として折り返して描く。
// 本文の中の __ で囲んだ部分は link と同じ青い下線で描く (歓迎の文の「blue and underlined」)。

using App = Utagoe.App;

namespace Utagoe.Vcl;

internal sealed class InfoPane : Control
{
    private string _text = "";
    private readonly VScrollBar _bar = new() { Dock = DockStyle.Right, Visible = false };

    public InfoPane()
    {
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                 ControlStyles.ResizeRedraw | ControlStyles.SupportsTransparentBackColor, true);
        BackColor = Color.Transparent;
        _bar.Width = SystemInformation.VerticalScrollBarWidth;
        _bar.Scroll += (_, _) => Invalidate();
        _bar.HandleCreated += (_, _) => DarkScroll.Apply(_bar, App.Theme.Current.Dark);
        Controls.Add(_bar);
        void Rethemed() { DarkScroll.Apply(_bar, App.Theme.Current.Dark); Invalidate(); }
        App.Theme.Changed += Rethemed;
        Disposed += (_, _) => App.Theme.Changed -= Rethemed;
    }

    protected override void OnMouseWheel(MouseEventArgs e)
    {
        if (_bar.Visible)
        {
            int max = Math.Max(0, _bar.Maximum - _bar.LargeChange + 1);
            _bar.Value = Math.Clamp(_bar.Value - Math.Sign(e.Delta) * _bar.SmallChange * 3, 0, max);
            Invalidate();
        }
        base.OnMouseWheel(e);
    }

    public void ShowText(string text)
    {
        if (text == _text) return;
        _text = text;
        _bar.Value = 0;
        Invalidate();
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        var t = App.Theme.Current;
        int nl = _text.IndexOf('\n');
        string title = nl < 0 ? _text : _text[..nl];
        string body = nl < 0 ? "" : _text[(nl + 1)..];

        using var bold = new Font(Font.FontFamily, Font.Size * 1.15f, FontStyle.Bold, Font.Unit);
        using var link = new Font(Font, FontStyle.Underline);
        int width = Math.Max(10, Width - (_bar.Visible ? _bar.Width + 4 : 0));
        int top = _bar.Visible ? -_bar.Value : 0;
        Size ts = TextRenderer.MeasureText(g, title, bold, new Size(width, int.MaxValue), TextFormatFlags.WordBreak);
        TextRenderer.DrawText(g, title, bold, new Rectangle(0, top, width, ts.Height), t.Text, TextFormatFlags.WordBreak);
        int y = ts.Height + Font.Height / 2;

        // 本文を段落ごとに、語単位で折り返して描く。__ で囲んだ語は link の色と下線。
        int line = Font.Height + 2, space = TextRenderer.MeasureText(g, " ", Font, Size.Empty, TextFormatFlags.NoPadding).Width;
        foreach (string para in body.Split('\n'))
        {
            if (para.Length == 0) { y += line / 2; continue; }
            int x = 0;
            bool inLink = false;
            foreach (string piece in para.Split("__"))
            {
                foreach (string word in piece.Split(' ', StringSplitOptions.RemoveEmptyEntries))
                {
                    Font f = inLink ? link : Font;
                    int w = TextRenderer.MeasureText(g, word, f, Size.Empty, TextFormatFlags.NoPadding).Width;
                    if (x > 0 && x + w > width) { x = 0; y += line; }
                    TextRenderer.DrawText(g, word, f, new Point(x, y + top), inLink ? t.Link : t.Text, TextFormatFlags.NoPadding);
                    x += w + space;
                }
                inLink = !inLink;
            }
            y += line;
        }

        bool overflow = y > Height;
        if (overflow != _bar.Visible)
        {
            _bar.Visible = overflow;
            if (!overflow) _bar.Value = 0;
            Invalidate();
        }
        if (overflow)
        {
            _bar.Minimum = 0;
            _bar.LargeChange = Math.Max(1, Height);
            _bar.SmallChange = Math.Max(1, line);
            _bar.Maximum = Math.Max(0, y - 1);
            if (_bar.Value > y - Height) _bar.Value = Math.Max(0, y - Height);
            return;
        }

        var logo = VclGlyph.ThemeLogo();
        int lw = (int)Math.Min(Width * 0.5f, logo.Width * Font.Height / 20f);
        int lh = lw * logo.Height / Math.Max(1, logo.Width);
        if (lw > 20 && Height - lh >= y + line)
        {
            g.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.HighQualityBicubic;
            g.PixelOffsetMode = System.Drawing.Drawing2D.PixelOffsetMode.HighQuality;
            g.DrawImage(logo, new Rectangle(Width - lw, Height - lh, lw, lh));
        }
    }
}
