using System.Drawing.Drawing2D;
using System.Windows.Forms.VisualStyles;
using Utagoe.App;

namespace Utagoe.Ui;

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
        int visible = b.Parent != null ? Math.Min(textRect.Width, b.Parent.ClientSize.Width - b.Left - textRect.X) : textRect.Width;
        using var fit = b.AutoSize ? null : FitText.Shrink(g, b.Text, b.Font, visible, flags);
        TextRenderer.DrawText(g, b.Text, fit ?? b.Font, textRect, b.Enabled ? b.ForeColor : t.Muted, flags);
        if (b.Focused && focusCues)
        {
            Size ts = TextRenderer.MeasureText(g, b.Text, b.Font, Size.Empty, flags);
            ControlPaint.DrawFocusRectangle(g, new Rectangle(textRect.X - 1, (b.Height - ts.Height) / 2, Math.Min(ts.Width + 2, textRect.Width), ts.Height));
        }
    }
}
