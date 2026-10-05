// VCL の TBevel と 2px raised divider を WinForms 上で再現する。
// TBevel は独立 window を持たず親へ直接描く。BevelPainter も parent の Paint で描いて同じ重なり方にする。
// bsLowered は top/left が shadow、bottom/right が highlight。

namespace Utagoe.Ui;

internal enum BevelShape { Box, TopLine, BottomLine, LeftLine }

internal readonly record struct Bevel(int Left, int Top, int Width, int Height, BevelShape Shape = BevelShape.Box);

internal static class BevelPainter
{
    private static readonly Dictionary<Control, Bevel[]> Attached = new();

    public static void Attach(Control parent, params Bevel[] bevels)
    {
        Attached[parent] = bevels;
        parent.Paint += (_, e) =>
        {
            using var shadow = new Pen(App.Theme.Current.BevelShadow);
            using var light  = new Pen(App.Theme.Current.BevelLight);
            foreach (var b in Attached[parent]) Draw(e.Graphics, b, shadow, light);
        };
        parent.Disposed += (_, _) => Attached.Remove(parent);
    }

    /// VCL の ChangeScale と同様に親の bevel を edge 単位で scale する。
    public static void Scale(Control parent, int m, int d)
    {
        if (!Attached.TryGetValue(parent, out var bevels)) return;
        Attached[parent] = bevels.Select(b =>
        {
            int l = VclScaling.MulDiv(b.Left, m, d), t = VclScaling.MulDiv(b.Top, m, d);
            int r = VclScaling.MulDiv(b.Left + b.Width, m, d), bt = VclScaling.MulDiv(b.Top + b.Height, m, d);
            return b with { Left = l, Top = t, Width = r - l, Height = bt - t };
        }).ToArray();
    }

    /// 付けた bevel を置き換える (大きさが変わる dialog 用)。
    public static void Replace(Control parent, params Bevel[] bevels)
    {
        if (!Attached.ContainsKey(parent)) { Attach(parent, bevels); return; }
        Attached[parent] = bevels;
        parent.Invalidate();
    }

    private static readonly Dictionary<Control, Bevel[]> Remembered = new();

    /// 今の bevel を倍率 1 として覚える (UiZoom 用)。
    public static void Remember(Control parent)
    {
        if (Attached.TryGetValue(parent, out var bevels)) Remembered[parent] = bevels;
    }

    public static void Zoom(Control parent, float k, Point offset = default)
    {
        if (!Remembered.TryGetValue(parent, out var bevels)) return;
        int M(int v) => (int)Math.Round(v * k);
        Attached[parent] = bevels.Select(b =>
        {
            int l = M(b.Left), t = M(b.Top);
            return b with { Left = l + offset.X, Top = t + offset.Y, Width = M(b.Left + b.Width) - l, Height = M(b.Top + b.Height) - t };
        }).ToArray();
    }

    private static void Draw(Graphics g, Bevel b, Pen shadow, Pen light)
    {
        int l = b.Left, t = b.Top, r = b.Left + b.Width, bt = b.Top + b.Height;
        switch (b.Shape)
        {
            case BevelShape.Box:
                g.DrawLine(shadow, l, t, r - 1, t);
                g.DrawLine(shadow, l, t, l, bt - 1);
                g.DrawLine(light, r - 1, t, r - 1, bt - 1);
                g.DrawLine(light, l, bt - 1, r - 1, bt - 1);
                break;
            case BevelShape.TopLine:
                g.DrawLine(shadow, l, t, r, t);
                g.DrawLine(light, l, t + 1, r, t + 1);
                break;
            case BevelShape.BottomLine:
                g.DrawLine(shadow, l, bt - 2, r, bt - 2);
                g.DrawLine(light, l, bt - 1, r, bt - 1);
                break;
            case BevelShape.LeftLine:
                g.DrawLine(shadow, l, t, l, bt);
                g.DrawLine(light, l + 1, t, l + 1, bt);
                break;
        }
    }
}

/// 2px divider は real window なので sibling control より上に描ける。TBevel とはここが違う。
internal sealed class RaisedLine : Control
{
    public RaisedLine()
    {
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint, true);
        TabStop = false;
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        using var light  = new Pen(App.Theme.Current.BevelLight);
        using var shadow = new Pen(App.Theme.Current.BevelShadow);
        e.Graphics.DrawLine(light, 0, 0, Width, 0);
        e.Graphics.DrawLine(shadow, 0, Height - 1, Width, Height - 1);
    }
}
