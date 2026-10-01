// 元実装の settings dialog layout を再現する。
// control 座標は TSETFORM1 resource 由来。tab / group box 内は親 container からの相対座標。

#nullable enable

using Utagoe.Vcl;

namespace Utagoe.Forms;

partial class SettingsForm
{
    private System.ComponentModel.IContainer? components;

    private BitBtn OkBitBtn = null!;
    private BitBtn CanBitBtn = null!;
    private Button ResetButton = null!;
    private TabControl PageControl = null!;

    private VclRadioGroup IntroRadioGroup = null!;
    private VclRadioGroup MergeRadioGroup = null!;
    private VclRadioGroup SoundQtyGroup = null!;
    private VclRadioGroup LevelRadioGroup = null!;
    private RaisedLine Panel1 = null!;
    private VclTrackBar KvolTrackBar = null!;
    private Label KvolText = null!;
    private VclTrackBar KlvlTrackBar = null!;
    private Label KlvlText = null!;
    private VclGroupBox AdptLvlGroupBox = null!;
    private RadioButton AdptAutoButton = null!;
    private RadioButton AdptManualButton = null!;
    private NumericUpDown AdptUpDown = null!;

    private VclRadioGroup DataRadioGroup = null!;
    private VclRadioGroup PhaseRadioGroup = null!;
    private CheckBox CfocusCheckBox = null!;
    private VclTrackBar CfocusTrackBar = null!;
    private Label CfocusText = null!;
    private CheckBox LPFCheckBox = null!;
    private VclTrackBar LPFTrackBar = null!;
    private VclLabel LPFLabel = null!;
    private CheckBox HPFCheckBox = null!;
    private VclTrackBar HPFTrackBar = null!;
    private VclLabel HPFLabel = null!;
    private CheckBox OvspCheckBox = null!;
    private ComboBox OvspComboBox = null!;
    private ComboBox BsizeComboBox = null!;

    private CheckBox KnameCheckBox = null!;
    private CheckBox VnameCheckBox = null!;
    private TextBox VnameEdit = null!;

    private ComboBox OutFormatCombo = null!;
    private CheckBox GpuCheckBox = null!;
    private RadioButton GpuExactButton = null!;
    private RadioButton GpuFastButton = null!;
    private Label GpuStatusLabel = null!;
    private VclRadioGroup ModelRadioGroup = null!;
    private VclRadioGroup AlignRadioGroup = null!;
    private TextBox SpansEdit = null!;
    private ThemedCheckBox KickDuckCheckBox = null!;
    private VclRadioGroup CentreRadioGroup = null!;
    private ComboBox OutDepthCombo = null!;
    private ComboBox OutBitrateCombo = null!;
    private ComboBox OutKindCombo = null!;

    // 上の選択で使われなくなる部品を灰色にするため、field として持つ (v4)。
    private Label KvolCaption = null!;
    private Label KvolWeak = null!;
    private Label KvolStrong = null!;
    private VclGroupBox FilterBox = null!;
    private Label CfWeak = null!;
    private Label CfStrong = null!;
    private VclGroupBox OvspBox = null!;
    private VclGroupBox BsizeBox = null!;
    private Label AppendLabel = null!;
    private VclGroupBox SpansBox = null!;

    protected override void Dispose(bool disposing)
    {
        if (disposing) components?.Dispose();
        base.Dispose(disposing);
    }

