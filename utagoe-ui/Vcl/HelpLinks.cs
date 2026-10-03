// 設定の見出しを青い下線付きの link にし、mouse を乗せると説明を出す (v4)。
// 灰色 (無効) の group は mouse の event を受け取らないので、timer で mouse の位置と見出しの範囲を比べて判定する。
// 説明は見出しか説明自身の上に mouse がある間は出たままで、外れると少しずつ消える。戻れば消えるのを止める。

using System.Runtime.InteropServices;

namespace Utagoe.Vcl;

internal sealed class HelpLinks : IDisposable
{
    public static Color LinkColor => App.Theme.Current.Link;

    private readonly List<(Control Target, Func<string> Text)> _items = new();
    private readonly System.Windows.Forms.Timer _timer = new() { Interval = 50 };
    private Control? _hover;
    private Control? _sunk;
    private int _dwell;

    /// 最大化中は説明を popup ではなく右側の説明欄へ出す (main window が設定し、元に戻すと null)。
    public static Action<string>? Sink { get; set; }

    public HelpLinks()
    {
        _timer.Tick += (_, _) => Tick();
        _timer.Start();
    }

    /// target の見出し (group なら枠の見出し、label ならその文字) を link にする。
    public void Add(Control target, Func<string> text)
    {
        if (target is VclGroupBox g) g.IsLink = true;
        else
        {
            target.ForeColor = LinkColor;
            target.Tag = "link";   // theme が替わったら Theme.Paint が link の色に塗り直す
            target.Font = new Font(target.Font, target.Font.Style | FontStyle.Underline);
        }
        _items.Add((target, text));
    }

    public void AddPlain(Control target, Func<string> text) => _items.Add((target, text));

    /// 見出しの範囲 (画面座標)。group の見出しは枠の左から 8 px の所に描かれる。
    public static Rectangle CaptionBounds(Control target)
    {
        if (target is GroupBox)
        {
            Size t = TextRenderer.MeasureText(target.Text, target.Font);
            return target.RectangleToScreen(new Rectangle(6, 0, t.Width + 4, t.Height));
        }
        return target.RectangleToScreen(target.ClientRectangle);
    }

    // mouse が同じ見出しの上に 150 ms 留まったら説明を出す。
    private void Tick()
    {
        Point m = Cursor.Position;
        Control? over = null;
        foreach (var (target, _) in _items)
        {
            if (!target.Visible || target.IsDisposed || !CaptionBounds(target).Contains(m)) continue;
            if (!OnTop(target, m)) continue;
            over = target;
            break;
        }
        if (over != _hover) { _hover = over; _dwell = 0; }
        if (over == null) return;
        if (Sink is { } sink)
        {
            // 説明欄は次の link に乗せるまで残す。同じ link の上では書き直さない。
            if (over == _sunk || ++_dwell < 2) return;
            _sunk = over;
            InfoTip.CloseCurrent();
            sink(_items.First(i => i.Target == over).Text());
            return;
        }
        _sunk = null;
        if (InfoTip.IsShowingFor(over)) return;
        if (++_dwell < 3) return;
        string text = _items.First(i => i.Target == over).Text();
        InfoTip.Show(over, text);
    }

    // 別の window が上に重なっていないか (mouse の下の window が同じ window に属するか)。
    private static bool OnTop(Control target, Point screen)
    {
        IntPtr hit = WindowFromPoint(screen);
        if (hit == IntPtr.Zero || target.TopLevelControl == null) return false;
        return GetAncestor(hit, 2) == GetAncestor(target.Handle, 2);
    }

    public void Dispose()
    {
        _timer.Dispose();
        InfoTip.CloseCurrent();
    }

    [DllImport("user32.dll")] private static extern IntPtr WindowFromPoint(Point p);
    [DllImport("user32.dll")] private static extern IntPtr GetAncestor(IntPtr hwnd, uint flags);
}

/// 見出しに mouse を乗せると出る説明。focus は奪わない。1 度に 1 つだけ。
internal sealed class InfoTip : Form
{
    private static InfoTip? s_current;

