// VCL と同じく caption を 1 行のまま描く GroupBox。
// WinForms は狭い caption を wrap するが、VCL は 1 行で clip する。背景透過も維持したいので frame を自前描画する。
// v4: IsLink なら caption を青い下線付きで描く (mouse を乗せると説明が出る。HelpLinks)。灰色のときも link の色は保つ。

using System.ComponentModel;

namespace Utagoe.Vcl;

internal class VclGroupBox : GroupBox
{
    private bool _isLink;

    [DefaultValue(false)]
    public bool IsLink
    {
        get => _isLink;
        set { _isLink = value; Invalidate(); }
    }

    // v4: 枠と見出しは theme の色で自前に描く (Windows の枠は暗い theme で白く浮くため)。
    // 見出しは link でなければ普通の文字色。
    protected override void OnPaint(PaintEventArgs e)
    {
        var t = App.Theme.Current;
        Color color = _isLink ? (Enabled ? t.Link : t.DisabledLink)
                    : Enabled ? t.Text : t.Muted;
        using var underline = _isLink ? new Font(Font, Font.Style | FontStyle.Underline) : null;
        using (var back = new SolidBrush(BackColor)) e.Graphics.FillRectangle(back, ClientRectangle);
        DrawFrame(e.Graphics, ClientRectangle, Text, underline ?? Font, color, BackColor);
    }

    /// group の枠と見出しを描く。最大化したときに main window の中身を囲む枠もこれで描く。
    public static void DrawFrame(Graphics g, Rectangle r, string text, Font font, Color textColor, Color back)
    {
        var t = App.Theme.Current;
        var flags = TextFormatFlags.SingleLine | TextFormatFlags.Left | TextFormatFlags.NoPrefix | TextFormatFlags.NoPadding;
        Size caption = text.Length > 0 ? TextRenderer.MeasureText(g, text, font, Size.Empty, flags) : Size.Empty;
        int top = Math.Max(caption.Height, font.Height) / 2;
        using (var pen = new Pen(t.Line))
            g.DrawRectangle(pen, r.X, r.Y + top, r.Width - 1, r.Height - top - 1);
        if (caption.Width > 0)
        {
            int x = r.X + 7;
            using (var b = new SolidBrush(back)) g.FillRectangle(b, x - 2, r.Y, caption.Width + 4, caption.Height);
            TextRenderer.DrawText(g, text, font, new Point(x, r.Y), textColor, flags);
        }
    }
}