    private void InitializeComponent()
    {
        components = new System.ComponentModel.Container();
        SuspendLayout();

        // 実機では system menu / minimize / maximize がなく、OK / Cancel だけで閉じる。DFM の既定値より実際の window style を優先する。
        AutoScaleDimensions = new SizeF(96F, 96F);
        AutoScaleMode = AutoScaleMode.Dpi;
        ClientSize = new Size(506, 337);
        Font = new Font("Tahoma", 9F);
        FormBorderStyle = FormBorderStyle.FixedSingle;
        ControlBox = false;
        ShowInTaskbar = false;
        StartPosition = FormStartPosition.CenterScreen;
        Text = "Settings";

        OkBitBtn = new BitBtn { Bounds = new Rectangle(308, 305, 89, 25), TabIndex = 0 };
        OkBitBtn.SetKind(BitBtnKind.OK);
        CanBitBtn = new BitBtn { Bounds = new Rectangle(409, 305, 89, 25), Text = "Cancel", TabIndex = 1 };
        CanBitBtn.SetKind(BitBtnKind.Cancel);
        ResetButton = new BitBtn { Bounds = new Rectangle(200, 305, 75, 25), Text = "Reset", TabIndex = 3, UseVisualStyleBackColor = true };
        AcceptButton = OkBitBtn;
        CancelButton = CanBitBtn;

        PageControl = new ThemedTabControl { Bounds = new Rectangle(8, 8, 489, 291), TabIndex = 2 };
        var tab1 = new TabPage("Processing Method") { UseVisualStyleBackColor = true };
        var tab2 = new TabPage("Advanced") { UseVisualStyleBackColor = true };
        var tab3 = new TabPage("Output") { UseVisualStyleBackColor = true };
        var tab4 = new TabPage(App.Messages.ModelTab) { UseVisualStyleBackColor = true };
        var tab5 = new TabPage(App.Messages.CentreTab) { UseVisualStyleBackColor = true };
        PageControl.TabPages.AddRange(new[] { tab1, tab2, tab3, tab4, tab5 });

        IntroRadioGroup = MakeGroup("Intro Analysis", 8, 8, 120, 128, 0,
                                    "Automatic", "Normal", "Detailed", "None");

        // Extraction Method frame は上下 2 領域に分け、各方式の option を対応する側へ配置する。
        MergeRadioGroup = MakeGroup("Extraction Method", 135, 8, 338, 241, 2,
                                    "Frequency", "Waveform");

        SoundQtyGroup = MakeGroup("Accuracy Priority", 232, 24, 233, 88, 3, "Quality", "Extraction");
        LevelRadioGroup = MakeGroup("Instrumental Level Adjustment", 232, 126, 233, 115, 5,
                                    "Automatic (Averaged)", "Automatic (Adaptive)", "Manual", "None");

        Panel1 = new RaisedLine { Bounds = new Rectangle(144, 118, 322, 2) };

        KvolTrackBar = MakeTrackBar(309, 56, 154, 40, 4);
        KvolText = MakeLabel("", 377, 94);
        KvolCaption = MakeLabel("Extractable Level", 345, 43);
        KvolWeak = MakeLabel("Weak", 312, 91);
        KvolStrong = MakeLabel("Strong", 427, 91);

        KlvlTrackBar = MakeTrackBar(309, 184, 154, 40, 6);
        KlvlText = MakeLabel("", 374, 220);

        AdptLvlGroupBox = new VclGroupBox { Text = "Time Shift Correction", Bounds = new Rectangle(8, 142, 120, 107), TabIndex = 1 };
        AdptAutoButton = new ThemedRadioButton { Text = "Automatic", Bounds = new Rectangle(7, 18, 91, 17), TabIndex = 0 };
        AdptManualButton = new ThemedRadioButton { Text = "Manual", Bounds = new Rectangle(7, 46, 91, 17), TabIndex = 1 };
        // Edit + UpDown は 1 組として扱い、最大 999。
        AdptUpDown = new NumericUpDown { Bounds = new Rectangle(63, 75, 51, 20), Minimum = 0, Maximum = 999, TabIndex = 2 };
        var rangeLabel = MakeLabel("Range", 10, 79);
        AdptLvlGroupBox.Controls.AddRange(new Control[] { AdptAutoButton, AdptManualButton, AdptUpDown, rangeLabel });

        tab1.Controls.AddRange(new Control[]
        {
            Panel1, KvolText, KvolCaption, KvolWeak, KvolStrong, KvolTrackBar,
            KlvlText, KlvlTrackBar, SoundQtyGroup, LevelRadioGroup,
            IntroRadioGroup, AdptLvlGroupBox, MergeRadioGroup,
        });
        MergeRadioGroup.SendToBack();

        DataRadioGroup = MakeGroup("Processing Mode", 8, 8, 102, 101, 0, "Normal", "L/R Difference", "Mono");
        PhaseRadioGroup = MakeGroup("Instrumental Phase", 8, 115, 102, 101, 1,
                                    "Automatic", "Positive Phase", "Inverted Phase");

        FilterBox = new VclGroupBox { Text = "Filtering", Bounds = new Rectangle(116, 8, 224, 246), TabIndex = 2 };
        CfocusCheckBox = new ThemedCheckBox { Text = "Extraction Centralization", Bounds = new Rectangle(16, 21, 125, 17), TabIndex = 0, AutoSize = true };
        CfocusTrackBar = MakeTrackBar(25, 44, 153, 40, 1);
        CfocusText = MakeLabel("", 180, 56);
        CfWeak = MakeLabel("Weak", 28, 79);
        CfStrong = MakeLabel("Strong", 164, 79);
        LPFCheckBox = new ThemedCheckBox { Text = "Low Pass Filter", Bounds = new Rectangle(16, 100, 110, 17), TabIndex = 2 };
        LPFTrackBar = MakeTrackBar(25, 123, 153, 40, 3);
        // 右寄せ AutoSize label は右端位置を固定する。
        LPFLabel = new VclLabel(rightJustify: true) { Text = "10.0kHz", Bounds = new Rectangle(178, 137, 39, 12) };
        HPFCheckBox = new ThemedCheckBox { Text = "High Pass Filter", Bounds = new Rectangle(16, 180, 110, 17), TabIndex = 4 };
        HPFTrackBar = MakeTrackBar(25, 203, 153, 40, 5);
        HPFLabel = new VclLabel(rightJustify: true) { Text = "100Hz", Bounds = new Rectangle(180, 217, 31, 12) };
        FilterBox.Controls.AddRange(new Control[]
        {
            CfocusText, CfWeak, CfStrong, LPFLabel, HPFLabel,
            CfocusCheckBox, CfocusTrackBar, LPFCheckBox, LPFTrackBar, HPFCheckBox, HPFTrackBar,
        });

        OvspBox = new VclGroupBox { Text = "Oversampling", Bounds = new Rectangle(346, 8, 126, 88), TabIndex = 3 };
        OvspCheckBox = new ThemedCheckBox { Text = "Multiplier:", Bounds = new Rectangle(11, 16, 110, 17), TabIndex = 0 };
        OvspComboBox = new ComboBox { Bounds = new Rectangle(31, 39, 57, 20), MaxLength = 3, TabIndex = 1 };
        OvspComboBox.Items.AddRange(new object[] { "8", "16", "32", "64", "128" });
        var ovspNote = new ThemedLabel
        {
            Text = "(For \"Waveform\" Method)", Bounds = new Rectangle(8, 62, 110, 24),
            TextAlign = ContentAlignment.TopCenter,
        };
        OvspBox.Controls.AddRange(new Control[] { OvspCheckBox, OvspComboBox, ovspNote });

        BsizeBox = new VclGroupBox { Text = "Block Length", Bounds = new Rectangle(346, 100, 126, 48), TabIndex = 4 };
        BsizeComboBox = new ComboBox { Bounds = new Rectangle(11, 20, 50, 20), MaxLength = 3, TabIndex = 0 };
        BsizeComboBox.Items.AddRange(new object[] { "50", "100", "200", "400", "800" });
        // "Millseconds" の typo は元実装どおり残す。
        var bsizeUnit = MakeLabel("Millseconds", 66, 24);
        BsizeBox.Controls.AddRange(new Control[] { BsizeComboBox, bsizeUnit });


        var fileBox = new VclGroupBox { Text = "File Name Settings", Bounds = new Rectangle(8, 8, 225, 138), TabIndex = 0 };
        KnameCheckBox = new ThemedCheckBox { Text = "Search For Instrumental File", Bounds = new Rectangle(16, 24, 200, 17), TabIndex = 0 };
        VnameCheckBox = new ThemedCheckBox { Text = "Automatically Name Output File", Bounds = new Rectangle(16, 55, 200, 17), TabIndex = 1 };
        AppendLabel = MakeLabel("Append To Filename:", 16, 80);
        VnameEdit = new TextBox { Bounds = new Rectangle(125, 78, 81, 20), MaxLength = 32, TabIndex = 3 };
        fileBox.Controls.AddRange(new Control[] { KnameCheckBox, VnameCheckBox, AppendLabel, VnameEdit });
        tab3.Controls.Add(fileBox);

        // Misc tab の空きに出力の group を置く。位置と部品の大きさは既存 group に揃えている。
        // 1 行目は出力するもの (声か、原曲と揃えたインストの組か)。v3 にない。
        var outBox = new VclGroupBox { Text = App.Messages.OutputGroup, Bounds = new Rectangle(240, 8, 232, 138), TabIndex = 1 };
        var kindLabel = MakeLabel(App.Messages.OutputKindLabel, 12, 26);
        OutKindCombo = new ComboBox { Bounds = new Rectangle(84, 22, 136, 20), DropDownStyle = ComboBoxStyle.DropDownList, TabIndex = 0 };
        OutKindCombo.Items.AddRange(App.Messages.OutputKindNames);
        var fmtLabel = MakeLabel("Format:", 12, 54);
        OutFormatCombo = new ComboBox { Bounds = new Rectangle(84, 50, 136, 20), DropDownStyle = ComboBoxStyle.DropDownList, TabIndex = 1 };
        OutFormatCombo.Items.AddRange(App.Messages.FormatNames);
        var depthLabel = MakeLabel("Bit Depth:", 12, 82);
        OutDepthCombo = new ComboBox { Bounds = new Rectangle(84, 78, 136, 20), DropDownStyle = ComboBoxStyle.DropDownList, TabIndex = 2 };
        OutDepthCombo.Items.AddRange(new object[] { "Auto", "16-bit", "24-bit", "32-bit float" });
        var rateLabel = MakeLabel("Bitrate:", 12, 110);
        OutBitrateCombo = new ComboBox { Bounds = new Rectangle(84, 106, 136, 20), DropDownStyle = ComboBoxStyle.DropDownList, TabIndex = 3 };
        OutBitrateCombo.Items.AddRange(new object[] { "96 kbps", "128 kbps", "160 kbps", "192 kbps", "256 kbps", "320 kbps" });
        outBox.Controls.AddRange(new Control[] { kindLabel, OutKindCombo, fmtLabel, OutFormatCombo, depthLabel, OutDepthCombo, rateLabel, OutBitrateCombo });
        tab3.Controls.Add(outBox);

        // GPU の group は v3 にない。Misc tab 下段の空きに、上の 2 group と同じ幅でまとめて置く。
        var gpuBox = new VclGroupBox { Text = App.Messages.GpuGroup, Bounds = new Rectangle(346, 152, 126, 102), TabIndex = 5 };
        GpuCheckBox = new ThemedCheckBox { Text = App.Messages.GpuUse, Bounds = new Rectangle(11, 16, 110, 17), TabIndex = 0 };
        GpuExactButton = new ThemedRadioButton { Text = App.Messages.GpuExact, Bounds = new Rectangle(23, 34, 98, 17), TabIndex = 1 };
        GpuFastButton = new ThemedRadioButton { Text = App.Messages.GpuFastest, Bounds = new Rectangle(23, 52, 98, 17), TabIndex = 2 };
        GpuStatusLabel = new ThemedLabel { Text = App.Messages.GpuChecking, Bounds = new Rectangle(8, 71, 112, 28), AutoEllipsis = true };
        gpuBox.Controls.AddRange(new Control[] { GpuCheckBox, GpuExactButton, GpuFastButton, GpuStatusLabel });
        tab2.Controls.AddRange(new Control[] { DataRadioGroup, PhaseRadioGroup, FilterBox, OvspBox, BsizeBox, gpuBox });

        // Waveform の代替モデル (v3 にない)。左にモデル、右に位置合わせと声の無い区間を置く。
        ModelRadioGroup = MakeGroup(App.Messages.ModelGroup, 8, 8, 270, 222, 0, App.Messages.ModelNames);
        ModelRadioGroup.Scrollable = true;
        AlignRadioGroup = MakeGroup(App.Messages.AlignGroup, 286, 8, 186, 90, 1, App.Messages.AlignNames);
        SpansBox = new VclGroupBox { Text = App.Messages.SpansGroup, Bounds = new Rectangle(286, 104, 186, 104), TabIndex = 2 };
        SpansEdit = new TextBox { Bounds = new Rectangle(10, 20, 166, 20), MaxLength = 250, TabIndex = 0 };
        var spansHint = new ThemedLabel { Text = App.Messages.SpansHint, Bounds = new Rectangle(10, 46, 168, 54) };
        SpansBox.Controls.AddRange(new Control[] { SpansEdit, spansHint });
        var modelNote = new ThemedLabel { Text = App.Messages.ModelNote, Bounds = new Rectangle(10, 236, 460, 16) };
        KickDuckCheckBox = new ThemedCheckBox { Text = App.Messages.KickDuck, Bounds = new Rectangle(290, 213, 182, 17), TabIndex = 3 };
        tab4.Controls.AddRange(new Control[] { ModelRadioGroup, AlignRadioGroup, SpansBox, KickDuckCheckBox, modelNote });

        CentreRadioGroup = MakeGroup(App.Messages.CentreGroup, 8, 8, 464, 222, 0, App.Messages.CentreNames);
        var centreNote = new ThemedLabel { Text = App.Messages.CentreNote, Bounds = new Rectangle(10, 236, 460, 16) };
        tab5.Controls.AddRange(new Control[] { CentreRadioGroup, centreNote });

        Controls.AddRange(new Control[] { OkBitBtn, CanBitBtn, PageControl, ResetButton });

        ResumeLayout(false);
        PerformLayout();
    }

    private static VclRadioGroup MakeGroup(string caption, int left, int top, int width, int height,
                                           int tabIndex, params string[] items)
    {
        var g = new VclRadioGroup
        {
            Text = caption, Bounds = new Rectangle(left, top, width, height), TabIndex = tabIndex,
        };
        g.Items = items;
        return g;
    }

    // settings の trackbar は Max=20、PageSize=1、両側 tick、thumb length 15。
    private static VclTrackBar MakeTrackBar(int left, int top, int width, int height, int tabIndex) => new(ticksBoth: true, thumbLength: 15)
    {
        Bounds = new Rectangle(left, top, width, height),
        Minimum = 0,
        Maximum = 20,
        SmallChange = 1,
        LargeChange = 1,
        TabIndex = tabIndex,
    };

    private static Label MakeLabel(string text, int left, int top) => new ThemedLabel()
    {
        Text = text, Location = new Point(left, top), AutoSize = true,
    };
}
