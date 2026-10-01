// 画面全体の色 (v4)。app icon の色違いと 1 対 1 で、About で icon を選ぶと theme も替わる。
// 自前で描く部品 (terminal、見出し、bevel、説明、link) は描くたびに Theme.Current を読む。
// WinForms の標準部品は Attach した form ごとに色を塗り直す。button の面、tab の見出し、radio の丸など Windows が描く所は替えない。

using Utagoe.Vcl;

namespace Utagoe.App;

/// terminal の行の種類。色は描くときに theme から決める。
internal enum TermStyle { Text, Detail, Stage, Done, Warn, Error, Trace, Status, Prompt }

internal sealed record Theme(
    string Name,
    Color Window, Color Text, Color Muted, Color Input, Color InputText, Color Caption, Color Link,
    Color HeaderTop, Color HeaderBottom, Color HeaderText, Color Edge, Color FooterBack, Color FooterText,
    Color TermBack, Color TermText, Color TermDetail, Color TermStatus, Color TermPrompt, bool TermDark,
    Color TipBack, Color TipText, Color TipEdge)
{

    private static Color C(int rgb) => Color.FromArgb(255, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);

    public static readonly Theme[] All =
    {
        new("standard", C(0xFFEFF3), Color.Black, C(0x8A6F76), Color.White, Color.Black, C(0x9E0019), C(0x0066CC),
            C(0xFFBBCC), C(0xFFD6E0), Color.Black, C(0xFF0000), C(0xFFBBCC), Color.Black,
            C(0xFFF0F4), Color.Black, C(0x693746), C(0xBE001E), C(0xC80000), false,
            C(0xFFF0F4), Color.Black, C(0xFF0000)),
        new("army", C(0xE9F2E3), C(0x1E1710), C(0x6B6458), C(0xFBFDF9), C(0x1E1710), C(0x3E2E1F), C(0x1F5FA8),
            C(0x6FB14B), C(0x93C677), C(0x1E1710), C(0x3E2E1F), C(0x6FB14B), C(0x1E1710),
            C(0x2A2016), C(0xEAF2E3), C(0xB5C9A6), C(0x8FD16A), C(0x9BD67B), true,
            C(0xE9F2E3), C(0x1E1710), C(0x3E2E1F)),
        new("ice", C(0xEEF5FE), Color.Black, C(0x5E6F84), Color.White, Color.Black, C(0x1D5FAF), C(0x0B5CBF),
            C(0xB7D6FC), C(0xD6E7FD), Color.Black, C(0x4790E4), C(0xB7D6FC), Color.Black,
            C(0xF5F9FF), Color.Black, C(0x3A4F6B), C(0x1E64BE), C(0x2A6FCB), false,
            C(0xEEF5FE), Color.Black, C(0x4790E4)),
        new("silver", C(0xEDEDED), Color.Black, C(0x6E6E6E), Color.White, Color.Black, C(0x7A2D2D), C(0x0066CC),
            C(0xD9D9D9), C(0xEBEBEB), Color.Black, C(0x7A2D2D), C(0xD9D9D9), Color.Black,
            C(0xF7F7F7), Color.Black, C(0x555555), C(0x7A2D2D), C(0x7A2D2D), false,
            C(0xF7F7F7), Color.Black, C(0x7A2D2D)),
        new("teal", C(0xEEFDF4), Color.Black, C(0x5C6E63), Color.White, Color.Black, C(0x8A4519), C(0x0066CC),
            C(0xB7FCD2), C(0xD8FDE7), Color.Black, C(0xAE5924), C(0xB7FCD2), Color.Black,
            C(0xF3FEF8), Color.Black, C(0x3C5A48), C(0x8A4519), C(0xAE5924), false,
            C(0xEEFDF4), Color.Black, C(0xAE5924)),
        new("white", SystemColors.Control, Color.Black, C(0x6D6D6D), Color.White, Color.Black, Color.Black, C(0x0066CC),
            Color.White, C(0xF0F0F0), Color.Black, C(0xA0A0A0), C(0xF0F0F0), Color.Black,
            Color.White, Color.Black, C(0x555555), C(0x0050A0), Color.Black, false,
            Color.White, Color.Black, C(0xA0A0A0)),
        new("wine", C(0x2B2B2B), C(0xE8E8E8), C(0x9A9A9A), C(0x1E1E1E), C(0xE8E8E8), C(0xD8B4BA), C(0x7FB8FF),
            C(0x3A3A3A), C(0x2B2B2B), C(0xF0F0F0), Color.Black, C(0x1E1E1E), C(0xD0D0D0),
            C(0x0A0A0A), C(0xE8E8E8), C(0xA0A0A0), Color.White, Color.White, true,
            C(0x2B2B2B), C(0xE8E8E8), C(0x5A5A5A)),
    };

    public static Theme Current { get; private set; } = All[0];

    /// theme が替わったとき (UI thread で呼ばれる)。
    public static event Action? Changed;

    public static void Set(string name)
    {
        var t = All.FirstOrDefault(x => x.Name == name) ?? All[0];
        if (t == Current) return;
        Current = t;
        foreach (Form f in Application.OpenForms.Cast<Form>().ToList()) Paint(f);
        Changed?.Invoke();
    }


    public bool Dark => Window.GetBrightness() < 0.45f;

    /// bevel の影と光。面の明るさに合わせて作る。
    public Color BevelShadow => Blend(Window, Color.Black, Dark ? 0.45 : 0.32);
    public Color BevelLight => Blend(Window, Color.White, Dark ? 0.12 : 0.75);

    public Color DisabledCaption => Muted;

    public Color Progress => Name switch
    {
        "standard" => C(0xFF6F8E),
        "army" => C(0x6FB14B),
        "ice" => C(0x4790E4),
        "silver" => C(0x7A2D2D),
        "teal" => C(0xAE5924),
        "wine" => C(0x3FB950),
        _ => C(0x06B025),
    };

    /// 枠線 (group の枠、tab、button、入力欄の四角、進捗棒)。どの theme も同じ描き方で、色だけが違う。
    public Color Line => Blend(Window, Text, Dark ? 0.35 : 0.30);

    /// button の面。明るい theme は面より白く、暗い theme は面より少し明るく。
    public Color ButtonFace => Dark ? Blend(Window, Color.White, 0.10) : Blend(Window, Color.White, 0.65);
    public Color ButtonHot => Blend(ButtonFace, Progress, Dark ? 0.22 : 0.18);
    public Color ButtonHotEdge => Progress;
    public Color InputFocus => Progress;
    public Color ButtonPressed => Dark ? Blend(Window, Color.Black, 0.25) : Blend(Window, Text, 0.10);
    public Color ButtonEdge => Line;
    public Color DisabledLink => Blend(Link, Window, 0.45);

    public Color Term(TermStyle s) => s switch
    {
        TermStyle.Detail => TermDetail,
        TermStyle.Status => TermStatus,
        TermStyle.Prompt => TermPrompt,
        TermStyle.Stage => TermDark ? C(0xFFD966) : C(0x784600),
        TermStyle.Done => TermDark ? C(0x7CD67C) : C(0x00691E),
        TermStyle.Warn => TermDark ? C(0xFFC04D) : C(0x964B00),
        TermStyle.Error => TermDark ? C(0xFF5F5F) : C(0xC80000),
        TermStyle.Trace => TermDark ? C(0xFFA0A0) : C(0x8C1414),
        _ => TermText,
    };

    public void FillHeader(Graphics g, Rectangle r, bool vertical = true)
    {
        if (r.Width <= 0 || r.Height <= 0) return;
        using var brush = new System.Drawing.Drawing2D.LinearGradientBrush(r, HeaderTop, HeaderBottom, vertical ? 90f : 0f);
        g.FillRectangle(brush, r);
    }

    public static Color Blend(Color a, Color b, double t) => Color.FromArgb(
        (int)Math.Round(a.R + (b.R - a.R) * t), (int)Math.Round(a.G + (b.G - a.G) * t), (int)Math.Round(a.B + (b.B - a.B) * t));


    /// 部品の木をたどって色を付ける。自前で色を決めている部品 (terminal など) は自分で塗る。
    public static void Paint(Control root)
    {
        L.Apply(root);
        var t = Current;
        // 単独の window は枠と title bar も theme で描く (Vcl.ThemedFrame)。
        if (root is Form rf) ThemedFrame.Attach(rf);
        Walk(root);
        root.Invalidate(true);

        void Walk(Control c)
        {
            switch (c)
            {
                case Form f when f is not Utagoe.Forms.XpTerminalWindow and not InfoTip:
                    f.BackColor = t.Window;
                    f.ForeColor = t.Text;
                    break;
                case TextBoxBase or ComboBox or NumericUpDown or ListBox:
                    c.BackColor = t.Input;
                    c.ForeColor = t.InputText;
                    // どの theme も細い枠の平らな入力欄にそろえる (Windows の立体枠は theme の色にならないため)。
                    if (c is TextBox tbx) tbx.BorderStyle = BorderStyle.FixedSingle;
                    if (c is UpDownBase ud) ud.BorderStyle = BorderStyle.FixedSingle;
                    if (c is ComboBox cb) cb.FlatStyle = FlatStyle.Flat;
                    if (c is TextBox or ComboBox or NumericUpDown) Utagoe.Vcl.FocusLine.Attach(c);
                    break;
                case TabPage tp:
                    tp.UseVisualStyleBackColor = false;
                    tp.BackColor = t.Window;
                    tp.ForeColor = t.Text;
                    break;
                case Button b:
                    // button は BitBtn が theme の色で面を描くので、文字も theme の文字色。
                    b.ForeColor = t.Text;
                    break;
                case LinkLabel ll:
                    ll.LinkColor = ll.ActiveLinkColor = ll.VisitedLinkColor = t.Link;
                    ll.DisabledLinkColor = t.DisabledLink;
                    break;
                case TrackBar tb:
                    tb.BackColor = t.Window;
                    break;
                case Label l when "muted".Equals(l.Tag):
                    l.ForeColor = t.Muted;
                    break;
                case Label l when "link".Equals(l.Tag):
                    l.ForeColor = t.Link;
                    break;
            }
            if (c is Utagoe.Forms.TerminalView or Utagoe.Forms.XpTerminalWindow) { if (c is Utagoe.Forms.XpTerminalWindow x) x.ApplyTheme(); return; }
            foreach (Control child in c.Controls) Walk(child);
        }
    }
}
