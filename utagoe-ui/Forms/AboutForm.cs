// 元実装の version dialog を元にした画面。
// layout は TABOUTFORM resource 由来。起動時 alpha 150 から 50 ms timer で fade-in する。
// v4: 著作表記を v4 のものにし、使っている第三者の部品の一覧 (Acknowledgements) を開閉できるようにした。
// 一覧は開くと dialog の中に伸び、各項目から配布元と同梱の license を開ける。
// app icon の色違いもここで選ぶ。選んでいるものは青い枠で示し、押すとすぐに全部の window の icon が替わる。

using System.Diagnostics;
using Utagoe.App;
using Utagoe.Vcl;

namespace Utagoe.Forms;

internal sealed class AboutForm : Form
{
    private static string Credit =>
        "©2026 teacommontea\r\n\r\n" +
        L.T("Based on the original work and ideas of TODAKEN.") + "\r\n\r\n" +
        L.T("Utagoe is a free-to-use software application for vocal extraction by instrumental subtraction.");

    /// 同梱・静的 link している第三者の部品。License は licenses\ の中の path (無ければ Web の license)。
    private sealed record Ack(string Name, string Role, string Author, string LicenseName, string Url, string License);

    private static readonly Ack[] Acks =
    {
        new("libogg 1.3.6", "Ogg container", "Xiph.Org Foundation", "BSD-3-Clause", "https://xiph.org/ogg/", @"libogg-1.3.6\COPYING"),
        new("libvorbis 1.3.7", "Ogg Vorbis", "Xiph.Org Foundation", "BSD-3-Clause", "https://xiph.org/vorbis/", @"libvorbis-1.3.7\COPYING"),
        new("Opus 1.6.1", "Opus codec", "Xiph.Org Foundation and contributors", "BSD-3-Clause", "https://opus-codec.org/", @"opus-1.6.1\COPYING"),
        new("opusfile 0.12", "Opus decoding", "Xiph.Org Foundation", "BSD-3-Clause", "https://opus-codec.org/", @"opusfile-0.12\COPYING"),
        new("libopusenc 0.2.1", "Opus encoding", "Xiph.Org Foundation", "BSD-3-Clause", "https://opus-codec.org/", @"libopusenc-0.2.1\COPYING"),
        new("libFLAC 1.5.0", "FLAC", "Josh Coalson, Xiph.Org Foundation", "BSD-3-Clause", "https://xiph.org/flac/", @"flac-1.5.0\COPYING.Xiph"),
        new("dr_wav, dr_mp3", "WAV and MP3", "David Reid", "Unlicense or MIT-0", "https://github.com/mackron/dr_libs", @"dr_libs\LICENSE"),
        new("minimp3", "MP3 decoding (inside dr_mp3)", "lieff", "CC0-1.0", "https://github.com/lieff/minimp3",
            "https://github.com/lieff/minimp3/blob/master/LICENSE"),
        new(".NET runtime, Windows Forms", "Application runtime", "Microsoft, .NET Foundation", "MIT", "https://dotnet.microsoft.com/",
            "https://github.com/dotnet/runtime/blob/main/LICENSE.TXT"),
        new("GCC runtime (libgcc, libstdc++)", "C++ runtime", "Free Software Foundation", "GPL-3.0 + runtime exception",
            "https://gcc.gnu.org/", "https://www.gnu.org/licenses/gcc-exception-3.1.html"),
        new("MinGW-w64 runtime", "Windows C runtime startup", "MinGW-w64 project", "ZPL-2.1 and others", "https://www.mingw-w64.org/",
            "https://sourceforge.net/p/mingw-w64/mingw-w64/ci/master/tree/COPYING"),
    };

    private static string WindowsNote => L.T("Also uses Media Foundation and Direct3D 11, which are part of Windows.");

    private readonly System.Windows.Forms.Timer _fade = new() { Interval = 50 };
    private readonly Label _credit;
    private readonly LinkLabel _ackLink;
    private readonly Panel _ackPanel;
    private readonly BitBtn _ok;
    private readonly PictureBox _logo;
    private readonly Label _ver;
    private readonly Label _iconCaption;
    private readonly Label _langCaption;
    private readonly ComboBox _lang;
    private readonly List<IconChoice> _iconChoices = new();
    private readonly ToolTip _tips = new();
    private bool _expanded;