    private readonly Control _anchor;
    private readonly System.Windows.Forms.Timer _timer = new() { Interval = 30 };
    private readonly Padding _pad;
    private readonly string _title, _body;
    private readonly Font _bold;
    private readonly int _titleHeight;

    public static bool IsShowingFor(Control anchor) => s_current is { IsDisposed: false } t && t._anchor == anchor;

    public static void CloseCurrent() => s_current?.Close();

    public static void Show(Control anchor, string text)
    {
        CloseCurrent();
        if (anchor.TopLevelControl is not Form owner) return;
        s_current = new InfoTip(anchor, text);
        s_current.Show(owner);
    }

    private InfoTip(Control anchor, string text)
    {
        _anchor = anchor;
        FormBorderStyle = FormBorderStyle.None;
        ShowInTaskbar = false;
        StartPosition = FormStartPosition.Manual;
        BackColor = App.Theme.Current.TipBack;
        Font = new Font("Tahoma", anchor.Font.SizeInPoints * 0.95f);
        _bold = new Font(Font, FontStyle.Bold);
        int line = anchor.Font.Height;
        _pad = new Padding(line / 2 + 2);
        int nl = text.IndexOf('\n');
        _title = nl < 0 ? text : text[..nl];
        _body = nl < 0 ? "" : text[(nl + 1)..];

        // 幅は文字の高さの 22 倍まで (拡大にも追従)。文の量に合わせて高さを決める。
        int maxWidth = line * 22;
        var flags = TextFormatFlags.WordBreak;
        Size t = TextRenderer.MeasureText(_title, _bold, new Size(maxWidth, int.MaxValue), flags);
        Size b = _body.Length > 0 ? TextRenderer.MeasureText(_body, Font, new Size(maxWidth, int.MaxValue), flags) : Size.Empty;
        _titleHeight = t.Height + (b.Height > 0 ? 2 : 0);
        Size = new Size(Math.Max(t.Width, b.Width) + _pad.Horizontal + 2, _titleHeight + b.Height + _pad.Vertical + 2);

        // 見出しの下に出す。画面からはみ出すなら左や上へずらす。
        Rectangle a = HelpLinks.CaptionBounds(anchor);
        Rectangle area = Screen.FromControl(anchor).WorkingArea;
        int x = a.Left, y = a.Bottom + 2;
        if (x + Width > area.Right) x = Math.Max(area.Left, area.Right - Width);
        if (y + Height > area.Bottom) y = Math.Max(area.Top, a.Top - Height - 2);
        Location = new Point(x, y);

        _timer.Tick += (_, _) => Tick();
        _timer.Start();
        FormClosed += (_, _) => { _timer.Dispose(); _bold.Dispose(); if (s_current == this) s_current = null; };
    }

    protected override bool ShowWithoutActivation => true;

    protected override CreateParams CreateParams
    {
        get
        {
            var cp = base.CreateParams;
            cp.ExStyle |= 0x08000000 | 0x00000080;
            return cp;
        }
    }

    // mouse が見出しか説明の上にあれば濃いまま、外れたら 0.5 秒ほどで消える。
    private void Tick()
    {
        if (_anchor.IsDisposed || !_anchor.Visible) { Close(); return; }
        Point m = Cursor.Position;
        bool over = Bounds.Contains(m) || HelpLinks.CaptionBounds(_anchor).Contains(m);
        if (over) { Opacity = 1.0; return; }
        Opacity -= 0.06;
        if (Opacity <= 0.02) Close();
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var t = App.Theme.Current;
        using (var edge = new Pen(t.TipEdge)) e.Graphics.DrawRectangle(edge, 0, 0, Width - 1, Height - 1);
        var r = new Rectangle(_pad.Left + 1, _pad.Top + 1, Width - _pad.Horizontal - 2, Height - _pad.Vertical - 2);
        TextRenderer.DrawText(e.Graphics, _title, _bold, r, t.TipText, TextFormatFlags.WordBreak);
        r.Y += _titleHeight;
        r.Height -= _titleHeight;
        TextRenderer.DrawText(e.Graphics, _body, Font, r, t.TipText, TextFormatFlags.WordBreak);
    }

    protected override void OnClick(EventArgs e) { base.OnClick(e); Close(); }
}
