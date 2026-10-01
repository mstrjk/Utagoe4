// 最大化したときの右側の説明欄 (v4)。最初は歓迎の文、見出しの link に mouse を乗せるとその説明に替わり、
// 別の link に乗せるまでそのまま残る。文は 1 行目を太字の見出し、残りを本文として折り返して描く。
// 本文の中の __ で囲んだ部分は link と同じ青い下線で描く (歓迎の文の「blue and underlined」)。

using App = Utagoe.App;

namespace Utagoe.Vcl;

internal sealed class InfoPane : Control
{
    private string _text = "";

    public InfoPane()
    {
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                 ControlStyles.ResizeRedraw | ControlStyles.SupportsTransparentBackColor, true);
        BackColor = Color.Transparent;
        App.Theme.Changed += Invalidate;
        Disposed += (_, _) => App.Theme.Changed -= Invalidate;
    }

    public void ShowText(string text)
    {
        if (text == _text) return;
        _text = text;
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
        int width = Math.Max(10, Width);
        Size ts = TextRenderer.MeasureText(g, title, bold, new Size(width, int.MaxValue), TextFormatFlags.WordBreak);
        TextRenderer.DrawText(g, title, bold, new Rectangle(0, 0, width, ts.Height), t.Text, TextFormatFlags.WordBreak);
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
                    TextRenderer.DrawText(g, word, f, new Point(x, y), inLink ? t.Link : t.Text, TextFormatFlags.NoPadding);
                    x += w + space;
                }
                inLink = !inLink;
            }
            y += line;
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
