// 元実装の settings dialog の挙動を再現する。
// 表示時に設定を control へコピーし、Reset で factory default、trackbar change で表示更新、変えた設定はすぐに保存する。
// v4: modal ではなくなった。開いたまま main window を操作できる。別 window (Close で閉じる) か、
// main window を最大化したときはその中に組み込まれる。組み込み中は button を持たず、変えた設定はすぐに保存される
// (Reset は main window の側に出る)。

using System.Globalization;
using Utagoe.App;
using Utagoe.Native;
using Utagoe.Ui;

namespace Utagoe.Forms;

internal sealed partial class SettingsForm : UiWindow
{
    private static readonly WindowSpec Definition = new()
    {
        ClientSize = new Size(600, 452),
        Buttons = TitleButtons.None,
        Placement = Placement.CenterScreen,
        Title = () => L.T("Settings"),
    };

    private readonly Func<CoreSettings> _current;
    private readonly Func<string> _originalPath;
    private readonly WindowSlot<SpansForm> _spans = new();

    private void OpenSpansPicker() => _spans.Show(this, () => new SpansForm(_originalPath(), SpansEdit.Text, text => SpansEdit.Text = text));
    private readonly HelpLinks _links = new();
    private bool _docked;

    /// OK / Apply で確定した設定。main window が保存して使う。
    public event Action<CoreSettings>? Applied;

    /// current は今の設定を返す。OK のときはその上に画面の値を書く (画面に無い設定、例えば通知の表示可否は保つ)。
    public SettingsForm(Func<CoreSettings> current, Func<string> originalPath) : base(Definition)
    {
        _current = current;
        _originalPath = originalPath;
        InitializeComponent();
        ScaleToFont();

        KvolTrackBar.ValueChanged   += (_, _) => KvolText.Text   = TrackLabels.ExtractLevel(KvolTrackBar.Value);
        KlvlTrackBar.ValueChanged   += (_, _) => KlvlText.Text   = TrackLabels.InstLevel(KlvlTrackBar.Value);
        CfocusTrackBar.ValueChanged += (_, _) => CfocusText.Text = TrackLabels.Centralize(CfocusTrackBar.Value);
        LPFTrackBar.ValueChanged    += (_, _) => LPFLabel.Text   = TrackLabels.LowPass(LPFTrackBar.Value);
        HPFTrackBar.ValueChanged    += (_, _) => HPFLabel.Text   = TrackLabels.HighPass(HPFTrackBar.Value);

        SpansPickButton.Click += (_, _) => OpenSpansPicker();
        ResetButton.Click += (_, _) => ResetToDefaults();
        OkBitBtn.Click += (_, _) => Finish();
        OutFormatCombo.SelectedIndexChanged += (_, _) => UpdateOutputControls();
        GpuCheckBox.CheckedChanged += (_, _) => GpuExactButton.Enabled = GpuFastButton.Enabled = GpuCheckBox.Checked;
        // 親の選択が変わったら、使われなくなる子の設定を灰色にする。
        foreach (var g in new[] { MethodRadioGroup, LevelRadioGroup, ModelRadioGroup, AlignRadioGroup, FreqModelRadioGroup })
            g.ItemIndexChanged += (_, _) => UpdateEnabled();
        foreach (var c in new[] { OvspCheckBox, CfocusCheckBox, LPFCheckBox, HPFCheckBox, VnameCheckBox })
            c.CheckedChanged += (_, _) => UpdateEnabled();
        AdptManualButton.CheckedChanged += (_, _) => UpdateEnabled();
        SaveDropDown.CheckedChanged += (_, _) => UpdateEnabled();
        for (int k = 0; k < UseMethodButtons.Length; ++k)
        {
            int m = k;
            UseMethodButtons[k].CheckedChanged += (_, _) =>
            {
                if (UseMethodButtons[m].Checked && MethodRadioGroup.ItemIndex != m) MethodRadioGroup.ItemIndex = m;
            };
        }
        MethodRadioGroup.ItemIndexChanged += (_, _) => SyncMethodButtons();
        PageControl.SelectedIndexChanged += (_, _) => PlaceShared();
        foreach (var inner in new[] { WavePages, FreqPages })
        {
            inner.Dock = DockStyle.None;
            var host = inner.Parent!;
            host.Layout += (_, _) => CenterInner(inner);
            inner.SelectedIndexChanged += (_, _) => CenterInner(inner);
        }
        Populate(current());
        if (UseMethodButtons[Math.Max(0, MethodRadioGroup.ItemIndex)].Parent?.Parent is TabPage active) PageControl.SelectedTab = active;
        PlaceShared();
        _liveApply.Tick += (_, _) => ApplyNow();
        HookLiveApply(this);
        AttachTips();
        UiText.OnChange(this, () => { UpdateEnabled(); ShowGpuStatus(); });
        FormClosed += (_, _) => { _links.Dispose(); _liveApply.Dispose(); };
        ShowGpuStatus();
    }

