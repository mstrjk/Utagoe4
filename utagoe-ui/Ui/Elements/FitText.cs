using System.Windows.Forms.VisualStyles;
using Utagoe.App;

namespace Utagoe.Ui;

internal static class FitText
{
    public static Font? Shrink(Graphics g, string text, Font font, int width, TextFormatFlags flags)
    {
        if (string.IsNullOrEmpty(text) || width <= 0) return null;
        flags &= ~(TextFormatFlags.EndEllipsis | TextFormatFlags.WordBreak);
        if (TextRenderer.MeasureText(g, text, font, Size.Empty, flags).Width <= width) return null;
        float min = font.Size * 0.7f;
        for (float size = font.Size - 0.5f; size >= min; size -= 0.5f)
        {
            var f = new Font(font.FontFamily, size, font.Style, font.Unit);
            if (TextRenderer.MeasureText(g, text, f, Size.Empty, flags).Width <= width || size - 0.5f < min) return f;
            f.Dispose();
        }
        return null;
    }
}