    /// iconChosen は app icon の色違いが選ばれたときに呼ばれる (設定への保存と反映は main window が行う)。
    public AboutForm(Action<string> iconChosen, Action<string> languageChosen)
    {
        AutoScaleDimensions = new SizeF(96F, 96F);
        AutoScaleMode = AutoScaleMode.Dpi;
        ClientSize = new Size(257, 240);
        Font = new Font("Tahoma", 9F);
        FormBorderStyle = FormBorderStyle.FixedSingle;
        MaximizeBox = false;
        MinimizeBox = false;
        ShowInTaskbar = false;
        StartPosition = FormStartPosition.CenterScreen;
        Text = "Utagoe";
        Icon = VclGlyph.AppIcon;
        Opacity = 150 / 255.0;

        BevelPainter.Attach(this, new Bevel(8, 8, 241, 190));

        _logo = new PictureBox
        {
            Bounds = new Rectangle(16, 17, 225, 64),
            // v4 の logo は縁がなめらかな alpha 付き BMP。どの theme の背景でも縁が浮かないよう alpha のまま描く。
            Image = ThemedLogo(),
            // logo は native size ではなく image box に合わせて拡大表示される。
            SizeMode = PictureBoxSizeMode.Zoom,
            BackColor = Color.Transparent,
        };

        _ver = new Label
        {
            Text = L.F("Version {0}", "4.0"),
            Location = new Point(166, 80),
            AutoSize = true,
            Font = new Font("Tahoma", 12F),
        }.WithOwnFont();

        _credit = new Label { Text = Credit, Bounds = new Rectangle(16, 104, 225, 90), BackColor = Color.Transparent };

        _ackLink = new LinkLabel
        {
            AutoSize = true,
            Location = new Point(16, 180),
            BackColor = Color.Transparent,
            LinkColor = HelpLinks.LinkColor,
            ActiveLinkColor = HelpLinks.LinkColor,
            VisitedLinkColor = HelpLinks.LinkColor,
            LinkBehavior = LinkBehavior.AlwaysUnderline,
            TabIndex = 1,
        };
        _ackLink.LinkClicked += (_, _) => { _expanded = !_expanded; Relayout(); };

        _ackPanel = new Panel { AutoScroll = true, Visible = false, BackColor = Color.Transparent, TabIndex = 2 };

        _ok = new BitBtn { Bounds = new Rectangle(88, 208, 75, 25), TabIndex = 0 };
        _ok.SetKind(BitBtnKind.OK);
        AcceptButton = _ok;
        CancelButton = _ok;
        _ok.DialogResult = DialogResult.None;
        _ok.Click += (_, _) => Close();

        _iconCaption = new Label { Text = "App Icon", AutoSize = true, Location = new Point(16, 170), BackColor = Color.Transparent };
        foreach (string name in AppIcons.Names)
        {
            var choice = new IconChoice(name) { Size = new Size(26, 26), TabStop = true };
            choice.Click += (_, _) =>
            {
                iconChosen(name);
                foreach (var c in _iconChoices) c.Invalidate();
            };
            _tips.SetToolTip(choice, AppIcons.DisplayName(name));
            _iconChoices.Add(choice);
        }

        _langCaption = new Label { Text = "Language", AutoSize = true, BackColor = Color.Transparent };
        _lang = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, TabIndex = 3 };
        foreach (var (_, name) in L.Languages) _lang.Items.Add(name);
        _lang.SelectedIndex = Math.Max(0, Array.FindIndex(L.Languages, l => l.Code == L.Current));
        _lang.SelectionChangeCommitted += (_, _) =>
        {
            if (_lang.SelectedIndex >= 0) languageChosen(L.Languages[_lang.SelectedIndex].Code);
        };

        Controls.AddRange(new Control[] { _ok, _logo, _ver, _credit, _iconCaption, _langCaption, _lang, _ackLink, _ackPanel });
        Theme.Changed += SwapLogo;
        L.Changed += Retranslate;
        Disposed += (_, _) => { Theme.Changed -= SwapLogo; L.Changed -= Retranslate; };
        Controls.AddRange(_iconChoices.ToArray());

        _fade.Tick += (_, _) =>
        {
            Opacity = Math.Min(1.0, Opacity + 15 / 255.0);
            if (Opacity >= 1.0) _fade.Stop();
        };