    private void CenterInner(TabControl inner)
    {
        var host = inner.Parent;
        if (host == null) return;
        int right = 0, bottom = 0, left = int.MaxValue, top = int.MaxValue;
        foreach (Control c in inner.TabPages.Cast<TabPage>().SelectMany(p => p.Controls.Cast<Control>()).Concat(new Control[] { SharedMain, SharedSettings }).Distinct())
        {
            right = Math.Max(right, c.Right);
            bottom = Math.Max(bottom, c.Bottom);
            left = Math.Min(left, c.Left);
            top = Math.Min(top, c.Top);
        }
        if (right == 0) return;
        Rectangle area = host.ClientRectangle;
        foreach (Control c in host.Controls)
            if (c != inner && c.Visible && c.Dock == DockStyle.Top) { area.Y = Math.Max(area.Y, c.Bottom); }
        area.Height = host.ClientSize.Height - area.Y;
        Rectangle display = inner.DisplayRectangle;
        int chromeW = inner.Width - display.Width, chromeH = inner.Height - display.Height;
        int w = Math.Min(area.Width, right + left + chromeW);
        int h = Math.Min(area.Height, bottom + top + chromeH);
        var bounds = new Rectangle(area.X + (area.Width - w) / 2, area.Y + (area.Height - h) / 2, w, h);
        if (inner.Bounds != bounds) inner.Bounds = bounds;
    }

    private Size _fullClient;
    private bool _populating;
    private readonly System.Windows.Forms.Timer _liveApply = new() { Interval = 250 };

    /// main window に組み込むときは Close / Reset を隠し、その分だけ高さを詰める。
    [System.ComponentModel.DesignerSerializationVisibility(System.ComponentModel.DesignerSerializationVisibility.Hidden)]
    public bool Docked
    {
        get => _docked;
        set
        {
            if (_docked == value) return;
            _docked = value;
            OkBitBtn.Visible = ResetButton.Visible = !value;
            CancelButton = value ? null : OkBitBtn;
            if (value)
            {
                _fullClient = ClientSize;
                ClientSize = new Size(ClientSize.Width, PageControl.Bottom + PageControl.Top);
            }
            else if (!_fullClient.IsEmpty)
            {
                ClientSize = _fullClient;
            }
        }
    }

    public void ResetToDefaults()
    {
        var answer = MessageForm.Show(TopLevelControl ?? this, Messages.ResetConfirm,
                                      Messages.Title, MessageBoxButtons.OKCancel, MessageBoxIcon.Question,
                                      heading: Messages.ResetTitle, confirm: Messages.Reset);
        if (answer != DialogResult.OK) return;
        Populate(AppSettings.Defaults());
        ApplyNow();
    }

    // どの設定を変えてもすぐに (少し待ってまとめて) 保存する。
    private void HookLiveApply(Control c)
    {
        void Changed(object? sender, EventArgs e)
        {
            if (_populating) return;
            _liveApply.Stop();
            _liveApply.Start();
        }
        switch (c)
        {
            case VclRadioGroup g: g.ItemIndexChanged += Changed; break;
            case RadioButton rb when rb.Parent is not VclRadioGroup: rb.CheckedChanged += Changed; break;
            case CheckBox cb: cb.CheckedChanged += Changed; break;
            case TrackBar tb: tb.ValueChanged += Changed; break;
            case NumericUpDown nud: nud.ValueChanged += Changed; break;
            case ComboBox cmb: cmb.SelectedIndexChanged += Changed; cmb.TextChanged += Changed; break;
            case TextBox tx: tx.TextChanged += Changed; break;
        }
        foreach (Control child in c.Controls) HookLiveApply(child);
    }

