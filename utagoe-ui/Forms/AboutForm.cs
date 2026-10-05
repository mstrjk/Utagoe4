// 元実装の version dialog を元にした画面。
// layout は TABOUTFORM resource 由来。
// v4: 著作表記を v4 のものにし、使っている第三者の部品の一覧 (Acknowledgements) を開閉できるようにした。
// 一覧は開くと dialog の中に伸び、各項目から配布元と同梱の license を開ける。
// app icon の色違い (theme) と言語もここで選ぶ。theme は押すとすぐに試せるが、保存は OK のときだけ。Cancel では元に戻す。言語は OK で反映する。

using Utagoe.App;
using Utagoe.Ui;

namespace Utagoe.Forms;

internal sealed class AboutForm : UiWindow
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

    private const string GitHubUrl = "https://github.com/mstrjk/Utagoe4";

    private static readonly WindowSpec Definition = new()
    {
        ClientSize = new Size(257, 240),
        Placement = Placement.CenterScreen,
        Title = () => "Utagoe",
    };

    private readonly Label _credit;
    private readonly LinkLabel _ackLink;
    private readonly LinkLabel _gitLink;
    private readonly Panel _ackPanel;
    private readonly BitBtn _ok;
    private readonly BitBtn _cancel;
    private readonly string _originalIcon = AppIcons.Current;
    private string _icon = AppIcons.Current;
    private string _language = L.Current;
    private bool _committed;
    private readonly PictureBox _logo;
    private readonly Label _ver;
    private readonly Label _iconCaption;
    private readonly Label _langCaption;
    private readonly ComboBox _lang;
    private readonly List<IconChoice> _iconChoices = new();
    private readonly ToolTip _tips = new();
    private bool _expanded;

    public AboutForm(Action<string> iconChosen, Action<string> languageChosen) : base(Definition)
    {
        BevelPainter.Attach(this, new Bevel(8, 8, 241, 190));

        _logo = Element.Picture(Art.ThemeLogo(), new Rectangle(16, 17, 225, 64), PictureBoxSizeMode.Zoom);
        _ver = Element.Caption(() => L.F("Version {0}", Program.Version), new Point(166, 80), 12F);
        _ver.BackColor = default;
        _credit = Element.Plain(() => Credit, new Rectangle(16, 104, 225, 90));

        _ackLink = Element.Link("", null);
        _ackLink.Location = new Point(16, 180);
        _ackLink.TabIndex = 1;
        _ackLink.LinkClicked += (_, _) => { _expanded = !_expanded; Relayout(); };

        _gitLink = Element.Link("GitHub: github.com/mstrjk/Utagoe4", GitHubUrl, 8);
        _gitLink.TabIndex = 4;

        _ackPanel = Element.ScrollArea(2, visible: false);

        _ok = Element.Ok(new Rectangle(50, 208, 75, 25), 0, closes: true);
        _cancel = Element.Cancel(new Rectangle(131, 208, 75, 25), 5);
        _cancel.DialogResult = DialogResult.None;
        AcceptButton = _ok;
        CancelButton = _cancel;
        _ok.Click += (_, _) =>
        {
            _committed = true;
            bool icon = _icon != _originalIcon, language = _language != L.Current;
            if (icon || language)
            {
                var swap = Transition.Begin(Transition.OpenWindows());
                if (icon) iconChosen(_icon);
                if (language) languageChosen(_language);
                swap.End(fade: false);
            }
            Close();
        };
        _cancel.Click += (_, _) => Close();

        _iconCaption = Element.Caption(() => L.T("Theme & Icon"), new Point(16, 170));
        foreach (string name in AppIcons.Names)
        {
            var choice = Element.Swatch(name, new Size(26, 26));
            choice.Chosen = name == _icon;
            choice.Click += (_, _) =>
            {
                _icon = name;
                AppIcons.Apply(name, updateShell: false);
                foreach (var c in _iconChoices)
                {
                    c.Chosen = c.IconName == _icon;
                    c.Invalidate();
                }
            };
            UiText.Tip(_tips, choice, () => AppIcons.DisplayName(name));
            _iconChoices.Add(choice);
        }

        _langCaption = Element.Caption(() => L.T("Language"), Point.Empty);
        _lang = Element.List(Rectangle.Empty, 3, L.Languages.Select(l => l.Name).ToArray());
        _lang.SelectedIndex = Math.Max(0, Array.FindIndex(L.Languages, l => l.Code == L.Current));
        _lang.SelectionChangeCommitted += (_, _) =>
        {
            if (_lang.SelectedIndex >= 0) _language = L.Languages[_lang.SelectedIndex].Code;
        };

        Controls.AddRange(new Control[] { _ok, _cancel, _logo, _ver, _credit, _gitLink, _iconCaption, _langCaption, _lang, _ackLink, _ackPanel });
        Theme.Changed += SwapLogo;
        Disposed += (_, _) => Theme.Changed -= SwapLogo;
        UiText.OnChange(this, Retranslate);
        Controls.AddRange(_iconChoices.ToArray());

        VclScaling.Apply(this);
        FillAcks();
        Relayout();
    }

    private void SwapLogo()
    {
        if (!_logo.IsDisposed) _logo.Image = Art.ThemeLogo();
    }

    private void Retranslate()
    {
        if (IsDisposed) return;
        int current = Math.Max(0, Array.FindIndex(L.Languages, l => l.Code == L.Current));
        if (_lang.SelectedIndex != current) _lang.SelectedIndex = current;
        _ackPanel.Controls.Clear();
        FillAcks();
        Relayout();
    }

    private void Relayout()
    {
        float s = _ok.Width / 75f;
        int S(int v) => (int)Math.Round(v * s);
        int left = _logo.Left, width = _logo.Width;

        Size text = TextRenderer.MeasureText(_credit.Text, _credit.Font, new Size(width, int.MaxValue), TextFormatFlags.WordBreak);
        _ver.Location = new Point(_logo.Right - _ver.PreferredSize.Width, _logo.Bottom + S(2));
        _credit.SetBounds(left, _ver.Bottom + S(8), width, text.Height);

        _gitLink.Location = new Point(left, _credit.Bottom + S(6));
        _iconCaption.Location = new Point(left, _gitLink.Bottom + S(10));
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
        _ok.Top = _cancel.Top = bevelBottom + S(10);
        ClientSize = new Size(ClientSize.Width, _ok.Bottom + S(7));
        Invalidate();
    }

    private void FillAcks()
    {
        int width = _logo.Width - SystemInformation.VerticalScrollBarWidth - 2;
        int y = 0;
        foreach (var a in Acks)
        {
            string line1 = $"{a.Name} · {a.LicenseName}";
            string line2 = $"{a.Role} - {a.Author}";
            var item = Element.Links(line1 + "\n" + line2,
                (0, a.Url.Length > 0 ? a.Name.Length : 0, a.Url),
                (a.Name.Length + 3, a.LicenseName.Length, a.License));
            item.UseMnemonic = true;
            item.Location = new Point(0, y);
            item.MaximumSize = new Size(width, 0);
            _ackPanel.Controls.Add(item);
            y = item.Bottom + 6;
        }
        var note = Element.Para(WindowsNote, tag: "muted", mnemonic: true);
        note.Location = new Point(0, y);
        note.MaximumSize = new Size(width, 0);
        _ackPanel.Controls.Add(note);
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        if (!_committed && AppIcons.Current != _originalIcon) AppIcons.Apply(_originalIcon, updateShell: false);
        base.OnFormClosing(e);
    }

    protected override void OnFormClosed(FormClosedEventArgs e)
    {
        _tips.Dispose();
        base.OnFormClosed(e);
    }
}
