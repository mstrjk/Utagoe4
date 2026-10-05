// VCL の TBitBtn と同じ glyph / caption layout にする WinForms button。
// WinForms 標準 layout は VCL の Margin / Spacing と合わないため自前配置する。background は ButtonRenderer に任せる。

using System.ComponentModel;
using System.Drawing.Drawing2D;
using System.Windows.Forms.VisualStyles;

namespace Utagoe.Ui;

internal enum BitBtnKind { Custom, OK, Cancel, Help }

internal sealed class BitBtn : Button
{
    private Bitmap? _glyph;
    private Bitmap? _glyphDisabled;
    private string? _role;
    private bool _pressed;
    private bool _hot;

    /// Margin=-1 は glyph + caption 全体を中央寄せする VCL 既定値。
    [DefaultValue(-1)]
    public int GlyphMargin { get; set; } = -1;

    /// Spacing=-1 は glyph の右側領域で caption を中央寄せ。VCL 既定は 4px。
    [DefaultValue(4)]
    public int Spacing { get; set; } = 4;

    /// 拡大表示の倍率 (v4、main window 最大化時)。glyph は pixel art なので整数倍に丸めて拡大する。
    [DefaultValue(1f)]
    public float GlyphZoom { get; set; } = 1f;

    public BitBtn()
    {
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint |
                 ControlStyles.OptimizedDoubleBuffer, true);
        UseVisualStyleBackColor = true;
    }

    public void SetGlyph(string role)
    {
        _role = role;
        Art.Button(role);
        Invalidate();
    }

    /// TMediaPlayer の enabled / disabled glyph を別 image として設定できる。
    public void SetGlyphs(Bitmap enabled, Bitmap? disabled)
    {
        _role = null;
        _glyph = enabled;
        _glyphDisabled = disabled;
        Invalidate();
    }

    /// VCL stock Kind は glyph、caption、dialog result をまとめて設定する。
    public void SetKind(BitBtnKind kind)
    {
        switch (kind)
        {
            case BitBtnKind.OK:
                SetGlyph("check");
                if (string.IsNullOrEmpty(Text)) Text = "OK";
                DialogResult = DialogResult.OK;
                break;
            case BitBtnKind.Cancel:
                SetGlyph("x");
                if (string.IsNullOrEmpty(Text)) Text = "Cancel";
                DialogResult = DialogResult.Cancel;
                break;
            case BitBtnKind.Help:
                SetGlyph("question");
                if (string.IsNullOrEmpty(Text)) Text = "&Help";
                break;
        }
    }

    protected override void OnMouseEnter(EventArgs e) { _hot = true; Invalidate(); base.OnMouseEnter(e); }
    protected override void OnMouseLeave(EventArgs e) { _hot = false; Invalidate(); base.OnMouseLeave(e); }
    protected override void OnMouseDown(MouseEventArgs e)
    {
        if (e.Button == MouseButtons.Left) { _pressed = true; Invalidate(); }
        base.OnMouseDown(e);
    }
    protected override void OnMouseUp(MouseEventArgs e)
    {
        _pressed = false; Invalidate();
        base.OnMouseUp(e);
    }
    protected override void OnEnabledChanged(EventArgs e) { Invalidate(); base.OnEnabledChanged(e); }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;

        var state = !Enabled ? PushButtonState.Disabled
                  : _pressed ? PushButtonState.Pressed
                  : _hot ? PushButtonState.Hot
                  : (Focused || IsDefault) ? PushButtonState.Default
                  : PushButtonState.Normal;

        // v4: どの theme でも同じ平らな button を theme の色で描く (Windows の button は theme の色にならないため)。
        var theme = App.Theme.Current;
        using (var parent = new SolidBrush(Parent?.BackColor ?? theme.Window)) g.FillRectangle(parent, ClientRectangle);
        Color face = !Enabled ? theme.Window : _pressed ? theme.ButtonPressed : _hot ? theme.ButtonHot : theme.ButtonFace;
        Color edge = (Focused || IsDefault) && Enabled ? theme.Link : theme.ButtonEdge;
        var box = new Rectangle(0, 0, Width - 1, Height - 1);
        using (var fb = new SolidBrush(face)) g.FillRectangle(fb, box);
        using (var ep = new Pen(edge)) g.DrawRectangle(ep, box);
        if (_hot && !_pressed && Enabled)
            using (var hp = new Pen(theme.ButtonHotEdge))
            {
                g.DrawRectangle(hp, box);
                g.DrawRectangle(hp, Rectangle.Inflate(box, -1, -1));
            }

        if (_role != null) (_glyph, _glyphDisabled) = Art.Button(_role);
        var glyph = Enabled ? _glyph : (_glyphDisabled ?? _glyph);
        string text = Text ?? "";
        var flags = TextFormatFlags.SingleLine | TextFormatFlags.NoPadding;
        if (!ShowKeyboardCues) flags |= TextFormatFlags.HidePrefix;
        Size textSize = text.Length > 0 ? TextRenderer.MeasureText(g, text, Font, Size.Empty, flags) : Size.Empty;
        int gz = Math.Max(1, (int)Math.Round(GlyphZoom));
        Size glyphSize = glyph != null ? new Size(glyph.Width * gz, glyph.Height * gz) : Size.Empty;

        // glyph 左配置は TButtonGlyph.CalcButtonLayout の挙動に合わせる。
        var client = ClientRectangle;
        if (glyph != null && text.Length > 0 && glyphSize.Width + Math.Max(Spacing, 4) + textSize.Width + 8 > client.Width)
        {
            glyph = null;
            glyphSize = Size.Empty;
        }
        int spacing = Spacing, margin = GlyphMargin;
        if (glyph == null || text.Length == 0) spacing = 0;

        if (margin == -1)
        {
            if (spacing == -1)
            {
                int total = glyphSize.Width + textSize.Width;
                spacing = (client.Width - total) / 3;
                margin = spacing;
            }
            else
            {
                int total = glyphSize.Width + spacing + textSize.Width;
                margin = (client.Width - total + 1) / 2;
            }
        }
        else if (spacing == -1)
        {
            int remaining = client.Width - (margin + glyphSize.Width);
            spacing = (remaining - textSize.Width) / 2;
        }

        int offset = _pressed && Enabled ? 1 : 0;
        int glyphX = client.Left + margin + offset;
        int glyphY = client.Top + (client.Height - glyphSize.Height + 1) / 2 + offset;
        int textX = glyphX + glyphSize.Width + spacing;
        int textY = client.Top + (client.Height - textSize.Height + 1) / 2 + offset;

        if (glyph != null)
        {
            g.InterpolationMode = InterpolationMode.NearestNeighbor;
            g.DrawImage(glyph, glyphX, glyphY, glyphSize.Width, glyphSize.Height);
        }

        if (text.Length > 0)
        {
            var color = Enabled ? ForeColor : theme.Muted;
            TextRenderer.DrawText(g, text, Font, new Point(textX, textY), color, flags);
        }

        if (Focused && ShowFocusCues)
        {
            var r = ClientRectangle;
            r.Inflate(-4, -4);
            ControlPaint.DrawFocusRectangle(g, r);
        }
    }
}