    // 区間の書式が読めないうちは保存しない (直し終わったときに保存される)。
    private void ApplyNow()
    {
        _liveApply.Stop();
        if (!Core.CheckSpans(SpansEdit.Text.Trim(), out _)) return;
        Applied?.Invoke(Collect(_current()));
    }

    /// 別の所 (保存 dialog) で出力形式が変わったら、画面にも反映する。
    public void SyncOutputFormat(int format) =>
        OutFormatCombo.SelectedIndex = Math.Clamp(format, 0, OutFormatCombo.Items.Count - 1);

    // 区間の書式が読めないまま閉じないよう、先に確かめる。
    private void Finish()
    {
        if (!Core.CheckSpans(SpansEdit.Text.Trim(), out string error))
        {
            Utagoe.Forms.MessageForm.Show(this, Messages.BadSpans(error), Messages.Title, MessageBoxButtons.OK, MessageBoxIcon.Warning);
            PageControl.SelectedTab = WaveTab;
            WavePages.SelectedTab = WaveMainPage;
            SpansEdit.Focus();
            return;
        }
        ApplyNow();
        Close();
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        if (_liveApply.Enabled) ApplyNow();
        base.OnFormClosing(e);
    }

    // GPU の初期化は時間がかかることがあるので、dialog を出してから裏で調べて表示する。
    private async void ShowGpuStatus()
    {
        var gpu = await Task.Run(() => Core.Gpu);
        if (IsDisposed) return;
        GpuStatusLabel.Text = gpu.Available ? Messages.GpuUsing(gpu.Adapter) : Messages.GpuUnavailable(gpu.Reason);
    }

    public void Reload() => Populate(_current());

    private void Populate(CoreSettings s)
    {
        _populating = true;
        try { PopulateCore(s); }
        finally { _populating = false; }
    }

    private void PopulateCore(CoreSettings s)
    {
        IntroRadioGroup.ItemIndex = s.IntroMode;
        MethodRadioGroup.ItemIndex = s.MergeMode == 1 ? 1 : 0;
        SoundQtyGroup.ItemIndex   = s.SoundQty;
        LevelRadioGroup.ItemIndex = s.LevelAdpt;
        DataRadioGroup.ItemIndex  = s.ProcMode;
        PhaseRadioGroup.ItemIndex = s.KrkPhase;

        AdptAutoButton.Checked   = s.AdptMode == 0;
        AdptManualButton.Checked = s.AdptMode != 0;
        AdptUpDown.Value = Math.Clamp(s.AdptRange, (int)AdptUpDown.Minimum, (int)AdptUpDown.Maximum);

        SetTrack(KvolTrackBar, s.ExtractLevel);
        SetTrack(KlvlTrackBar, s.InstLevel);
        SetTrack(CfocusTrackBar, s.CentralizePos);
        SetTrack(LPFTrackBar, s.LowPassPos);
        SetTrack(HPFTrackBar, s.HighPassPos);

        CfocusCheckBox.Checked = s.Centralize != 0;
        LPFCheckBox.Checked    = s.LowPass != 0;
        HPFCheckBox.Checked    = s.HighPass != 0;
        OvspCheckBox.Checked   = s.Oversample != 0;

        OvspComboBox.Text  = s.OversampleMul.ToString(CultureInfo.InvariantCulture);
        BsizeComboBox.Text = s.BlockSizeMs.ToString(CultureInfo.InvariantCulture);

        KnameCheckBox.Checked = s.SearchInstFile != 0;
        VnameCheckBox.Checked = s.AutoNameOutput != 0;
        VnameEdit.Text = s.OutputSuffix;

        SaveDropDown.Mask = s.OutputKind == 1 ? SaveFiles.Aligned : (s.SaveMask & 15) == 0 ? SaveFiles.Default : s.SaveMask & 15;
        OutFormatCombo.SelectedIndex = Math.Clamp(s.OutputFormat, 0, OutFormatCombo.Items.Count - 1);
        OutDepthCombo.SelectedIndex = Math.Clamp(s.OutputDepth, 0, OutDepthCombo.Items.Count - 1);
        OutBitrateCombo.SelectedIndex = NearestBitrate(s.OutputBitrate);
        UpdateOutputControls();

        GpuCheckBox.Checked = s.UseGpu != 0;
        GpuExactButton.Checked = s.GpuMode == 0;
        GpuFastButton.Checked = s.GpuMode != 0;
        GpuExactButton.Enabled = GpuFastButton.Enabled = GpuCheckBox.Checked;

        int modelIndex = Array.IndexOf(Messages.ModelOrder, s.WaveModel);
        ModelRadioGroup.ItemIndex = modelIndex >= 0 ? modelIndex : 1;
        AlignRadioGroup.ItemIndex = Math.Clamp(s.WaveAlign, 0, Messages.AlignNames.Length - 1);
        SpansEdit.Text = s.FitSpans;
        MatchBandwidthCheckBox.Checked = s.MatchBandwidth != 0;
        MatchLowEndCheckBox.Checked = s.MatchLowEnd != 0;
        FreqModelRadioGroup.ItemIndex = Math.Clamp(s.FreqModel, 0, Messages.FreqModelNames.Length - 1);
        SubsonicCheckBox.Checked = s.RemoveSubsonic != 0;
        FftCombo.SelectedIndex = Math.Max(0, Array.IndexOf(Messages.FftValues, s.WaveFft));
        UpdateEnabled();
        SyncMethodButtons();

        // 値が変わらず event が出ない場合もあるので label は明示的に更新する。Reset が既定値のままでも正しく表示するため。
        KvolText.Text   = TrackLabels.ExtractLevel(KvolTrackBar.Value);
        KlvlText.Text   = TrackLabels.InstLevel(KlvlTrackBar.Value);
        CfocusText.Text = TrackLabels.Centralize(CfocusTrackBar.Value);
        LPFLabel.Text   = TrackLabels.LowPass(LPFTrackBar.Value);
        HPFLabel.Text   = TrackLabels.HighPass(HPFTrackBar.Value);
    }

