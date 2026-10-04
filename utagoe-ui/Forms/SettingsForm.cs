// 元実装の settings dialog の挙動を再現する。
// 表示時に設定を control へコピーし、Reset で factory default、trackbar change で表示更新、OK で設定へ戻す。
// v4: modal ではなくなった。開いたまま main window を操作できる。別 window (OK / Cancel で閉じる) か、
// main window を最大化したときはその中に組み込まれる。組み込み中は button を持たず、変えた設定はすぐに保存される
// (Reset は main window の側に出る)。

using System.Globalization;
using Utagoe.App;
using Utagoe.Native;
using Utagoe.Vcl;

namespace Utagoe.Forms;

internal sealed partial class SettingsForm : Form
{
    private readonly Func<CoreSettings> _current;
    private readonly Func<string> _originalPath;
    private SpansForm? _spansForm;

    private void OpenSpansPicker()
    {
        if (_spansForm is { IsDisposed: false })
        {
            _spansForm.Activate();
            return;
        }
        _spansForm = new SpansForm(_originalPath(), SpansEdit.Text, text => SpansEdit.Text = text);
        _spansForm.FormClosed += (_, _) => _spansForm = null;
        _spansForm.Show(this);
    }
    private readonly HelpLinks _links = new();
    private bool _docked;

    /// OK / Apply で確定した設定。main window が保存して使う。
    public event Action<CoreSettings>? Applied;

    /// current は今の設定を返す。OK のときはその上に画面の値を書く (画面に無い設定、例えば通知の表示可否は保つ)。
    public SettingsForm(Func<CoreSettings> current, Func<string> originalPath)
    {
        _current = current;
        _originalPath = originalPath;
        InitializeComponent();
        Icon = VclGlyph.AppIcon;
        VclScaling.Apply(this);

        KvolTrackBar.ValueChanged   += (_, _) => KvolText.Text   = TrackLabels.ExtractLevel(KvolTrackBar.Value);
        KlvlTrackBar.ValueChanged   += (_, _) => KlvlText.Text   = TrackLabels.InstLevel(KlvlTrackBar.Value);
        CfocusTrackBar.ValueChanged += (_, _) => CfocusText.Text = TrackLabels.Centralize(CfocusTrackBar.Value);
        LPFTrackBar.ValueChanged    += (_, _) => LPFLabel.Text   = TrackLabels.LowPass(LPFTrackBar.Value);
        HPFTrackBar.ValueChanged    += (_, _) => HPFLabel.Text   = TrackLabels.HighPass(HPFTrackBar.Value);

        SpansPickButton.Click += (_, _) => OpenSpansPicker();
        ResetButton.Click += (_, _) => ResetToDefaults();   // 別 window では確かめた後も、OK で確定するまで保存しない
        OkBitBtn.DialogResult = CanBitBtn.DialogResult = DialogResult.None;
        OkBitBtn.Click += (_, _) => Apply();
        CanBitBtn.Click += (_, _) =>
        {
            if (_docked) Populate(_current());
            else Close();
        };
        OutFormatCombo.SelectedIndexChanged += (_, _) => UpdateOutputControls();
        GpuCheckBox.CheckedChanged += (_, _) => GpuExactButton.Enabled = GpuFastButton.Enabled = GpuCheckBox.Checked;
        // 親の選択が変わったら、使われなくなる子の設定を灰色にする。
        foreach (var g in new[] { MethodRadioGroup, LevelRadioGroup, ModelRadioGroup, AlignRadioGroup, CentreRadioGroup, UpmixRadioGroup, FreqModelRadioGroup })
            g.ItemIndexChanged += (_, _) => UpdateEnabled();
        foreach (var c in new[] { OvspCheckBox, CfocusCheckBox, LPFCheckBox, HPFCheckBox, VnameCheckBox })
            c.CheckedChanged += (_, _) => UpdateEnabled();
        AdptManualButton.CheckedChanged += (_, _) => UpdateEnabled();
        SaveDropDown.CheckedChanged += (_, _) => UpdateEnabled();
        UpmixVocalCombo.SelectedIndexChanged += (_, _) => UpdateEnabled();
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
        Populate(current());
        if (UseMethodButtons[Math.Max(0, MethodRadioGroup.ItemIndex)].Parent?.Parent is TabPage active) PageControl.SelectedTab = active;
        PlaceShared();
        _liveApply.Tick += (_, _) => ApplyNow();
        HookLiveApply(this);
        AttachTips();
        Theme.Paint(this);
        void Retranslate() { UpdateEnabled(); ShowGpuStatus(); }
        L.Changed += Retranslate;
        FormClosed += (_, _) => { _links.Dispose(); _liveApply.Dispose(); L.Changed -= Retranslate; };
        ShowGpuStatus();
    }

