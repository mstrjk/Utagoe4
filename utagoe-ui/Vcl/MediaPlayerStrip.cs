// VCL TMediaPlayer の button strip を再現する control。
// 1 つの window 内に classic 3D button を横並びで描く。全体が 1 tab stop で、矢印で focus、Space/Enter で押下。

namespace Utagoe.Vcl;

internal sealed class MediaPlayerStrip : Control
{
    private readonly string[] _names;
    private readonly Bitmap[] _glyphs;
    private readonly Bitmap[] _disabled;
    private readonly bool[] _enabled;
    private int _focus;
    private int _pressed = -1;

    /// click された button index を event で返す。
    public event Action<int>? ButtonClick;

    /// buttons は MPPLAY など resource stem を表示順に渡す。
    public MediaPlayerStrip(params string[] buttons)
    {
        _names = buttons;
        _glyphs = buttons.Select(b => VclGlyph.Load("CL_" + b)).ToArray();
        _disabled = buttons.Select(b => VclGlyph.Load("DI_" + b)).ToArray();
        _enabled = buttons.Select(_ => true).ToArray();
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint |
                 ControlStyles.OptimizedDoubleBuffer | ControlStyles.Selectable, true);
        TabStop = true;
    }

    public void SetButtonEnabled(int index, bool enabled)
    {
        _enabled[index] = enabled;
        Invalidate();
    }

    private Rectangle ButtonRect(int i)
    {
        int n = _names.Length;
        int w = Width / n;
        // 割り切れない余り pixel は最後の button に足して strip 幅を埋める。
        int right = i == n - 1 ? Width : (i + 1) * w;
        return new Rectangle(i * w, 0, right - i * w, Height);
    }

    private int HitTest(Point p)
    {
        for (int i = 0; i < _names.Length; i++)
            if (ButtonRect(i).Contains(p)) return i;
        return -1;
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        for (int i = 0; i < _names.Length; i++)
        {
            var r = ButtonRect(i);
            bool down = i == _pressed;
            ControlPaint.DrawButton(g, r, down ? ButtonState.Pushed : ButtonState.Normal);

            var glyph = Enabled && _enabled[i] ? _glyphs[i] : _disabled[i];
            int ox = down ? 1 : 0;
            g.DrawImage(glyph, r.Left + (r.Width - glyph.Width) / 2 + ox,
                               r.Top + (r.Height - glyph.Height) / 2 + ox,
                               glyph.Width, glyph.Height);

            if (Focused && i == _focus && ShowFocusCues)
            {
                var f = r;
                f.Inflate(-3, -3);
                ControlPaint.DrawFocusRectangle(g, f);
            }
        }
    }

    protected override void OnMouseDown(MouseEventArgs e)
    {
        base.OnMouseDown(e);
        if (e.Button != MouseButtons.Left) return;
        int i = HitTest(e.Location);
        if (i < 0 || !_enabled[i]) return;
        Focus();
        _focus = i;
        _pressed = i;
        Capture = true;
        Invalidate();
    }

    protected override void OnMouseUp(MouseEventArgs e)
    {
        base.OnMouseUp(e);
        int i = _pressed;
        _pressed = -1;
        Capture = false;
        Invalidate();
        if (i >= 0 && HitTest(e.Location) == i) ButtonClick?.Invoke(i);
    }

    protected override bool IsInputKey(Keys keyData) =>
        keyData is Keys.Left or Keys.Right || base.IsInputKey(keyData);

    protected override void OnKeyDown(KeyEventArgs e)
    {
        base.OnKeyDown(e);
        switch (e.KeyCode)
        {
            case Keys.Left:  _focus = (_focus + _names.Length - 1) % _names.Length; Invalidate(); break;
            case Keys.Right: _focus = (_focus + 1) % _names.Length; Invalidate(); break;
            case Keys.Space:
            case Keys.Enter:
                if (_enabled[_focus]) ButtonClick?.Invoke(_focus);
                e.Handled = true;
                break;
        }
    }

    protected override void OnGotFocus(EventArgs e) { base.OnGotFocus(e); Invalidate(); }
    protected override void OnLostFocus(EventArgs e) { base.OnLostFocus(e); Invalidate(); }
    protected override void OnEnabledChanged(EventArgs e) { base.OnEnabledChanged(e); Invalidate(); }
}