    private CoreSettings Collect(CoreSettings s)
    {
        s.IntroMode = Math.Max(0, IntroRadioGroup.ItemIndex);
        s.SoundQty  = Math.Max(0, SoundQtyGroup.ItemIndex);
        s.LevelAdpt = Math.Max(0, LevelRadioGroup.ItemIndex);
        s.ProcMode  = Math.Max(0, DataRadioGroup.ItemIndex);
        s.KrkPhase  = Math.Max(0, PhaseRadioGroup.ItemIndex);

        s.AdptMode  = AdptManualButton.Checked ? 1 : 0;
        s.AdptRange = (int)AdptUpDown.Value;

        s.ExtractLevel  = KvolTrackBar.Value;
        s.InstLevel     = KlvlTrackBar.Value;
        s.CentralizePos = CfocusTrackBar.Value;
        s.LowPassPos    = LPFTrackBar.Value;
        s.HighPassPos   = HPFTrackBar.Value;

        s.Centralize = CfocusCheckBox.Checked ? 1 : 0;
        s.LowPass    = LPFCheckBox.Checked ? 1 : 0;
        s.HighPass   = HPFCheckBox.Checked ? 1 : 0;
        s.Oversample = OvspCheckBox.Checked ? 1 : 0;

        // combo box は編集可能。正の数として読めない text なら以前の値を維持する。
        if (int.TryParse(OvspComboBox.Text, NumberStyles.None, CultureInfo.InvariantCulture, out int ov) && ov > 0)
            s.OversampleMul = ov;
        if (int.TryParse(BsizeComboBox.Text, NumberStyles.None, CultureInfo.InvariantCulture, out int bs) && bs > 0)
            s.BlockSizeMs = bs;

        s.SearchInstFile = KnameCheckBox.Checked ? 1 : 0;
        s.AutoNameOutput = VnameCheckBox.Checked ? 1 : 0;
        // 元実装は file name 禁止文字を拒否するが、ここでは入力時に落とす。
        s.OutputSuffix = FileNaming.SanitizeSuffix(VnameEdit.Text);

        s.MergeMode = Math.Max(0, MethodRadioGroup.ItemIndex);
        s.OutputKind = 0;
        s.SaveMask = SaveDropDown.Mask == 0 ? SaveFiles.Default : SaveDropDown.Mask;
        s.OutputFormat = Math.Max(0, OutFormatCombo.SelectedIndex);
        s.OutputDepth = Math.Max(0, OutDepthCombo.SelectedIndex);
        s.OutputBitrate = Bitrates[Math.Max(0, OutBitrateCombo.SelectedIndex)];

        s.UseGpu = GpuCheckBox.Checked ? 1 : 0;
        s.GpuMode = GpuFastButton.Checked ? 1 : 0;

        s.WaveModel = ModelValue;
        s.WaveAlign = Math.Max(0, AlignRadioGroup.ItemIndex);
        s.FitSpans = SpansEdit.Text.Trim();
        s.MatchBandwidth = MatchBandwidthCheckBox.Checked ? 1 : 0;
        s.MatchLowEnd = MatchLowEndCheckBox.Checked ? 1 : 0;
        s.FreqModel = Math.Max(0, FreqModelRadioGroup.ItemIndex);
        s.RemoveSubsonic = SubsonicCheckBox.Checked ? 1 : 0;
        s.WaveFft = Messages.FftValues[Math.Max(0, FftCombo.SelectedIndex)];
        return s;
    }