    private Size _fullClient;
    private bool _populating;
    private readonly System.Windows.Forms.Timer _liveApply = new() { Interval = 250 };

    /// main window に組み込むときは OK / Cancel / Reset を隠し、その分だけ高さを詰める。変えた設定はすぐに保存する。
    [System.ComponentModel.DesignerSerializationVisibility(System.ComponentModel.DesignerSerializationVisibility.Hidden)]
    public bool Docked
    {
        get => _docked;
        set
        {
            if (_docked == value) return;
            _docked = value;
            OkBitBtn.Visible = CanBitBtn.Visible = ResetButton.Visible = !value;
            if (PageControl is ThemedTabControl tabs) tabs.MinRows = value ? 1 : 2;
            AcceptButton = value ? null : OkBitBtn;
            CancelButton = value ? null : CanBitBtn;
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
        var answer = MessageForm.Show(TopLevelControl ?? this, _docked ? Messages.ResetConfirm : Messages.ResetConfirmOk,
                                      Messages.Title, MessageBoxButtons.OKCancel, MessageBoxIcon.Question,
                                      heading: Messages.ResetTitle, confirm: Messages.Reset);
        if (answer != DialogResult.OK) return;
        Populate(AppSettings.Defaults());
        if (_docked) ApplyNow();
    }

    // 組み込み中は、どの設定を変えてもすぐに (少し待ってまとめて) 保存する。
    private void HookLiveApply(Control c)
    {
        void Changed(object? sender, EventArgs e)
        {
            if (!_docked || _populating) return;
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

    // 区間の書式が読めないまま確定しないよう、先に確かめる。
    private void Apply()
    {
        if (!Core.CheckSpans(SpansEdit.Text.Trim(), out string error))
        {
            Utagoe.Forms.MessageForm.Show(this, Messages.BadSpans(error), Messages.Title, MessageBoxButtons.OK, MessageBoxIcon.Warning);
            PageControl.SelectedTab = WaveTab;
            WavePages.SelectedTab = WaveMainPage;
            SpansEdit.Focus();
            return;
        }
        Applied?.Invoke(Collect(_current()));
        if (!_docked) Close();
    }

    // GPU の初期化は時間がかかることがあるので、dialog を出してから裏で調べて表示する。
    private async void ShowGpuStatus()
    {
        var gpu = await Task.Run(() => Core.Gpu);
        if (IsDisposed) return;
        GpuStatusLabel.Text = gpu.Available ? Messages.GpuUsing(gpu.Adapter) : Messages.GpuUnavailable(gpu.Reason);
    }

    private void Populate(CoreSettings s)
    {
        _populating = true;
        try { PopulateCore(s); }
        finally { _populating = false; }
    }

    private void PopulateCore(CoreSettings s)
    {
        IntroRadioGroup.ItemIndex = s.IntroMode;
        MethodRadioGroup.ItemIndex = s.OutputKind is 2 or 3 or 4 ? s.OutputKind : s.MergeMode == 1 ? 1 : 0;
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
        RepeatGuideCombo.SelectedIndex = Math.Clamp(s.RepeatGuide, 0, RepeatGuideCombo.Items.Count - 1);
        RepeatSearchCombo.SelectedIndex = Math.Clamp(s.RepeatBreadth, 0, RepeatSearchCombo.Items.Count - 1);
        CentreRadioGroup.ItemIndex = Math.Clamp(s.CenterMethod, 0, Messages.CentreNames.Length - 1);
        UpmixRadioGroup.ItemIndex = Math.Clamp(s.UpmixMethod, 0, Messages.UpmixNames.Length - 1);
        UpmixLayoutCombo.SelectedIndex = s.UpmixSevenOne != 0 ? 1 : 0;
        UpmixLfeCheckBox.Checked = s.UpmixLfe != 0;
        UpmixVocalCombo.SelectedIndex = s.MergeMode == 1 ? 1 : 0;
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

        int method = Math.Max(0, MethodRadioGroup.ItemIndex);
        if (method <= 1) s.MergeMode = method;
        else if (method == 4 && UpmixRadioGroup.ItemIndex is 6 or 9) s.MergeMode = Math.Max(0, UpmixVocalCombo.SelectedIndex);
        s.OutputKind = method >= 2 ? method : 0;
        s.SaveMask = SaveDropDown.Mask == 0 ? SaveFiles.Default : SaveDropDown.Mask;
        s.CenterMethod = Math.Max(0, CentreRadioGroup.ItemIndex);
        s.UpmixMethod = Math.Max(0, UpmixRadioGroup.ItemIndex);
        s.UpmixSevenOne = UpmixLayoutCombo.SelectedIndex == 1 ? 1 : 0;
        s.UpmixLfe = UpmixLfeCheckBox.Checked ? 1 : 0;
        s.RepeatGuide = Math.Max(0, RepeatGuideCombo.SelectedIndex);
        s.RepeatBreadth = Math.Max(0, RepeatSearchCombo.SelectedIndex);
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
        bool split = method == 2;
        bool repeats = method == 3;
        bool upmix = method == 4;
        int upmixMethod = UpmixRadioGroup.ItemIndex;
        bool referenceScene = upmix && upmixMethod is 6 or 9;
        bool originalOnly = split || repeats || (upmix && !referenceScene);
        bool vocalRun = !originalOnly && !upmix;
        SaveDropDown.SetAvailable(1, vocalRun);
        SaveDropDown.SetAvailable(2, vocalRun);
        SaveDropDown.SetAvailable(3, vocalRun && SaveFiles.MembersAvailable(method, ModelValue));
        bool pair = vocalRun && SaveDropDown.EffectiveMask == SaveFiles.Aligned;
        bool noExtract = pair || originalOnly;
        bool wave = method == 1 || (referenceScene && UpmixVocalCombo.SelectedIndex == 1);
        bool model = ModelValue > 0;
        bool autoPick = !noExtract && wave && ModelValue == AutoValue;
        bool gcc = !autoPick && AlignRadioGroup.ItemIndex >= 1;
        bool algorithm = ModelValue >= FirstSideAlgorithm && ModelValue <= LastSideAlgorithm;
        bool freq = !noExtract && !wave;
        int freqAlgo = Math.Max(0, FreqModelRadioGroup.ItemIndex);
        bool v3Freq = freq && freqAlgo == 0;
        bool modelAligns = pair || (!noExtract && wave && model && !autoPick) || (freq && freqAlgo > 0);
        bool aligns = modelAligns || freq;
        bool v3Analysis = !originalOnly && !(modelAligns && gcc);

        NeedsLabels[0].Text = NeedsLabels[1].Text = Messages.NeedsBoth;
        NeedsLabels[2].Text = NeedsLabels[3].Text = Messages.NeedsOriginal;
        NeedsLabels[4].Text = upmixMethod is 4 or >= 6 ? Messages.NeedsBoth : Messages.NeedsOriginal;
        FreqModelRadioGroup.Enabled = freq;
        SoundQtyGroup.Enabled = KvolBox.Enabled = v3Freq;
        foreach (Control c in new Control[] { KvolTrackBar, KvolText, KvolWeak, KvolStrong }) c.Enabled = v3Freq;

        bool v3Sub = !noExtract && wave && !model;
        LevelRadioGroup.Enabled = v3Sub;
        // 代替モデルが level を決めているときは、灰色の理由 (どのモデルか) を見出しに出す。
        LevelRadioGroup.Text = !noExtract && wave && model ? Messages.LevelSetByModel(ModelValue) : Messages.LevelGroup;
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
        MatchBandwidthCheckBox.Enabled = !noExtract;
        MatchLowEndCheckBox.Enabled = SubsonicCheckBox.Enabled = !noExtract;
        CentreRadioGroup.Enabled = split;
        RepeatsBox.Enabled = repeats;
        UpmixRadioGroup.Enabled = UpmixLfeCheckBox.Enabled = upmix;
        UpmixLayoutCombo.Enabled = UpmixLayoutLabel.Enabled = upmix && upmixMethod is 5 or 6;
        UpmixVocalCombo.Enabled = UpmixVocalLabel.Enabled = referenceScene;

        // 揃えた組の自動命名は _aligned を使うので、声の suffix は使わない。
        VnameEdit.Enabled = AppendLabel.Enabled = VnameCheckBox.Checked && !noExtract;
    }

    // 灰色になりうる設定と分かりにくい設定の見出しを link にし、mouse を乗せると説明を出す。今使えないならその理由も付ける。
    private void AttachTips()
    {
        bool Split() => MethodRadioGroup.ItemIndex == 2;
        bool Repeats() => MethodRadioGroup.ItemIndex == 3;
        bool Upmix() => MethodRadioGroup.ItemIndex == 4;
        bool ReferenceScene() => Upmix() && UpmixRadioGroup.ItemIndex is 6 or 9;
        bool OriginalOnly() => Split() || Repeats() || (Upmix() && !ReferenceScene());
        bool Pair() => OriginalOnly() || (!Upmix() && SaveDropDown.EffectiveMask == SaveFiles.Aligned);
        string NoX() => Split() ? Messages.Tips.BecauseSplit : Repeats() ? Messages.Tips.BecauseRepeats :
                        Upmix() ? Messages.Tips.BecauseUpmix : Messages.Tips.BecausePair;
        bool Wave() => MethodRadioGroup.ItemIndex == 1 || (ReferenceScene() && UpmixVocalCombo.SelectedIndex == 1);
        bool Model() => ModelValue > 0;
        bool Auto() => !Pair() && Wave() && ModelValue == AutoValue;
        int FreqAlgo() => Math.Max(0, FreqModelRadioGroup.ItemIndex);
        bool Gcc() => ((Pair() && !OriginalOnly()) || (!Pair() && Wave() && Model() && !Auto()) || (!Pair() && !Wave() && FreqAlgo() > 0)) &&
                      AlignRadioGroup.ItemIndex >= 1;
        bool SideAlgorithm() => ModelValue >= FirstSideAlgorithm && ModelValue <= LastSideAlgorithm;
        string ModelName() => Messages.ModelShortNames[Math.Clamp(ModelValue, 0, Messages.ModelShortNames.Length - 1)];

        void Tip(Control target, Func<string> text, Func<string?> why) =>
            _links.Add(target, () => why() is { } reason ? text() + Messages.Tips.NotNow + reason : text());

        string? V3Analysis() => OriginalOnly() ? NoX() : Gcc() ? Messages.Tips.BecauseGcc : null;
        string? FreqOnly() => Pair() ? NoX() : Wave() ? Messages.Tips.BecauseFreqOnly : FreqAlgo() > 0 ? Messages.Tips.BecauseV3FreqOnly : null;

        Tip(IntroRadioGroup, () => Messages.Tips.Intro, V3Analysis);
        Tip(AdptLvlGroupBox, () => Messages.Tips.TimeShift, V3Analysis);
        foreach (var use in UseMethodButtons) Tip(use, () => Messages.Tips.Method, () => null);
        Tip(SoundQtyGroup, () => Messages.Tips.Accuracy, FreqOnly);
        Tip(KvolBox, () => Messages.Tips.ExtractLevel, FreqOnly);
        Tip(FreqModelRadioGroup, () => Messages.Tips.FreqModel, () => Pair() ? NoX() : Wave() ? Messages.Tips.BecauseFreqOnly : null);
        Tip(LevelRadioGroup, () => Messages.Tips.Level, () =>
            Pair() ? NoX() : !Wave() ? Messages.Tips.BecauseWaveOnly : Model() ? Messages.Tips.BecauseModel(ModelName()) : null);

        Tip(DataRadioGroup, () => Messages.Tips.ProcMode, V3Analysis);
        Tip(PhaseRadioGroup, () => Messages.Tips.Phase, V3Analysis);
        Tip(FilterBox, () => Messages.Tips.Filtering, () => Pair() ? NoX() : null);
        Tip(OvspBox, () => Messages.Tips.Oversampling, () => V3Analysis() ?? (!Pair() && !Wave() ? Messages.Tips.BecauseWaveOnly : null));
        Tip(BsizeBox, () => Messages.Tips.Block, V3Analysis);

        Tip(VnameCheckBox.Parent!, () => Messages.Tips.FileName, () => null);
        Tip(SaveDropDown.Parent!, () => Messages.Tips.Output, () => null);
        Tip(GpuCheckBox.Parent!, () => Messages.Tips.Gpu, () => GpuCheckBox.Checked ? null : Messages.Tips.BecauseGpuOff);

        Tip(ModelRadioGroup, () => Messages.Tips.Model, () => Pair() ? NoX() : !Wave() ? Messages.Tips.BecauseWaveOnly : null);
        Tip(AlignRadioGroup, () => Messages.Tips.Align, () => OriginalOnly() ? NoX() : Auto() ? Messages.Tips.BecauseAuto : Pair() || !Wave() || Model() ? null : Messages.Tips.BecauseV3Align);
        Tip(SpansBox, () => Messages.Tips.Spans, () =>
            Pair() ? NoX() : !Wave() ? Messages.Tips.BecauseWaveOnly : !Model() ? Messages.Tips.BecauseV3Model :
            SideAlgorithm() ? Messages.Tips.BecauseNoSpans(ModelName()) : null);
        Tip(CentreRadioGroup, () => Messages.Tips.Centre, () => Split() ? null : Messages.Tips.BecauseNotSplit);
        Tip(RepeatsBox, () => Messages.Tips.Repeats, () => Repeats() ? null : Messages.Tips.BecauseNotRepeats);
        Tip(UpmixRadioGroup, () => Messages.Tips.Upmix, () => Upmix() ? null : Messages.Tips.BecauseNotUpmix);
        Tip(UpmixLayoutLabel, () => Messages.Tips.Upmix, () =>
            !Upmix() ? Messages.Tips.BecauseNotUpmix : UpmixRadioGroup.ItemIndex is not (5 or 6) ? Messages.Tips.BecauseFiveOne : null);
        Tip(UpmixVocalLabel, () => Messages.Tips.Upmix, () =>
            !Upmix() ? Messages.Tips.BecauseNotUpmix : ReferenceScene() ? null : Messages.Tips.BecauseNotReferenceScene);
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

    private const int FirstSideAlgorithm = 7;
    private const int LastSideAlgorithm = 11;
    private const int AutoValue = 13;

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
