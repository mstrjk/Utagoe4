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
    private VclRadioGroup MethodRadioGroup = null!;
    private ThemedRadioButton[] UseMethodButtons = null!;
    private Label[] NeedsLabels = null!;
    private VclRadioGroup SoundQtyGroup = null!;
    private VclRadioGroup LevelRadioGroup = null!;
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
    private BitBtn SpansPickButton = null!;
    private ThemedCheckBox MatchBandwidthCheckBox = null!;
    private ThemedCheckBox MatchLowEndCheckBox = null!;
    private ThemedCheckBox SubsonicCheckBox = null!;
    private VclRadioGroup CentreRadioGroup = null!;
    private ComboBox OutDepthCombo = null!;
    private ComboBox OutBitrateCombo = null!;
    private CheckDropDown SaveDropDown = null!;
    private VclGroupBox RepeatsBox = null!;
    private ComboBox RepeatGuideCombo = null!;
    private ComboBox RepeatSearchCombo = null!;
    private VclRadioGroup UpmixRadioGroup = null!;
    private ComboBox UpmixLayoutCombo = null!;
    private ThemedCheckBox UpmixLfeCheckBox = null!;
    private ComboBox UpmixVocalCombo = null!;
    private Label UpmixLayoutLabel = null!;
    private Label UpmixVocalLabel = null!;

    // 上の選択で使われなくなる部品を灰色にするため、field として持つ (v4)。
    private VclGroupBox KvolBox = null!;
    private VclRadioGroup FreqModelRadioGroup = null!;
    private TabPage FreqTab = null!;
    private TabPage WaveTab = null!;
    private ThemedTabControl FreqPages = null!;
    private ThemedTabControl WavePages = null!;
    private TabPage FreqMainPage = null!;
    private TabPage FreqSettingsPage = null!;
    private TabPage WaveMainPage = null!;
    private TabPage WaveSettingsPage = null!;
    private Panel SharedMain = null!;
    private Panel SharedSettings = null!;
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
        ClientSize = new Size(600, 452);
        Font = new Font("Tahoma", 9F);
        FormBorderStyle = FormBorderStyle.FixedSingle;
        ControlBox = false;
        ShowInTaskbar = false;
        StartPosition = FormStartPosition.CenterScreen;
        Text = "Settings";

        OkBitBtn = new BitBtn { Bounds = new Rectangle(402, 420, 89, 25), TabIndex = 0 };
        OkBitBtn.SetKind(BitBtnKind.OK);
        CanBitBtn = new BitBtn { Bounds = new Rectangle(503, 420, 89, 25), Text = "Cancel", TabIndex = 1 };
        CanBitBtn.SetKind(BitBtnKind.Cancel);
        ResetButton = new BitBtn { Bounds = new Rectangle(282, 420, 108, 25), Text = "Reset", TabIndex = 3, UseVisualStyleBackColor = true };
        ((BitBtn)ResetButton).SetGlyph("reset");
        AcceptButton = OkBitBtn;
        CancelButton = CanBitBtn;

        PageControl = new ThemedTabControl { Bounds = new Rectangle(8, 8, 585, 406), TabIndex = 2, Multiline = true, SizeMode = TabSizeMode.FillToRight, MinRows = 2 };
        FreqTab = new TabPage("Frequency") { UseVisualStyleBackColor = true };
        WaveTab = new TabPage("Waveform") { UseVisualStyleBackColor = true };
        var tab5 = new TabPage(App.Messages.CentreTab) { UseVisualStyleBackColor = true };
        var tab6 = new TabPage(App.Messages.RepeatsGroup) { UseVisualStyleBackColor = true };
        var tab7 = new TabPage(App.Messages.UpmixTab) { UseVisualStyleBackColor = true };
        var tab3 = new TabPage("Output") { UseVisualStyleBackColor = true };
        PageControl.TabPages.AddRange(new[] { WaveTab, FreqTab, tab5, tab7, tab6, tab3 });

        FreqPages = new ThemedTabControl { Dock = DockStyle.Fill, TabIndex = 0, SizeMode = TabSizeMode.Normal };
        FreqMainPage = new TabPage(App.Messages.MainPage) { UseVisualStyleBackColor = true };
        FreqSettingsPage = new TabPage("Settings") { UseVisualStyleBackColor = true };
        FreqPages.TabPages.AddRange(new[] { FreqMainPage, FreqSettingsPage });
        FreqTab.Controls.Add(FreqPages);
        WavePages = new ThemedTabControl { Dock = DockStyle.Fill, TabIndex = 0, SizeMode = TabSizeMode.Normal };
        WaveMainPage = new TabPage(App.Messages.MainPage) { UseVisualStyleBackColor = true };
        WaveSettingsPage = new TabPage("Settings") { UseVisualStyleBackColor = true };
        WavePages.TabPages.AddRange(new[] { WaveMainPage, WaveSettingsPage });
        WaveTab.Controls.Add(WavePages);

        MethodRadioGroup = MakeGroup("Extraction Method", 8, 8, 150, 168, 0,
                                     "Frequency", "Waveform", "Centre + sides", "Repeats", "Upmix");
        CentreRadioGroup = MakeGroup(App.Messages.CentreGroup, 8, 8, 548, 222, 0, App.Messages.CentreNames);
        RepeatsBox = new VclGroupBox { Text = App.Messages.RepeatsGroup, Bounds = new Rectangle(8, 8, 306, 78), TabIndex = 0 };
        var guideLabel = MakeLabel(App.Messages.RepeatGuideLabel, 10, 24);
        RepeatGuideCombo = new ComboBox { Bounds = new Rectangle(84, 20, 210, 20), DropDownStyle = ComboBoxStyle.DropDownList, TabIndex = 0 };
        RepeatGuideCombo.Items.AddRange(App.Messages.RepeatGuideNames);
        var searchLabel = MakeLabel(App.Messages.RepeatSearchLabel, 10, 50);
        RepeatSearchCombo = new ComboBox { Bounds = new Rectangle(84, 46, 210, 20), DropDownStyle = ComboBoxStyle.DropDownList, TabIndex = 1 };
        RepeatSearchCombo.Items.AddRange(App.Messages.RepeatSearchNames);
        RepeatsBox.Controls.AddRange(new Control[] { guideLabel, RepeatGuideCombo, searchLabel, RepeatSearchCombo });
        tab5.Controls.Add(CentreRadioGroup);
        tab6.Controls.Add(RepeatsBox);

        UpmixRadioGroup = MakeGroup(App.Messages.UpmixGroup, 8, 8, 330, 256, 0, App.Messages.UpmixNames);
        UpmixLayoutLabel = MakeLabel(App.Messages.UpmixLayoutLabel, 348, 18);
        UpmixLayoutCombo = new ComboBox { Bounds = new Rectangle(348, 36, 208, 20), DropDownStyle = ComboBoxStyle.DropDownList, TabIndex = 1 };
        UpmixLayoutCombo.Items.AddRange(new object[] { "5.1", "7.1" });
        UpmixLfeCheckBox = new ThemedCheckBox { Text = App.Messages.UpmixLfe, Bounds = new Rectangle(348, 64, 208, 17), TabIndex = 2 };
        UpmixVocalLabel = MakeLabel(App.Messages.UpmixVocalLabel, 348, 94);
        UpmixVocalCombo = new ComboBox { Bounds = new Rectangle(348, 112, 208, 20), DropDownStyle = ComboBoxStyle.DropDownList, TabIndex = 3 };
        UpmixVocalCombo.Items.AddRange(App.Messages.UpmixVocalNames);
        var upmixNote = new ThemedLabel { Text = App.Messages.UpmixNote, Bounds = new Rectangle(348, 142, 208, 88) };
        tab7.Controls.AddRange(new Control[] { UpmixRadioGroup, UpmixLayoutLabel, UpmixLayoutCombo, UpmixLfeCheckBox, UpmixVocalLabel, UpmixVocalCombo, upmixNote });

        // Frequency › Main
        FreqModelRadioGroup = MakeGroup(App.Messages.FreqModelGroup, 8, 8, 270, 100, 0, App.Messages.FreqModelNames);
        SoundQtyGroup = MakeGroup("Accuracy Priority", 8, 114, 270, 76, 1, "Quality", "Extraction");
        KvolBox = new VclGroupBox { Text = "Extractable Level", Bounds = new Rectangle(8, 196, 270, 82), TabIndex = 2 };
        KvolTrackBar = MakeTrackBar(10, 18, 200, 40, 0);
        KvolText = MakeLabel("", 216, 30);
        KvolWeak = MakeLabel("Weak", 14, 58);
        KvolStrong = MakeLabel("Strong", 170, 58);
        KvolBox.Controls.AddRange(new Control[] { KvolText, KvolWeak, KvolStrong, KvolTrackBar });
        FreqMainPage.Controls.AddRange(new Control[] { FreqModelRadioGroup, SoundQtyGroup, KvolBox });

        // Frequency と Waveform の両方で使う部品。開いている方の page へ移す。
        SharedMain = new Panel { Bounds = new Rectangle(286, 8, 270, 140), TabIndex = 3 };
        AlignRadioGroup = MakeGroup(App.Messages.AlignGroup, 0, 0, 270, 90, 0, App.Messages.AlignNames);
        MatchBandwidthCheckBox = new ThemedCheckBox { Text = App.Messages.MatchBandwidth, Bounds = new Rectangle(4, 98, 262, 17), TabIndex = 1 };
        MatchLowEndCheckBox = new ThemedCheckBox { Text = App.Messages.MatchLowEnd, Bounds = new Rectangle(4, 118, 262, 17), TabIndex = 2 };
        SharedMain.Controls.AddRange(new Control[] { AlignRadioGroup, MatchBandwidthCheckBox, MatchLowEndCheckBox });

        SharedSettings = new Panel { Bounds = new Rectangle(8, 8, 370, 250), TabIndex = 0 };
        IntroRadioGroup = MakeGroup("Intro Analysis", 0, 0, 120, 128, 0, "Automatic", "Normal", "Detailed", "None");
        AdptLvlGroupBox = new VclGroupBox { Text = "Time Shift Correction", Bounds = new Rectangle(0, 134, 120, 107), TabIndex = 1 };
        AdptAutoButton = new ThemedRadioButton { Text = "Automatic", Bounds = new Rectangle(7, 18, 91, 17), TabIndex = 0 };
        AdptManualButton = new ThemedRadioButton { Text = "Manual", Bounds = new Rectangle(7, 46, 91, 17), TabIndex = 1 };
        // Edit + UpDown は 1 組として扱い、最大 999。
        AdptUpDown = new NumericUpDown { Bounds = new Rectangle(63, 75, 51, 20), Minimum = 0, Maximum = 999, TabIndex = 2 };
        var rangeLabel = MakeLabel("Range", 10, 79);
        AdptLvlGroupBox.Controls.AddRange(new Control[] { AdptAutoButton, AdptManualButton, AdptUpDown, rangeLabel });
        DataRadioGroup = MakeGroup("Processing Mode", 128, 0, 110, 101, 2, "Normal", "L/R Difference", "Mono");
        PhaseRadioGroup = MakeGroup("Instrumental Phase", 128, 107, 110, 101, 3, "Automatic", "Positive Phase", "Inverted Phase");
        BsizeBox = new VclGroupBox { Text = "Block Length", Bounds = new Rectangle(246, 0, 124, 48), TabIndex = 4 };
        BsizeComboBox = new ComboBox { Bounds = new Rectangle(11, 20, 50, 20), MaxLength = 3, TabIndex = 0 };
        BsizeComboBox.Items.AddRange(new object[] { "50", "100", "200", "400", "800" });
        // "Millseconds" の typo は元実装どおり残す。
        var bsizeUnit = MakeLabel("Millseconds", 66, 24);
        BsizeBox.Controls.AddRange(new Control[] { BsizeComboBox, bsizeUnit });
        // GPU の group は v3 にない。
        var gpuBox = new VclGroupBox { Text = App.Messages.GpuGroup, Bounds = new Rectangle(246, 54, 124, 102), TabIndex = 5 };
        GpuCheckBox = new ThemedCheckBox { Text = App.Messages.GpuUse, Bounds = new Rectangle(11, 16, 108, 17), TabIndex = 0 };
        GpuExactButton = new ThemedRadioButton { Text = App.Messages.GpuExact, Bounds = new Rectangle(23, 34, 96, 17), TabIndex = 1 };
        GpuFastButton = new ThemedRadioButton { Text = App.Messages.GpuFastest, Bounds = new Rectangle(23, 52, 96, 17), TabIndex = 2 };
        GpuStatusLabel = new ThemedLabel { Text = App.Messages.GpuChecking, Bounds = new Rectangle(8, 71, 110, 28), AutoEllipsis = true };
        gpuBox.Controls.AddRange(new Control[] { GpuCheckBox, GpuExactButton, GpuFastButton, GpuStatusLabel });
        SharedSettings.Controls.AddRange(new Control[] { IntroRadioGroup, AdptLvlGroupBox, DataRadioGroup, PhaseRadioGroup, BsizeBox, gpuBox });
        FreqMainPage.Controls.Add(SharedMain);
        FreqSettingsPage.Controls.Add(SharedSettings);

        // Waveform › Main
        ModelRadioGroup = MakeGroup(App.Messages.ModelGroup, 8, 8, 270, 270, 0, App.Messages.ModelNames);

        SpansBox = new VclGroupBox { Text = App.Messages.SpansGroup, Bounds = new Rectangle(286, 154, 270, 82), TabIndex = 4 };
        SpansEdit = new TextBox { Bounds = new Rectangle(10, 20, 174, 20), MaxLength = 250, TabIndex = 0 };
        SpansPickButton = new BitBtn { Text = App.L.T("Pick..."), Bounds = new Rectangle(188, 18, 72, 24), TabIndex = 1 };
        var spansHint = new ThemedLabel { Text = App.Messages.SpansHint, Bounds = new Rectangle(10, 44, 252, 34) };
        SpansBox.Controls.AddRange(new Control[] { SpansEdit, SpansPickButton, spansHint });
        var modelNote = new ThemedLabel { Text = App.Messages.ModelNote, Bounds = new Rectangle(290, 242, 266, 30) };
        WaveMainPage.Controls.AddRange(new Control[] { ModelRadioGroup, SpansBox, modelNote });

        // Waveform › Settings
        OvspBox = new VclGroupBox { Text = "Oversampling", Bounds = new Rectangle(386, 8, 170, 70), TabIndex = 1 };
        OvspCheckBox = new ThemedCheckBox { Text = "Multiplier:", Bounds = new Rectangle(11, 20, 90, 17), TabIndex = 0 };
        OvspComboBox = new ComboBox { Bounds = new Rectangle(31, 42, 57, 20), MaxLength = 3, TabIndex = 1 };
        OvspComboBox.Items.AddRange(new object[] { "8", "16", "32", "64", "128" });
        OvspBox.Controls.AddRange(new Control[] { OvspCheckBox, OvspComboBox });
        LevelRadioGroup = MakeGroup("Instrumental Level Adjustment", 386, 84, 170, 114, 2,
                                    "Automatic (Averaged)", "Automatic (Adaptive)", "Manual", "None");
        KlvlTrackBar = MakeTrackBar(386, 204, 150, 40, 3);
        KlvlText = MakeLabel("", 420, 244);
        WaveSettingsPage.Controls.AddRange(new Control[] { OvspBox, LevelRadioGroup, KlvlTrackBar, KlvlText });

        // Output
        var fileBox = new VclGroupBox { Text = "File Name Settings", Bounds = new Rectangle(8, 8, 270, 138), TabIndex = 0 };
        KnameCheckBox = new ThemedCheckBox { Text = "Search For Instrumental File", Bounds = new Rectangle(16, 24, 240, 17), TabIndex = 0 };
        VnameCheckBox = new ThemedCheckBox { Text = "Automatically Name Output File", Bounds = new Rectangle(16, 55, 240, 17), TabIndex = 1 };
        AppendLabel = MakeLabel("Append To Filename:", 16, 80);
        VnameEdit = new TextBox { Bounds = new Rectangle(150, 78, 100, 20), MaxLength = 32, TabIndex = 3 };
        fileBox.Controls.AddRange(new Control[] { KnameCheckBox, VnameCheckBox, AppendLabel, VnameEdit });
        tab3.Controls.Add(fileBox);

        // 1 行目は出力するもの (声か、原曲と揃えたインストの組か)。v3 にない。
        var outBox = new VclGroupBox { Text = App.Messages.OutputGroup, Bounds = new Rectangle(286, 8, 270, 138), TabIndex = 1 };
        var kindLabel = MakeLabel(App.Messages.OutputKindLabel, 12, 26);
        SaveDropDown = new CheckDropDown { Bounds = new Rectangle(94, 22, 164, 20), TabIndex = 0, Choices = App.Messages.SaveNames };
        var fmtLabel = MakeLabel("Format:", 12, 54);
        OutFormatCombo = new ComboBox { Bounds = new Rectangle(94, 50, 164, 20), DropDownStyle = ComboBoxStyle.DropDownList, TabIndex = 1 };
        OutFormatCombo.Items.AddRange(App.Messages.FormatNames);
        var depthLabel = MakeLabel("Bit Depth:", 12, 82);
        OutDepthCombo = new ComboBox { Bounds = new Rectangle(94, 78, 164, 20), DropDownStyle = ComboBoxStyle.DropDownList, TabIndex = 2 };
        OutDepthCombo.Items.AddRange(new object[] { "Auto", "16-bit", "24-bit", "32-bit float" });
        var rateLabel = MakeLabel("Bitrate:", 12, 110);
        OutBitrateCombo = new ComboBox { Bounds = new Rectangle(94, 106, 164, 20), DropDownStyle = ComboBoxStyle.DropDownList, TabIndex = 3 };
        OutBitrateCombo.Items.AddRange(new object[] { "96 kbps", "128 kbps", "160 kbps", "192 kbps", "256 kbps", "320 kbps" });
        outBox.Controls.AddRange(new Control[] { kindLabel, SaveDropDown, fmtLabel, OutFormatCombo, depthLabel, OutDepthCombo, rateLabel, OutBitrateCombo });
        tab3.Controls.Add(outBox);

        // 後処理は出力にかかるので Output に置く。左に on/off、右にその強さ。
        FilterBox = new VclGroupBox { Text = "Filtering", Bounds = new Rectangle(8, 152, 548, 156), TabIndex = 2 };
        CfocusCheckBox = new ThemedCheckBox { Text = "Extraction Centralization", Bounds = new Rectangle(12, 24, 226, 17), TabIndex = 0 };
        CfocusTrackBar = MakeTrackBar(244, 14, 220, 40, 1);
        CfocusText = MakeLabel("", 470, 26);
        CfWeak = MakeLabel("Weak", 248, 46);
        CfStrong = MakeLabel("Strong", 426, 46);
        LPFCheckBox = new ThemedCheckBox { Text = "Low Pass Filter", Bounds = new Rectangle(12, 72, 226, 17), TabIndex = 2 };
        LPFTrackBar = MakeTrackBar(244, 62, 220, 40, 3);
        // 右寄せ AutoSize label は右端位置を固定する。
        LPFLabel = new VclLabel(rightJustify: true) { Text = "10.0kHz", Bounds = new Rectangle(470, 74, 50, 12) };
        HPFCheckBox = new ThemedCheckBox { Text = "High Pass Filter", Bounds = new Rectangle(12, 112, 226, 17), TabIndex = 4 };
        HPFTrackBar = MakeTrackBar(244, 104, 220, 40, 5);
        HPFLabel = new VclLabel(rightJustify: true) { Text = "100Hz", Bounds = new Rectangle(470, 116, 50, 12) };
        SubsonicCheckBox = new ThemedCheckBox { Text = App.Messages.RemoveSubsonic, Bounds = new Rectangle(12, 132, 226, 17), TabIndex = 6 };
        FilterBox.Controls.AddRange(new Control[]
        {
            CfocusText, CfWeak, CfStrong, LPFLabel, HPFLabel,
            CfocusCheckBox, CfocusTrackBar, LPFCheckBox, LPFTrackBar, HPFCheckBox, HPFTrackBar, SubsonicCheckBox,
        });
        tab3.Controls.Add(FilterBox);

        // 方式を選ぶ専用 tab は無い。各方式の tab の上に「この方式を使う」を置く (MethodRadioGroup は状態だけを持つ)。
        foreach (var page in new[] { tab5, tab6, tab7 })
            foreach (Control child in page.Controls) child.Top += 32;
        var methodPages = new[] { FreqTab, WaveTab, tab5, tab6, tab7 };
        UseMethodButtons = new ThemedRadioButton[methodPages.Length];
        NeedsLabels = new Label[methodPages.Length];
        for (int k = 0; k < methodPages.Length; ++k)
        {
            var header = new Panel { Dock = DockStyle.Top, Height = 30 };
            UseMethodButtons[k] = new ThemedRadioButton { Text = App.Messages.UseMethod, Location = new Point(10, 7), AutoSize = true, TabIndex = 0 };
            NeedsLabels[k] = new ThemedLabel { Location = new Point(250, 9), AutoSize = true };
            header.Controls.AddRange(new Control[] { UseMethodButtons[k], NeedsLabels[k] });
            methodPages[k].Controls.Add(header);
        }
        FreqPages.BringToFront();
        WavePages.BringToFront();

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
