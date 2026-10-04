// v4: Settings / Help (terminal) / About / Playback は modal にしない。開いたまま main window を操作できる。
// main window を最大化すると、Settings と terminal を main window の中に並べて常に出す。
// 上段は左から、枠で囲んだ元の main window の中身、Settings、説明欄 (Information)。3 つとも同じ高さで、中身は縦の中央。
// 説明欄は最初は歓迎の文、見出しの link に mouse を乗せるとその説明になり、次の link まで残る。terminal は下段に横いっぱい。
// 普通の大きさに戻すと、最大化の前に開いていたものだけ別 window に戻し、ほかは閉じる (button でまた開ける)。

using Utagoe.App;
using Utagoe.Vcl;

namespace Utagoe.Forms;

internal sealed partial class MainForm
{
    private SettingsForm? _settingsForm;
    private AboutForm? _aboutForm;
    private VclGroupBox? _settingsFrame;
    private VclGroupBox? _infoFrame;
    private InfoPane? _infoPane;
    private Rectangle _mainFrameBounds;   // 最大化中に main window の中身を囲む枠 (form に直接描く)
    private bool _docked;
    private bool _settingsWasOpen, _terminalWasOpen;
    private Size _mainArea;   // 元の main window の中身の大きさ (倍率 1)。最大化中はこれを拡大して左上に置く
    private UiZoom? _mainZoom, _settingsZoom;

    private const int Gap = 8;
    // 最大化したときの拡大の上限。大きすぎると読みにくいので 1.5 倍まで。余った高さは terminal に回す。
    private const float MaxZoom = 1.5f;

    private BitBtn? _resetBtn;
    private ResultsPanel? _results;
    private List<string> _lastOutputs = new();

    private void InitPanels()
    {
        // 最大化中だけ出す Reset (Help... の位置)。Settings と terminal は画面に出ているので、その 2 つの button は隠す。
        _resetBtn = new BitBtn { Bounds = HelpBtn.Bounds, Text = Messages.Reset, Visible = false, TabIndex = HelpBtn.TabIndex };
        _resetBtn.SetGlyph("reset");
        _resetBtn.Click += (_, _) => _settingsForm?.ResetToDefaults();
        Controls.Add(_resetBtn);
        _mainArea = ClientSize;
        _mainZoom = new UiZoom(this);
        Paint += PaintMainFrame;
        Resize += (_, _) => OnMainResize();
    }


    private void ShowSettings()
    {
        if (_docked)
        {
            if (_settingsForm == null) DockSettings();
            _settingsForm?.Focus();
            return;
        }
        if (_settingsForm is { IsDisposed: false })
        {
            _settingsForm.Activate();
            return;
        }
        CreateSettings().Show(this);
    }

    private SettingsForm CreateSettings()
    {
        var f = new SettingsForm(() => _settings.Values, () => Edit1.Text.Trim());
        f.Applied += OnSettingsApplied;
        f.FormClosed += (_, _) => { if (_settingsForm == f) _settingsForm = null; };
        return _settingsForm = f;
    }

    private void OnSettingsApplied(Native.CoreSettings values)
    {
        _settings.Values = values;
        _settings.Save();
        ApplyOutputKind();

    }

    private void ShowTerminal()
    {
        if (_docked)
        {
            if (TerminalForm.Current == null) DockTerminal();
            TerminalForm.Current?.Focus();
            return;
        }
        TerminalForm.ShowFor(this);
    }

    private void ShowAbout()
    {
        if (_aboutForm is { IsDisposed: false })
        {
            _aboutForm.Activate();
            return;
        }
        _aboutForm = new AboutForm(name =>
        {
            // About で選んだ app icon の色違いを覚え、すぐに全部の window へ反映する。
            _settings.Values.AppIcon = name;
            _settings.Save();
            AppIcons.Apply(name, updateShell: true);
        }, code =>
        {
            _settings.Values.Language = code;
            _settings.Save();
            L.Set(code);
        });
        _aboutForm.Show(this);
    }


    private void OnMainResize()
    {
        if (WindowState == FormWindowState.Minimized) return;
        bool want = WindowState == FormWindowState.Maximized;
        if (want && !_docked) DockAll();
        else if (!want && _docked) UndockAll();
        else if (_docked) LayoutDocked();
    }

