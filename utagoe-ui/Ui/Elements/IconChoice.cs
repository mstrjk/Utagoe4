using Utagoe.App;

namespace Utagoe.Ui;

/// app icon の色違いを 1 つ表す枠。選んでいるものは青い枠と薄い青の背景、mouse が乗ると薄い枠。
internal sealed class IconChoice : Control
{
    private static readonly Color Selected = Color.FromArgb(0, 120, 215);
    private static readonly Color SelectedBack = Color.FromArgb(204, 228, 247);
    private readonly string _name;
    private bool _hot;

    public string IconName => _name;

    [System.ComponentModel.DesignerSerializationVisibility(System.ComponentModel.DesignerSerializationVisibility.Hidden)]
    public bool? Chosen { get; set; }

    public IconChoice(string name)
    {
        _name = name;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                 ControlStyles.ResizeRedraw | ControlStyles.SupportsTransparentBackColor, true);
        BackColor = Color.Transparent;
        Cursor = Cursors.Hand;
        AccessibleName = AppIcons.DisplayName(name);
        AccessibleRole = AccessibleRole.RadioButton;
    }

    protected override void OnMouseEnter(EventArgs e) { _hot = true; Invalidate(); base.OnMouseEnter(e); }
    protected override void OnMouseLeave(EventArgs e) { _hot = false; Invalidate(); base.OnMouseLeave(e); }

    protected override bool IsInputKey(Keys keyData) => keyData is Keys.Space or Keys.Enter || base.IsInputKey(keyData);

    protected override void OnKeyDown(KeyEventArgs e)
    {
        if (e.KeyCode is Keys.Space or Keys.Enter) { OnClick(EventArgs.Empty); e.Handled = true; }
        base.OnKeyDown(e);
    }

    protected override void OnGotFocus(EventArgs e) { Invalidate(); base.OnGotFocus(e); }
    protected override void OnLostFocus(EventArgs e) { Invalidate(); base.OnLostFocus(e); }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        bool selected = Chosen ?? AppIcons.Current == _name;
        var r = new Rectangle(0, 0, Width - 1, Height - 1);
        if (selected)
        {
            using var back = new SolidBrush(SelectedBack);
            g.FillRectangle(back, r);
        }
        if (selected || _hot)
        {
            using var pen = new Pen(selected ? Selected : Color.FromArgb(150, 190, 230), selected ? 2 : 1);
            g.DrawRectangle(pen, selected ? new Rectangle(1, 1, Width - 3, Height - 3) : r);
        }
        int pad = Math.Max(2, Width / 9);
        int size = Width - pad * 2;
        g.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.NearestNeighbor;
        g.PixelOffsetMode = System.Drawing.Drawing2D.PixelOffsetMode.Half;
        g.DrawImage(AppIcons.Render(_name, size), pad, pad, size, size);
        if (Focused && ShowFocusCues) ControlPaint.DrawFocusRectangle(g, Rectangle.Inflate(r, -1, -1));
    }
}
