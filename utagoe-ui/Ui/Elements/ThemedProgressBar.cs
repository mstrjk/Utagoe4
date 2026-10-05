using Utagoe.App;

namespace Utagoe.Ui;

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