    private void DockAll()
    {
        _settingsWasOpen = _settingsForm is { IsDisposed: false };
        _terminalWasOpen = TerminalForm.Current != null;
        _docked = true;
        SuspendLayout();
        DockSettings();
        DockTerminal();
        DockInfo();
        _results = new ResultsPanel();
        _results.SetFiles(_lastOutputs);
        Controls.Add(_results);
        SetBitBtn.Visible = HelpBtn.Visible = false;
        if (_resetBtn != null) _resetBtn.Visible = true;
        ResumeLayout(true);
        LayoutDocked();
    }

    private void DockInfo()
    {
        _infoPane = new InfoPane();
        _infoPane.ShowText(Messages.Welcome);
        _infoFrame = new VclGroupBox { Text = Messages.InfoPanel };
        _infoFrame.Controls.Add(_infoPane);
        Controls.Add(_infoFrame);
        // 最大化中は、見出しの link の説明を popup ではなくこの欄に出す。
        HelpLinks.Sink = text => _infoPane?.ShowText(text);
    }

    private void RemoveInfo()
    {
        HelpLinks.Sink = null;
        if (_infoFrame == null) return;
        Controls.Remove(_infoFrame);
        _infoFrame.Dispose();
        _infoFrame = null;
        _infoPane = null;
    }

    // 最大化中だけ、元の main window の中身を Settings と同じ形の枠で囲む。
    private void PaintMainFrame(object? sender, PaintEventArgs e)
    {
        if (!_docked || _mainFrameBounds.IsEmpty) return;
        VclGroupBox.DrawFrame(e.Graphics, _mainFrameBounds, Messages.MainPanel, Font, Theme.Current.Text, BackColor);
    }

    private void DockSettings()
    {
        var f = _settingsForm is { IsDisposed: false } ? _settingsForm : CreateSettings();
        Embed(f);
        f.Docked = true;
        _settingsZoom = new UiZoom(f);
        _settingsFrame = new VclGroupBox { Text = Messages.SettingsPanel };
        _settingsFrame.Controls.Add(f);
        f.Location = new Point(Gap, LogicalToDeviceUnits(18));
        f.FormClosed += (_, _) => RemoveSettingsFrame();
        Controls.Add(_settingsFrame);
        f.Show();
        LayoutDocked();
    }

    private void DockTerminal()
    {
        var t = TerminalForm.Create(this);
        Embed(t);
        Controls.Add(t);
        t.Show();
        LayoutDocked();
    }

    // form を別 window から main window の部品に変える。
    private static void Embed(Form f)
    {
        if (f.Visible) f.Hide();
        f.Owner = null;
        f.TopLevel = false;
        f.FormBorderStyle = FormBorderStyle.None;
    }

    private void UndockAll()
    {
        _docked = false;
        SuspendLayout();
        RemoveInfo();
        if (_results != null)
        {
            Controls.Remove(_results);
            _results.Dispose();
            _results = null;
        }
        _mainFrameBounds = Rectangle.Empty;
        SetBitBtn.Visible = HelpBtn.Visible = true;
        if (_resetBtn != null) _resetBtn.Visible = false;
        _mainZoom?.Apply(1f, resizeRoot: false);
        _settingsZoom?.Apply(1f, resizeRoot: true);
        _settingsZoom = null;
        if (_settingsForm is { IsDisposed: false } s)
        {
            Unembed(s, FormBorderStyle.FixedSingle);
            RemoveSettingsFrame();
            s.Docked = false;
            if (_settingsWasOpen) { s.StartPosition = FormStartPosition.CenterParent; s.Show(this); }
            else s.Close();
        }
        if (TerminalForm.Current is { } t)
        {
            Unembed(t, FormBorderStyle.None);   // terminal は Windows の枠を持たない (見出しが title bar)
            if (_terminalWasOpen) { t.StartPosition = FormStartPosition.CenterParent; t.Show(this); }
            else t.Close();
        }
        ResumeLayout(true);
    }

    private static void Unembed(Form f, FormBorderStyle border)
    {
        f.Hide();
        f.Parent?.Controls.Remove(f);
        f.TopLevel = true;
        f.FormBorderStyle = border;
    }

