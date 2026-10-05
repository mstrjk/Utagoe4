// Windows XP (Luna) 風の枠に terminal を入れた window (v3 にない)。Help の terminal と導入画面が共通で使う。
// 上端は Luna の title bar のような gradient、下端は XP の status bar。色は App.Theme。中身は TerminalView。

using Utagoe.App;
using Utagoe.Ui;

namespace Utagoe.Ui;

internal class XpTerminalWindow : UiWindow
{
    protected readonly TerminalView Terminal = new() { Dock = DockStyle.Fill };
    private readonly Panel _header = new BufferedPanel { Dock = DockStyle.Top, Height = 46 };
    private string _shownFooter = "";
    private readonly Panel _frame = new() { Dock = DockStyle.Fill, Padding = new Padding(1) };
    private readonly Label _footer = new() { Dock = DockStyle.Bottom, Height = 22, TextAlign = ContentAlignment.MiddleLeft };
    private readonly System.Windows.Forms.Timer _tick = new() { Interval = 250 };

    protected string Heading { get; set; } = "Utagoe Terminal";
    protected string Subheading { get; set; } = "";

    protected static WindowSpec Template(Func<string> title, Placement placement) => new()
    {
        ClientSize = new Size(780, 460),
        MinimumSize = new Size(480, 260),
        Border = Border.Header,
        Buttons = TitleButtons.All,
        Taskbar = true,
        FontSize = 8.25f,
        Scaling = Scaling.Dpi,
        Placement = placement,
        Title = title,
    };

    protected XpTerminalWindow(WindowSpec spec) : base(spec)
    {

        _header.Paint += PaintHeader;
        _header.Resize += (_, _) => _header.Invalidate();
        _frame.Controls.Add(Terminal);
        _footer.Padding = new Padding(6, 0, 6, 0);
        _footer.Font = new Font("Tahoma", 8.25f);
        _footer.AutoEllipsis = true;

        Controls.Add(_frame);
        Controls.Add(_header);
        Controls.Add(_footer);

        // footer は変わったときだけ描き直す (毎回描くと window 全体がちらつく)。
        _tick.Tick += (_, _) =>
        {
            string footer = FooterText();
            if (footer != _shownFooter) { _shownFooter = footer; _footer.Text = footer; }
        };
        _tick.Start();
        Shown += (_, _) => { _footer.Text = FooterText(); Terminal.Focus(); };
        ApplyTheme();
        Theme.Changed += ApplyTheme;
        // 単独の window のときは Windows の title bar を持たず、この見出しが title bar になる (動かす・最大化・閉じる)。
        // 外周 1 px は theme の縁の色 (form の背景が Padding の分だけ見える)。
        Padding = new Padding(1);
        ThemedFrame.Attach(this, _header);
    }

    /// 枠線・footer の色を今の theme にし、見出しを描き直す。
    public void ApplyTheme()
    {
        var t = Theme.Current;
        BackColor = t.Edge;
        _frame.BackColor = t.Edge;
        _footer.BackColor = t.FooterBack;
        _footer.ForeColor = t.FooterText;
        _header.Invalidate();
        Terminal.Invalidate();
    }

    protected virtual string FooterText() => "";

    // 枠の無い window にも Windows の影を付ける。
    protected override CreateParams CreateParams
    {
        get
        {
            var cp = base.CreateParams;
            if (TopLevel) cp.ClassStyle |= 0x00020000;
            return cp;
        }
    }

    // gradient の見出しを描き直すときにちらつかないよう、裏で描いてから出す panel。
    private sealed class BufferedPanel : Panel
    {
        public BufferedPanel() =>
            SetStyle(ControlStyles.OptimizedDoubleBuffer | ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true);
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing) { _tick.Dispose(); Theme.Changed -= ApplyTheme; }
        base.Dispose(disposing);
    }

    private void PaintHeader(object? sender, PaintEventArgs e)
    {
        var g = e.Graphics;
        var r = _header.ClientRectangle;
        if (r.Width <= 0 || r.Height <= 0) return;
        var t = Theme.Current;
        t.FillHeader(g, r);
        using (var hi = new Pen(Color.FromArgb(120, 255, 255, 255)))
            g.DrawLine(hi, 0, 0, r.Width, 0);
        using var title = new Font("Trebuchet MS", 13f, FontStyle.Bold);
        using var sub = new Font("Tahoma", 8.25f);
        int buttons = ThemedFrame.PaintCaptionButtons(this, g);
        TextRenderer.DrawText(g, L.T(Heading), title, new Point(11, 3), t.HeaderText, TextFormatFlags.NoPadding);
        var subBox = new Rectangle(13, 27, Math.Max(0, r.Width - 13 - (buttons > 0 ? buttons : 8)), r.Height - 27);
        TextRenderer.DrawText(g, L.T(Subheading), sub, subBox, t.HeaderText, TextFormatFlags.NoPadding | TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis);
        // v4: 以前は題名の右に RUNNING / IDLE などの状態を出していたが、本物の terminal には無く目障りなので出さない。
    }
}