    private static readonly int[] Bitrates = { 96, 128, 160, 192, 256, 320 };

    private static int NearestBitrate(int kbps) =>
        Array.IndexOf(Bitrates, Bitrates.OrderBy(b => Math.Abs(b - kbps)).First());

    // lossless 形式だけ bit depth、lossy 形式だけ bitrate を選べるようにする。32-bit float を保存できるのは WAV だけ。
    private void UpdateOutputControls()
    {
        var f = (OutputFormat)Math.Max(0, OutFormatCombo.SelectedIndex);
        bool lossless = f is OutputFormat.Wav or OutputFormat.Aiff or OutputFormat.Flac or OutputFormat.Alac;
        OutDepthCombo.Enabled = lossless;
        OutBitrateCombo.Enabled = !lossless;
        if (lossless && f != OutputFormat.Wav && OutDepthCombo.SelectedIndex == (int)OutputDepth.Float32)
            OutDepthCombo.SelectedIndex = (int)OutputDepth.Int24;
    }

    // 今の選択で実際に使われる設定だけを操作できるようにする (v4。v3 は全部いつでも触れた)。
    // どの設定がどの経路で読まれるかは core の engine.cpp / extract.cpp に合わせている。
    //   By Frequency だけ: Accuracy Priority、Extractable Level
    //   By Waveform (v3 の減算) だけ: Instrumental Level Adjustment (Manual のとき level の値)
    //   By Waveform: Oversampling、Waveform Model。代替モデルでは位置合わせと区間も
    //   v3 の解析 (Intro / Time Shift / Phase / Processing Mode / Block Length): 相互相関で揃えるときは使わない
    //   揃えた組の出力: 抽出しないので、方式・後処理・level は使わず、位置合わせだけを使う
    private void UpdateEnabled()
    {
        int method = MethodRadioGroup.ItemIndex;
        SaveDropDown.SetAvailable(3, SaveFiles.MembersAvailable(method, ModelValue));
        bool pair = SaveDropDown.EffectiveMask == SaveFiles.Aligned;
        bool noExtract = pair;
        bool wave = method == 1;
        bool model = ModelValue > 0;
        bool gcc = AlignRadioGroup.ItemIndex >= 1;
        bool algorithm = ModelValue is SurfaceValue or LowRankValue;
        bool freq = !noExtract && !wave;
        int freqAlgo = Math.Max(0, FreqModelRadioGroup.ItemIndex);
        bool v3Freq = freq && freqAlgo == 0;
        bool modelAligns = pair || (!noExtract && wave && model) || (freq && freqAlgo > 0);
        bool aligns = modelAligns || freq;
        bool v3Analysis = !(modelAligns && gcc);

        FreqModelRadioGroup.Enabled = freq;
        SoundQtyGroup.Enabled = KvolBox.Enabled = v3Freq;
        foreach (Control c in new Control[] { KvolTrackBar, KvolText, KvolWeak, KvolStrong }) c.Enabled = v3Freq;

        bool v3Sub = !noExtract && wave && !model;
        LevelRadioGroup.Enabled = v3Sub;
        // 代替モデルが level を決めているときは、灰色の理由 (どのモデルか) を見出しに出す。
        LevelRadioGroup.Text = Messages.LevelGroup;
        KlvlTrackBar.Enabled = KlvlText.Enabled = v3Sub && LevelRadioGroup.ItemIndex == 2;

        IntroRadioGroup.Enabled = AdptLvlGroupBox.Enabled = PhaseRadioGroup.Enabled =
            DataRadioGroup.Enabled = BsizeBox.Enabled = v3Analysis;
        AdptUpDown.Enabled = v3Analysis && AdptManualButton.Checked;

        // oversampling は By Waveform の block 探索で使う。揃えた組も同じ探索で揃える。
        OvspBox.Enabled = v3Analysis && (pair || wave);
        OvspComboBox.Enabled = OvspBox.Enabled && OvspCheckBox.Checked;

        FilterBox.Enabled = !noExtract;
        CfocusTrackBar.Enabled = CfocusText.Enabled = CfWeak.Enabled = CfStrong.Enabled = !noExtract && CfocusCheckBox.Checked;
        LPFTrackBar.Enabled = LPFLabel.Enabled = !noExtract && LPFCheckBox.Checked;
        HPFTrackBar.Enabled = HPFLabel.Enabled = !noExtract && HPFCheckBox.Checked;

        ModelRadioGroup.Enabled = !noExtract && wave;
        AlignRadioGroup.Enabled = aligns;
        SpansBox.Enabled = !noExtract && wave && model && !algorithm;
        FftBox.Enabled = !noExtract && wave && model && ModelValue != LowRankValue;
        MatchBandwidthCheckBox.Enabled = !noExtract;
        MatchLowEndCheckBox.Enabled = SubsonicCheckBox.Enabled = !noExtract;

        // 揃えた組の自動命名は _aligned を使うので、声の suffix は使わない。
        VnameEdit.Enabled = AppendLabel.Enabled = VnameCheckBox.Checked && !noExtract;
    }