    private void RemoveSettingsFrame()
    {
        _settingsZoom = null;
        if (_settingsFrame == null) return;
        Controls.Remove(_settingsFrame);
        _settingsFrame.Dispose();
        _settingsFrame = null;
    }

    // 上段: 枠で囲んだ元の main window の中身、Settings、説明欄。中身の 2 つは同じ倍率で、収まる範囲で最大 MaxZoom 倍。
    // 3 つの枠は同じ高さにそろえ、中身は枠の中で縦の中央に置く。説明欄は残りの幅を使う。
    // 下段: terminal を横いっぱいに置く。高さは画面の 1/4 以上を残す。
    private void LayoutDocked()
    {
        if (!_docked || _mainZoom == null) return;
        Size client = ClientSize;
        int gap = LogicalToDeviceUnits(Gap);
        int header = LogicalToDeviceUnits(18);
        bool hasSettings = _settingsFrame != null && _settingsForm is { IsDisposed: false } && _settingsZoom != null;
        Size set0 = hasSettings ? _settingsZoom!.BaseSize : Size.Empty;
        int termMin = Math.Max(LogicalToDeviceUnits(180), client.Height / 4);
        int infoMin = LogicalToDeviceUnits(220);

        // 枠の余白は倍率に含めず、残りの幅と高さに収まる倍率を選ぶ。
        int fixedW = gap * 2 + gap * 2 + (hasSettings ? gap * 3 : 0) + gap + infoMin;
        int fixedH = gap + header + gap + gap + termMin + gap;
        float kw = (client.Width - fixedW) / (float)(_mainArea.Width + set0.Width);
        float kh = (client.Height - fixedH) / (float)Math.Max(_mainArea.Height, set0.Height);
        float k = Math.Clamp(Math.Min(kw, kh), 1f, MaxZoom);
        int headerK = (int)Math.Round(header * k);
        int mainW = (int)Math.Round(_mainArea.Width * k), mainH = (int)Math.Round(_mainArea.Height * k);

        Size setSize = Size.Empty;
        if (hasSettings)
        {
            _settingsZoom!.Apply(k, resizeRoot: true);
            setSize = _settingsForm!.Size;
        }
        int resultsMin = _results != null ? (int)Math.Round(LogicalToDeviceUnits(96) * k) : 0;
        int inner = Math.Max(mainH + (resultsMin > 0 ? resultsMin + gap : 0), setSize.Height);
        int rowH = headerK + inner + gap;

        _mainFrameBounds = new Rectangle(gap, gap, mainW + gap * 2, rowH);
        var offset = new Point(_mainFrameBounds.X + gap, _mainFrameBounds.Y + headerK + (_results != null ? 0 : (inner - mainH) / 2));
        _mainZoom.Apply(k, resizeRoot: false, offset);
        if (_results != null)
        {
            int pad = (int)Math.Round(LogicalToDeviceUnits(Gap) * k);
            int resultsTop = offset.Y + mainH + gap;
            _results.Font = Font;
            _results.Bounds = new Rectangle(offset.X + pad, resultsTop, Math.Max(0, mainW - pad * 2), Math.Max(0, _mainFrameBounds.Bottom - gap - resultsTop));
        }
        int right = _mainFrameBounds.Right;

        if (hasSettings)
        {
            var s = _settingsForm!;
            s.Location = new Point(gap, headerK + (inner - setSize.Height) / 2);
            _settingsFrame!.Bounds = new Rectangle(right + gap, gap, setSize.Width + gap * 2, rowH);
            right = _settingsFrame.Right;
        }

        if (_infoFrame != null && _infoPane != null)
        {
            int w = client.Width - gap - (right + gap);
            _infoFrame.Visible = w >= infoMin / 2;
            _infoFrame.Bounds = new Rectangle(right + gap, gap, Math.Max(0, w), rowH);
            _infoPane.Font = Font;
            _infoPane.Bounds = new Rectangle(gap * 2, headerK + gap, Math.Max(0, w - gap * 4), Math.Max(0, rowH - headerK - gap * 2));
        }

        int top = gap + rowH;
        var termArea = Rectangle.FromLTRB(gap, top + gap, client.Width - gap, client.Height - gap);
        if (TerminalForm.Current is { TopLevel: false } t && termArea.Width > 0 && termArea.Height > 0)
            t.Bounds = termArea;
        Invalidate();
    }
}
