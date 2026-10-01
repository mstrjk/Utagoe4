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
    private readonly HelpLinks _links = new();
    private bool _docked;

    /// OK / Apply で確定した設定。main window が保存して使う。
    public event Action<CoreSettings>? Applied;

    /// current は今の設定を返す。OK のときはその上に画面の値を書く (画面に無い設定、例えば通知の表示可否は保つ)。
    public SettingsForm(Func<CoreSettings> current)
    {
        _current = current;
        InitializeComponent();
        Icon = VclGlyph.AppIcon;
        VclScaling.Apply(this);

        KvolTrackBar.ValueChanged   += (_, _) => KvolText.Text   = TrackLabels.ExtractLevel(KvolTrackBar.Value);
        KlvlTrackBar.ValueChanged   += (_, _) => KlvlText.Text   = TrackLabels.InstLevel(KlvlTrackBar.Value);
        CfocusTrackBar.ValueChanged += (_, _) => CfocusText.Text = TrackLabels.Centralize(CfocusTrackBar.Value);
        LPFTrackBar.ValueChanged    += (_, _) => LPFLabel.Text   = TrackLabels.LowPass(LPFTrackBar.Value);
        HPFTrackBar.ValueChanged    += (_, _) => HPFLabel.Text   = TrackLabels.HighPass(HPFTrackBar.Value);

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
        foreach (var g in new[] { MergeRadioGroup, LevelRadioGroup, ModelRadioGroup, AlignRadioGroup, CentreRadioGroup })
            g.ItemIndexChanged += (_, _) => UpdateEnabled();
        foreach (var c in new[] { OvspCheckBox, CfocusCheckBox, LPFCheckBox, HPFCheckBox, VnameCheckBox })
            c.CheckedChanged += (_, _) => UpdateEnabled();
        AdptManualButton.CheckedChanged += (_, _) => UpdateEnabled();
        OutKindCombo.SelectedIndexChanged += (_, _) => UpdateEnabled();
        Populate(current());
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

    private ResetPrompt? _resetPrompt;

    /// 既定値に戻す。theme の色の確認を window の中央に出してから戻す (Windows の message box は使わない)。
    /// 組み込み中は main window の Reset から呼ばれ、すぐに保存される。別 window では OK を押すまで保存しない。
    public void ResetToDefaults()
    {
        if (_resetPrompt is { IsDisposed: false }) { _resetPrompt.Focus(); return; }
        // main window 全体の中央に重ねる (組み込み中の Settings は main window の一部なので、その上に出す)。
        Control host = TopLevelControl ?? this;
        var prompt = new ResetPrompt(Font, _docked ? Messages.ResetConfirm : Messages.ResetConfirmOk);
        _resetPrompt = prompt;
        prompt.Answered += yes =>
        {
            host.Controls.Remove(prompt);
            prompt.Dispose();
            _resetPrompt = null;
            if (!yes) return;
            Populate(AppSettings.Defaults());
            if (_docked) ApplyNow();
        };
        Size size = prompt.PreferredBoxSize(Math.Min(host.ClientSize.Width - 40, Font.Height * 28));
        prompt.Bounds = new Rectangle((host.ClientSize.Width - size.Width) / 2, (host.ClientSize.Height - size.Height) / 2, size.Width, size.Height);
        host.Controls.Add(prompt);
        prompt.BringToFront();
        prompt.Focus();
    }

    /// Reset の確認。見出し・説明・Reset / Cancel の button を theme の色で描く小さな板。
    private sealed class ResetPrompt : Panel
    {
        private readonly BitBtn _yes, _no;
        private readonly Font _bold;
        public event Action<bool>? Answered;

        private readonly string _text;

        public ResetPrompt(Font font, string text)
        {
            _text = text;
            Font = font;
            _bold = new Font(font, FontStyle.Bold);
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            _yes = new BitBtn { Text = Messages.Reset, TabIndex = 0 };
            _yes.SetGlyph("BBOK");
            _no = new BitBtn { Text = "Cancel", TabIndex = 1 };
            _no.SetGlyph("BBCANCEL");
            _yes.Click += (_, _) => Answered?.Invoke(true);
            _no.Click += (_, _) => Answered?.Invoke(false);
            Controls.AddRange(new Control[] { _yes, _no });
            Theme.Paint(this);
        }

        private int Pad => Font.Height;

        public Size PreferredBoxSize(int width)
        {
            int inner = width - Pad * 2;
            Size t = TextRenderer.MeasureText(Messages.ResetTitle, _bold, new Size(inner, int.MaxValue), TextFormatFlags.WordBreak);
            Size b = TextRenderer.MeasureText(_text, Font, new Size(inner, int.MaxValue), TextFormatFlags.WordBreak);
            int bh = Font.Height * 2;
            return new Size(width, Pad + t.Height + Pad / 2 + b.Height + Pad + bh + Pad);
        }

        protected override void OnLayout(LayoutEventArgs e)
        {
            base.OnLayout(e);
            if (_yes == null || _no == null) return;   // constructor の中で Font を入れた時点ではまだ button が無い
            int bh = Font.Height * 2, bw = Font.Height * 6, gap = Pad / 2;
            int left = (Width - (bw * 2 + gap)) / 2;
            _yes.SetBounds(left, Height - Pad - bh, bw, bh);
            _no.SetBounds(left + bw + gap, _yes.Top, bw, bh);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            var t = Theme.Current;
            using (var back = new SolidBrush(t.TipBack)) g.FillRectangle(back, ClientRectangle);
            using (var edge = new Pen(t.TipEdge)) g.DrawRectangle(edge, 0, 0, Width - 1, Height - 1);
            int inner = Width - Pad * 2;
            var flags = TextFormatFlags.WordBreak;
            Size ts = TextRenderer.MeasureText(g, Messages.ResetTitle, _bold, new Size(inner, int.MaxValue), flags);
            TextRenderer.DrawText(g, Messages.ResetTitle, _bold, new Rectangle(Pad, Pad, inner, ts.Height), t.TipText, flags);
            TextRenderer.DrawText(g, _text, Font, new Rectangle(Pad, Pad + ts.Height + Pad / 2, inner, Height), t.TipText, flags);
        }

        protected override bool ProcessDialogKey(Keys keyData)
        {
            if (keyData == Keys.Escape) { Answered?.Invoke(false); return true; }
            return base.ProcessDialogKey(keyData);
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing) _bold.Dispose();
            base.Dispose(disposing);
        }
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
            MessageBox.Show(this, Messages.BadSpans(error), Messages.Title, MessageBoxButtons.OK, MessageBoxIcon.Warning);
            PageControl.SelectedIndex = 3;
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
        MergeRadioGroup.ItemIndex = s.MergeMode;
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

        OutKindCombo.SelectedIndex = s.OutputKind is >= 0 and <= 2 ? s.OutputKind : 0;
        CentreRadioGroup.ItemIndex = Math.Clamp(s.CenterMethod, 0, Messages.CentreNames.Length - 1);
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
        KickDuckCheckBox.Checked = s.KickDuck != 0;
        UpdateEnabled();

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
        s.MergeMode = Math.Max(0, MergeRadioGroup.ItemIndex);
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

        s.OutputKind = Math.Max(0, OutKindCombo.SelectedIndex);
        s.CenterMethod = Math.Max(0, CentreRadioGroup.ItemIndex);
        s.OutputFormat = Math.Max(0, OutFormatCombo.SelectedIndex);
        s.OutputDepth = Math.Max(0, OutDepthCombo.SelectedIndex);
        s.OutputBitrate = Bitrates[Math.Max(0, OutBitrateCombo.SelectedIndex)];

        s.UseGpu = GpuCheckBox.Checked ? 1 : 0;
        s.GpuMode = GpuFastButton.Checked ? 1 : 0;

        s.WaveModel = ModelValue;
        s.WaveAlign = Math.Max(0, AlignRadioGroup.ItemIndex);
        s.FitSpans = SpansEdit.Text.Trim();
        s.KickDuck = KickDuckCheckBox.Checked ? 1 : 0;
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
        bool pair = OutKindCombo.SelectedIndex == 1;
        bool split = OutKindCombo.SelectedIndex == 2;
        bool noExtract = pair || split;
        bool wave = MergeRadioGroup.ItemIndex == 1;
        bool model = ModelValue > 0;
        bool autoPick = !noExtract && wave && ModelValue == AutoValue;
        bool gcc = !autoPick && AlignRadioGroup.ItemIndex >= 1;
        bool algorithm = ModelValue >= FirstSideAlgorithm && ModelValue <= LastSideAlgorithm;
        bool aligns = pair || (!noExtract && wave && model && !autoPick);
        bool v3Analysis = !split && !(aligns && gcc);

        MergeRadioGroup.Enabled = !noExtract;
        bool freq = !noExtract && !wave;
        SoundQtyGroup.Enabled = freq;
        foreach (Control c in new Control[] { KvolTrackBar, KvolText, KvolWeak, KvolStrong }) c.Enabled = freq;

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
        KickDuckCheckBox.Enabled = !noExtract && wave && model;
        CentreRadioGroup.Enabled = split;

        // 揃えた組の自動命名は _aligned を使うので、声の suffix は使わない。
        VnameEdit.Enabled = AppendLabel.Enabled = VnameCheckBox.Checked && !noExtract;
    }

    // 灰色になりうる設定と分かりにくい設定の見出しを link にし、mouse を乗せると説明を出す。今使えないならその理由も付ける。
    private void AttachTips()
    {
        bool Pair() => OutKindCombo.SelectedIndex >= 1;
        bool Split() => OutKindCombo.SelectedIndex == 2;
        string NoX() => Split() ? Messages.Tips.BecauseSplit : Messages.Tips.BecausePair;
        bool Wave() => MergeRadioGroup.ItemIndex == 1;
        bool Model() => ModelValue > 0;
        bool Auto() => !Pair() && Wave() && ModelValue == AutoValue;
        bool Gcc() => ((Pair() && !Split()) || (!Pair() && Wave() && Model() && !Auto())) && AlignRadioGroup.ItemIndex >= 1;
        bool SideAlgorithm() => ModelValue >= FirstSideAlgorithm && ModelValue <= LastSideAlgorithm;
        string ModelName() => Messages.ModelShortNames[Math.Clamp(ModelValue, 0, Messages.ModelShortNames.Length - 1)];

        void Tip(Control target, string text, Func<string?> why) =>
            _links.Add(target, () => why() is { } reason ? text + Messages.Tips.NotNow + reason : text);

        string? V3Analysis() => Split() ? NoX() : Gcc() ? Messages.Tips.BecauseGcc : null;
        string? FreqOnly() => Pair() ? NoX() : Wave() ? Messages.Tips.BecauseFreqOnly : null;

        Tip(IntroRadioGroup, Messages.Tips.Intro, V3Analysis);
        Tip(AdptLvlGroupBox, Messages.Tips.TimeShift, V3Analysis);
        Tip(MergeRadioGroup, Messages.Tips.Method, () => Pair() ? NoX() : null);
        Tip(SoundQtyGroup, Messages.Tips.Accuracy, FreqOnly);
        Tip(KvolCaption, Messages.Tips.ExtractLevel, FreqOnly);
        Tip(LevelRadioGroup, Messages.Tips.Level, () =>
            Pair() ? NoX() : !Wave() ? Messages.Tips.BecauseWaveOnly : Model() ? Messages.Tips.BecauseModel(ModelName()) : null);

        Tip(DataRadioGroup, Messages.Tips.ProcMode, V3Analysis);
        Tip(PhaseRadioGroup, Messages.Tips.Phase, V3Analysis);
        Tip(FilterBox, Messages.Tips.Filtering, () => Pair() ? NoX() : null);
        Tip(OvspBox, Messages.Tips.Oversampling, () => V3Analysis() ?? (!Pair() && !Wave() ? Messages.Tips.BecauseWaveOnly : null));
        Tip(BsizeBox, Messages.Tips.Block, V3Analysis);

        Tip(VnameCheckBox.Parent!, Messages.Tips.FileName, () => null);
        Tip(OutKindCombo.Parent!, Messages.Tips.Output, () => null);
        Tip(GpuCheckBox.Parent!, Messages.Tips.Gpu, () => GpuCheckBox.Checked ? null : Messages.Tips.BecauseGpuOff);

        Tip(ModelRadioGroup, Messages.Tips.Model, () => Pair() ? NoX() : !Wave() ? Messages.Tips.BecauseWaveOnly : null);
        Tip(AlignRadioGroup, Messages.Tips.Align, () => Split() ? NoX() : Auto() ? Messages.Tips.BecauseAuto : Pair() || (Wave() && Model()) ? null : Messages.Tips.BecauseV3Align);
        Tip(SpansBox, Messages.Tips.Spans, () =>
            Pair() ? NoX() : !Wave() ? Messages.Tips.BecauseWaveOnly : !Model() ? Messages.Tips.BecauseV3Model :
            SideAlgorithm() ? Messages.Tips.BecauseNoSpans(ModelName()) : null);
        Tip(CentreRadioGroup, Messages.Tips.Centre, () => Split() ? null : Messages.Tips.BecauseNotSplit);
        Tip(KickDuckCheckBox, Messages.Tips.KickDuck, () =>
            Pair() ? NoX() : !Wave() ? Messages.Tips.BecauseWaveOnly : !Model() ? Messages.Tips.BecauseV3Model : null);
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
