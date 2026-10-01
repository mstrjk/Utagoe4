// 部品のまとまりを拡大する (v4)。main window を最大化したとき、元の main window の中身と Settings を大きく見せるのに使う。
// 拡大前の位置・大きさ・font・bevel を覚えておき、倍率が変わるたびにそこから掛け直す (丸め誤差を溜めない)。
// radio group の中の button と tab page は親が並べ直すので、位置は触らない。

namespace Utagoe.Vcl;

internal sealed class UiZoom
{
    private readonly Control _root;
    private readonly Font _rootFont;
    private readonly Size _rootSize;
    private readonly List<(Control Control, Rectangle Bounds, Font? OwnFont)> _items = new();

    public float Factor { get; private set; } = 1f;
    private Point _offset;

    /// root の今の状態を倍率 1 として覚える。parts は対象の子 (null なら root の子すべて)。
    public UiZoom(Control root, IEnumerable<Control>? parts = null)
    {
        _root = root;
        _rootFont = root.Font;
        _rootSize = root.Size;
        foreach (Control c in parts ?? root.Controls.Cast<Control>()) Collect(c);
        BevelPainter.Remember(root);
    }

    private void Collect(Control c)
    {
        // 親から受け継いだ font は親と同じ object を返す。自前の font だけ覚えて掛け直す。
        Font? own = c.Parent != null && ReferenceEquals(c.Font, c.Parent.Font) ? null : c.Font;
        _items.Add((c, c.Bounds, own));
        foreach (Control child in c.Controls) Collect(child);
    }

    public Size BaseSize => _rootSize;

    /// offset は拡大した後に部品をずらす量 (最大化したとき、main window の中身を枠の中へ置くため)。
    public void Apply(float k, bool resizeRoot, Point offset = default)
    {
        if (Math.Abs(k - Factor) < 0.001f && offset == _offset) return;
        Factor = k;
        _offset = offset;
        _root.SuspendLayout();
        _root.Font = Scaled(_rootFont, k);
        foreach (var (c, b, own) in _items)
        {
            if (c.IsDisposed) continue;
            if (c is not TabPage && c.Parent is not (VclRadioGroup or VclRadioGroup.ScrollHost))
            {
                int l = Mul(b.Left, k), t = Mul(b.Top, k);
                // ずらすのは root の直下の部品だけ (その中の部品は親と一緒に動く)。
                var d = c.Parent == _root ? offset : Point.Empty;
                c.SetBounds(l + d.X, t + d.Y, Mul(b.Right, k) - l, Mul(b.Bottom, k) - t);
            }
            if (own != null) c.Font = Scaled(own, k);
            if (c is BitBtn btn) btn.GlyphZoom = k;
        }
        if (resizeRoot) _root.Size = new Size(Mul(_rootSize.Width, k), Mul(_rootSize.Height, k));
        BevelPainter.Zoom(_root, k, offset);
        _root.ResumeLayout(true);
        _root.Invalidate(true);
    }

    private static Font Scaled(Font f, float k) => new(f.FontFamily, f.Size * k, f.Style, f.Unit);
    private static int Mul(int v, float k) => (int)Math.Round(v * k);
}