        VclScaling.Apply(this);
        FillAcks();
        Relayout();
        Theme.Paint(this);
    }

    // 著作表記の高さと一覧の開閉に合わせて、下の部品・枠・dialog の高さを決め直す。
    private static Image ThemedLogo() => VclGlyph.ThemeLogo();

    private void SwapLogo()
    {
        if (!_logo.IsDisposed) _logo.Image = ThemedLogo();
    }

    private void Retranslate()
    {
        if (IsDisposed) return;
        _credit.Text = Credit;
        _ver.Text = L.F("Version {0}", "4.0");
        _ackPanel.Controls.Clear();
        FillAcks();
        foreach (var c in _iconChoices) _tips.SetToolTip(c, AppIcons.DisplayName(c.IconName));
        Relayout();
    }

    private void Relayout()
    {
        float s = _ok.Width / 75f;            // 設計時の 1 px の今の大きさ (font scaling 後)
        int S(int v) => (int)Math.Round(v * s);
        int left = _logo.Left, width = _logo.Width;

        Size text = TextRenderer.MeasureText(_credit.Text, _credit.Font, new Size(width, int.MaxValue), TextFormatFlags.WordBreak);
        // Version は logo の真下に右寄せ (新しい logo は背が高く、元の位置では重なる)。著作表記はその下から。
        _ver.Location = new Point(_logo.Right - _ver.PreferredSize.Width, _logo.Bottom + S(2));
        _credit.SetBounds(left, _ver.Bottom + S(8), width, text.Height);

        _iconCaption.Location = new Point(left, _credit.Bottom + S(10));
        int cell = Math.Min(S(26), (width - S(2) * (_iconChoices.Count - 1)) / _iconChoices.Count);
        int iconsTop = _iconCaption.Bottom + S(3);
        for (int i = 0; i < _iconChoices.Count; i++)
            _iconChoices[i].SetBounds(left + i * (cell + S(2)), iconsTop, cell, cell);

        _langCaption.Location = new Point(left, iconsTop + cell + S(10));
        _lang.SetBounds(left, _langCaption.Bottom + S(3), width, _lang.Height);

        _ackLink.Text = (_expanded ? "▼ " : "► ") + L.T("Acknowledgements");
        _ackLink.LinkArea = new LinkArea(0, _ackLink.Text.Length);
        _ackLink.Location = new Point(left, _lang.Bottom + S(10));
        int y = _ackLink.Bottom + S(4);

        _ackPanel.Visible = _expanded;
        if (_expanded)
        {
            int content = _ackPanel.Controls.Cast<Control>().Select(c => c.Bottom).DefaultIfEmpty(0).Max() + S(2);
            _ackPanel.SetBounds(left, y, width, Math.Min(content, S(220)));
            y = _ackPanel.Bottom + S(4);
        }

        int bevelBottom = y + S(4);
        BevelPainter.Replace(this, new Bevel(S(8), S(8), S(241), bevelBottom - S(8)));
        _ok.Top = bevelBottom + S(10);
        ClientSize = new Size(ClientSize.Width, _ok.Bottom + S(7));
        Invalidate();
    }

    // 一覧の各項目: 1 行目は名前 (配布元への link) と license (同梱の license 文への link)、2 行目は用途と作者。
    private void FillAcks()
    {
        int width = _logo.Width - SystemInformation.VerticalScrollBarWidth - 2;
        int y = 0;
        foreach (var a in Acks)
        {
            string line1 = $"{a.Name} · {a.LicenseName}";
            string line2 = $"{a.Role} - {a.Author}";
            var item = new LinkLabel
            {
                Text = line1 + "\n" + line2,
                Location = new Point(0, y),
                MaximumSize = new Size(width, 0),
                AutoSize = true,
                BackColor = Color.Transparent,
                LinkColor = HelpLinks.LinkColor,
                ActiveLinkColor = HelpLinks.LinkColor,
                VisitedLinkColor = HelpLinks.LinkColor,
                LinkBehavior = LinkBehavior.AlwaysUnderline,
            };
            item.Links.Clear();
            if (a.Url.Length > 0) item.Links.Add(0, a.Name.Length, a.Url);
            item.Links.Add(a.Name.Length + 3, a.LicenseName.Length, a.License);
            item.LinkClicked += (_, e) => Open(e.Link?.LinkData as string);
            _ackPanel.Controls.Add(item);
            y = item.Bottom + 6;
        }
        var note = new Label
        {
            Text = WindowsNote,
            Location = new Point(0, y),
            MaximumSize = new Size(width, 0),
            AutoSize = true,
            Tag = "muted",
            BackColor = Color.Transparent,
        };
        _ackPanel.Controls.Add(note);
    }

    // Web の link は既定の browser で、同梱の license 文はメモ帳で開く。
    private static void Open(string? target)
    {
        if (string.IsNullOrEmpty(target)) return;
        try
        {
            if (target.StartsWith("https://", StringComparison.Ordinal))
            {
                Process.Start(new ProcessStartInfo(target) { UseShellExecute = true });
                return;
            }
            string file = Path.Combine(AppPaths.Licenses, target);
            if (File.Exists(file)) Process.Start(new ProcessStartInfo("notepad.exe", $"\"{file}\"") { UseShellExecute = true });
            else LogHub.Warning($"license file not found: {file}");
        }
        catch (Exception ex) when (ex is System.ComponentModel.Win32Exception or InvalidOperationException)
        {
            LogHub.Exception("could not open " + target, ex);
        }
    }

    protected override void OnShown(EventArgs e)
    {
        base.OnShown(e);
        _fade.Start();
    }

    protected override void OnFormClosed(FormClosedEventArgs e)
    {
        _fade.Dispose();
        _tips.Dispose();
        base.OnFormClosed(e);
    }

    /// app icon の色違いを 1 つ表す枠。選んでいるものは青い枠と薄い青の背景、mouse が乗ると薄い枠。
    private sealed class IconChoice : Control
    {
        private static readonly Color Selected = Color.FromArgb(0, 120, 215);
        private static readonly Color SelectedBack = Color.FromArgb(204, 228, 247);
        private readonly string _name;
        private bool _hot;

        public string IconName => _name;

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
            bool selected = AppIcons.Current == _name;
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
}