    // 灰色になりうる設定と分かりにくい設定の見出しを link にし、mouse を乗せると説明を出す。今使えないならその理由も付ける。
    private void AttachTips()
    {
        bool Pair() => SaveDropDown.EffectiveMask == SaveFiles.Aligned;
        string NoX() => Messages.Tips.BecausePair;
        bool Wave() => MethodRadioGroup.ItemIndex == 1;
        bool Model() => ModelValue > 0;
        int FreqAlgo() => Math.Max(0, FreqModelRadioGroup.ItemIndex);
        bool Gcc() => (Pair() || (Wave() && Model()) || (!Wave() && FreqAlgo() > 0)) &&
                      AlignRadioGroup.ItemIndex >= 1;
        bool SideAlgorithm() => ModelValue is SurfaceValue or LowRankValue;
        string ModelName() => Messages.ModelShortNames[Math.Clamp(ModelValue, 0, Messages.ModelShortNames.Length - 1)];

        void Tip(Control target, Func<string> text, Func<string?> why) =>
            _links.Add(target, () => why() is { } reason ? text() + Messages.Tips.NotNow + reason : text());

        string? V3Analysis() => Gcc() ? Messages.Tips.BecauseGcc : null;
        string? FreqOnly() => Pair() ? NoX() : Wave() ? Messages.Tips.BecauseFreqOnly : FreqAlgo() > 0 ? Messages.Tips.BecauseV3FreqOnly : null;

        Tip(IntroRadioGroup, () => Messages.Tips.Intro, V3Analysis);
        Tip(AdptLvlGroupBox, () => Messages.Tips.TimeShift, V3Analysis);
        foreach (var use in UseMethodButtons) Tip(use, () => Messages.Tips.Method, () => null);
        Tip(SoundQtyGroup, () => Messages.Tips.Accuracy, FreqOnly);
        Tip(KvolBox, () => Messages.Tips.ExtractLevel, FreqOnly);
        Tip(FreqModelRadioGroup, () => Messages.Tips.FreqModel, () => Pair() ? NoX() : Wave() ? Messages.Tips.BecauseFreqOnly : null);
        Tip(LevelRadioGroup, () => Messages.Tips.Level, () =>
            Pair() ? NoX() : !Wave() ? Messages.Tips.BecauseWaveOnly : Model() ? Messages.Tips.BecauseV3Only : null);

        Tip(DataRadioGroup, () => Messages.Tips.ProcMode, V3Analysis);
        Tip(PhaseRadioGroup, () => Messages.Tips.Phase, V3Analysis);
        Tip(FilterBox, () => Messages.Tips.Filtering, () => Pair() ? NoX() : null);
        Tip(OvspBox, () => Messages.Tips.Oversampling, () => V3Analysis() ?? (!Pair() && !Wave() ? Messages.Tips.BecauseWaveOnly : null));
        Tip(BsizeBox, () => Messages.Tips.Block, V3Analysis);

        Tip(VnameCheckBox.Parent!, () => Messages.Tips.FileName, () => null);
        Tip(SaveDropDown.Parent!, () => Messages.Tips.Output, () => null);
        Tip(GpuCheckBox.Parent!, () => Messages.Tips.Gpu, () => GpuCheckBox.Checked ? null : Messages.Tips.BecauseGpuOff);

        Tip(ModelRadioGroup, () => Messages.Tips.Model, () => Pair() ? NoX() : !Wave() ? Messages.Tips.BecauseWaveOnly : null);
        Tip(AlignRadioGroup, () => Messages.Tips.Align, () => Pair() || !Wave() || Model() ? null : Messages.Tips.BecauseV3Align);
        Tip(SpansBox, () => Messages.Tips.Spans, () =>
            Pair() ? NoX() : !Wave() ? Messages.Tips.BecauseWaveOnly : !Model() ? Messages.Tips.BecauseV3Model :
            SideAlgorithm() ? Messages.Tips.BecauseNoSpans(ModelName()) : null);
        Tip(FftBox, () => Messages.Tips.Fft, () =>
            Pair() ? NoX() : !Wave() ? Messages.Tips.BecauseWaveOnly : !Model() ? Messages.Tips.BecauseV3Model :
            ModelValue == LowRankValue ? Messages.Tips.BecauseFixedFft(ModelName()) : null);
        Tip(MatchLowEndCheckBox, () => Messages.Tips.MatchLowEnd, () => Pair() ? NoX() : null);
        Tip(SubsonicCheckBox, () => Messages.Tips.RemoveSubsonic, () => Pair() ? NoX() : null);
        Tip(MatchBandwidthCheckBox, () => Messages.Tips.MatchBandwidth, () => Pair() ? NoX() : null);

        var waveButtons = ModelRadioGroup.Buttons;
        for (int i = 0; i < waveButtons.Count && i < Messages.ModelOrder.Length; ++i)
        {
            int model = Messages.ModelOrder[i], k = i;
            _links.AddPlain(waveButtons[i], () => Messages.Tips.Stats(Messages.ModelNames[k], Messages.ModelStats[model]));
        }
        var freqButtons = FreqModelRadioGroup.Buttons;
        for (int i = 0; i < freqButtons.Count && i < Messages.FreqStats.Length; ++i)
        {
            int k = i;
            _links.AddPlain(freqButtons[i], () => Messages.Tips.Stats(Messages.FreqModelNames[k], Messages.FreqStats[k]));
        }
    }

    private void SyncMethodButtons()
    {
        for (int k = 0; k < UseMethodButtons.Length; ++k)
            UseMethodButtons[k].Checked = MethodRadioGroup.ItemIndex == k;
    }

    // Frequency と Waveform で共通の部品は 1 つだけ作り、開いている方の page へ移す。
    private void PlaceShared()
    {
        bool wave = PageControl.SelectedTab == WaveTab || (PageControl.SelectedTab != FreqTab && MethodRadioGroup.ItemIndex == 1);
        var main = wave ? WaveMainPage : FreqMainPage;
        var settings = wave ? WaveSettingsPage : FreqSettingsPage;
        if (SharedMain.Parent != main) SharedMain.Parent = main;
        if (SharedSettings.Parent != settings) SharedSettings.Parent = settings;
    }

    private const int SurfaceValue = 8;
    private const int LowRankValue = 11;

    private int ModelValue
    {
        get
        {
            int i = ModelRadioGroup.ItemIndex;
            return i >= 0 && i < Messages.ModelOrder.Length ? Messages.ModelOrder[i] : 0;
        }
    }

    private static void SetTrack(TrackBar t, int value) =>
        t.Value = Math.Clamp(value, t.Minimum, t.Maximum);
}
