// 元実装の settings dialog layout を再現する。
// control 座標は TSETFORM1 resource 由来。tab / group box 内は親 container からの相対座標。

#nullable enable

using Utagoe.App;
using Utagoe.Ui;

namespace Utagoe.Forms;

partial class SettingsForm
{
    private System.ComponentModel.IContainer? components;

    private BitBtn OkBitBtn = null!;
    private BitBtn ResetButton = null!;
    private TabControl PageControl = null!;

    private VclRadioGroup IntroRadioGroup = null!;
    private VclRadioGroup MethodRadioGroup = null!;
    private ThemedRadioButton[] UseMethodButtons = null!;
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
    private ComboBox OutDepthCombo = null!;
    private ComboBox OutBitrateCombo = null!;
    private CheckDropDown SaveDropDown = null!;

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
    private VclGroupBox FftBox = null!;
    private ThemedComboBox FftCombo = null!;
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

        OkBitBtn = Element.Close(new Rectangle(503, 420, 89, 25), 0);
        ResetButton = Element.Button("Reset", new Rectangle(383, 420, 108, 25), 3, "reset");
        CancelButton = OkBitBtn;

        PageControl = Element.Tabs(new Rectangle(8, 8, 585, 406), 2, fillToRight: true, minRows: 1);
        FreqTab = Element.Page("Frequency");
        WaveTab = Element.Page("Waveform");
        var tab3 = Element.Page("Output");
        PageControl.TabPages.AddRange(new[] { WaveTab, FreqTab, tab3 });

        FreqPages = Element.InnerTabs(0);
        FreqMainPage = Element.Page(() => Messages.MainPage);
        FreqSettingsPage = Element.Page("Settings");
        FreqPages.TabPages.AddRange(new[] { FreqMainPage, FreqSettingsPage });
        FreqTab.Controls.Add(FreqPages);
        WavePages = Element.InnerTabs(0);
        WaveMainPage = Element.Page(() => Messages.MainPage);
        WaveSettingsPage = Element.Page("Settings");
        WavePages.TabPages.AddRange(new[] { WaveMainPage, WaveSettingsPage });
        WaveTab.Controls.Add(WavePages);

        MethodRadioGroup = Element.Choices("Extraction Method", new Rectangle(8, 8, 150, 168), 0, "Frequency", "Waveform");

        FreqModelRadioGroup = Element.Choices(() => Messages.FreqModelGroup, new Rectangle(8, 8, 270, 100), 0, () => Messages.FreqModelNames);
        SoundQtyGroup = Element.Choices("Accuracy Priority", new Rectangle(8, 114, 270, 76), 1, "Quality", "Extraction");
        KvolBox = Element.Group("Extractable Level", new Rectangle(8, 196, 270, 82), 2);
        KvolTrackBar = Element.Slider(new Rectangle(10, 18, 200, 40), 0);
        KvolText = Element.Dynamic(216, 30);
        KvolWeak = Element.Label("Weak", 14, 58);
        KvolStrong = Element.Label("Strong", 170, 58);
        KvolBox.Controls.AddRange(new Control[] { KvolText, KvolWeak, KvolStrong, KvolTrackBar });
        FreqMainPage.Controls.AddRange(new Control[] { FreqModelRadioGroup, SoundQtyGroup, KvolBox });

        SharedMain = Element.Box(new Rectangle(286, 8, 270, 140), 3);
        AlignRadioGroup = Element.Choices(() => Messages.AlignGroup, new Rectangle(0, 0, 270, 90), 0, () => Messages.AlignNames);
        MatchBandwidthCheckBox = Element.Check(() => Messages.MatchBandwidth, new Rectangle(4, 98, 262, 17), 1);
        MatchLowEndCheckBox = Element.Check(() => Messages.MatchLowEnd, new Rectangle(4, 118, 262, 17), 2);
        SharedMain.Controls.AddRange(new Control[] { AlignRadioGroup, MatchBandwidthCheckBox, MatchLowEndCheckBox });

        SharedSettings = Element.Box(new Rectangle(8, 8, 370, 250), 0);
        IntroRadioGroup = Element.Choices("Intro Analysis", new Rectangle(0, 0, 120, 128), 0, "Automatic", "Normal", "Detailed", "None");
        AdptLvlGroupBox = Element.Group("Time Shift Correction", new Rectangle(0, 134, 120, 107), 1);
        AdptAutoButton = Element.Radio("Automatic", new Rectangle(7, 18, 91, 17), 0);
        AdptManualButton = Element.Radio("Manual", new Rectangle(7, 46, 91, 17), 1);
        AdptUpDown = Element.Number(new Rectangle(63, 75, 51, 20), 2, 0, 999);
        var rangeLabel = Element.Label("Range", 10, 79);
        AdptLvlGroupBox.Controls.AddRange(new Control[] { AdptAutoButton, AdptManualButton, AdptUpDown, rangeLabel });
        DataRadioGroup = Element.Choices("Processing Mode", new Rectangle(128, 0, 110, 101), 2, "Normal", "L/R Difference", "Mono");
        PhaseRadioGroup = Element.Choices("Instrumental Phase", new Rectangle(128, 107, 110, 101), 3, "Automatic", "Positive Phase", "Inverted Phase");
        BsizeBox = Element.Group("Block Length", new Rectangle(246, 0, 124, 48), 4);
        BsizeComboBox = Element.Editable(new Rectangle(11, 20, 50, 20), 0, 3, "50", "100", "200", "400", "800");
        var bsizeUnit = Element.Label("Millseconds", 66, 24);
        BsizeBox.Controls.AddRange(new Control[] { BsizeComboBox, bsizeUnit });
        var gpuBox = Element.Group(() => Messages.GpuGroup, new Rectangle(246, 54, 124, 102), 5);
        GpuCheckBox = Element.Check(() => Messages.GpuUse, new Rectangle(11, 16, 108, 17), 0);
        GpuExactButton = Element.Radio(() => Messages.GpuExact, new Rectangle(23, 34, 96, 17), 1);
        GpuFastButton = Element.Radio(() => Messages.GpuFastest, new Rectangle(23, 52, 96, 17), 2);
        GpuStatusLabel = Element.DynamicText(Messages.GpuChecking, new Rectangle(8, 71, 110, 28), ellipsis: true);
        gpuBox.Controls.AddRange(new Control[] { GpuCheckBox, GpuExactButton, GpuFastButton, GpuStatusLabel });
        FftBox = Element.Group(() => Messages.FftGroup, new Rectangle(246, 162, 124, 48), 6);
        FftCombo = Element.List(new Rectangle(11, 20, 100, 20), 0, () => Messages.FftNames);
        FftBox.Controls.Add(FftCombo);
        SharedSettings.Controls.AddRange(new Control[] { IntroRadioGroup, AdptLvlGroupBox, DataRadioGroup, PhaseRadioGroup, BsizeBox, gpuBox, FftBox });
        FreqMainPage.Controls.Add(SharedMain);
        FreqSettingsPage.Controls.Add(SharedSettings);

        ModelRadioGroup = Element.Choices(() => Messages.ModelGroup, new Rectangle(8, 8, 270, 270), 0, () => Messages.ModelNames);

        SpansBox = Element.Group(() => Messages.SpansGroup, new Rectangle(286, 154, 270, 82), 4);
        SpansEdit = Element.Field(new Rectangle(10, 20, 174, 20), 0, 250);
        SpansPickButton = Element.Button("Pick...", new Rectangle(188, 18, 72, 24), 1);
        var spansHint = Element.Text(() => Messages.SpansHint, new Rectangle(10, 44, 252, 34));
        spansHint.UseMnemonic = true;
        SpansBox.Controls.AddRange(new Control[] { SpansEdit, SpansPickButton, spansHint });
        var modelNote = Element.Text(() => Messages.ModelNote, new Rectangle(290, 242, 266, 30));
        modelNote.UseMnemonic = true;
        WaveMainPage.Controls.AddRange(new Control[] { ModelRadioGroup, SpansBox, modelNote });

        OvspBox = Element.Group("Oversampling", new Rectangle(386, 8, 170, 70), 1);
        OvspCheckBox = Element.Check("Multiplier:", new Rectangle(11, 20, 90, 17), 0);
        OvspComboBox = Element.Editable(new Rectangle(31, 42, 57, 20), 1, 3, "8", "16", "32", "64", "128");
        OvspBox.Controls.AddRange(new Control[] { OvspCheckBox, OvspComboBox });
        LevelRadioGroup = Element.Choices("Instrumental Level Adjustment", new Rectangle(386, 84, 170, 114), 2,
                                          "Automatic (Averaged)", "Automatic (Adaptive)", "Manual", "None");
        KlvlTrackBar = Element.Slider(new Rectangle(386, 204, 150, 40), 3);
        KlvlText = Element.Dynamic(420, 244);
        WaveSettingsPage.Controls.AddRange(new Control[] { OvspBox, LevelRadioGroup, KlvlTrackBar, KlvlText });

        var fileBox = Element.Group("File Name Settings", new Rectangle(8, 8, 270, 138), 0);
        KnameCheckBox = Element.Check("Search For Instrumental File", new Rectangle(16, 24, 240, 17), 0);
        VnameCheckBox = Element.Check("Automatically Name Output File", new Rectangle(16, 55, 240, 17), 1);
        AppendLabel = Element.Label("Append To Filename:", 16, 80);
        VnameEdit = Element.Field(new Rectangle(150, 78, 100, 20), 3, 32);
        fileBox.Controls.AddRange(new Control[] { KnameCheckBox, VnameCheckBox, AppendLabel, VnameEdit });
        tab3.Controls.Add(fileBox);

        var outBox = Element.Group(() => Messages.OutputGroup, new Rectangle(286, 8, 270, 138), 1);
        var kindLabel = Element.Label(() => Messages.OutputKindLabel, 12, 26);
        SaveDropDown = Element.MultiList(new Rectangle(94, 22, 164, 20), 0, () => Messages.SaveNames);
        var fmtLabel = Element.Label("Format:", 12, 54);
        OutFormatCombo = Element.List(new Rectangle(94, 50, 164, 20), 1, () => Messages.FormatNames);
        var depthLabel = Element.Label("Bit Depth:", 12, 82);
        OutDepthCombo = Element.List(new Rectangle(94, 78, 164, 20), 2, () => L.A("Auto", "16-bit", "24-bit", "32-bit float"));
        var rateLabel = Element.Label("Bitrate:", 12, 110);
        OutBitrateCombo = Element.List(new Rectangle(94, 106, 164, 20), 3, "96 kbps", "128 kbps", "160 kbps", "192 kbps", "256 kbps", "320 kbps");
        outBox.Controls.AddRange(new Control[] { kindLabel, SaveDropDown, fmtLabel, OutFormatCombo, depthLabel, OutDepthCombo, rateLabel, OutBitrateCombo });
        tab3.Controls.Add(outBox);

        FilterBox = Element.Group("Filtering", new Rectangle(8, 152, 548, 156), 2);
        CfocusCheckBox = Element.Check("Extraction Centralization", new Rectangle(12, 24, 226, 17), 0);
        CfocusTrackBar = Element.Slider(new Rectangle(244, 14, 220, 32), 1);
        CfocusText = Element.Dynamic(470, 26);
        CfWeak = Element.Label("Weak", 248, 46);
        CfStrong = Element.Label("Strong", 426, 46);
        LPFCheckBox = Element.Check("Low Pass Filter", new Rectangle(12, 72, 226, 17), 2);
        LPFTrackBar = Element.Slider(new Rectangle(244, 62, 220, 40), 3);
        LPFLabel = Element.Value("10.0kHz", 470, 74, 50, 12);
        HPFCheckBox = Element.Check("High Pass Filter", new Rectangle(12, 112, 226, 17), 4);
        HPFTrackBar = Element.Slider(new Rectangle(244, 104, 220, 40), 5);
        HPFLabel = Element.Value("100Hz", 470, 116, 50, 12);
        SubsonicCheckBox = Element.Check(() => Messages.RemoveSubsonic, new Rectangle(12, 132, 226, 17), 6);
        FilterBox.Controls.AddRange(new Control[]
        {
            CfocusText, CfWeak, CfStrong, LPFLabel, HPFLabel,
            CfocusCheckBox, CfocusTrackBar, LPFCheckBox, LPFTrackBar, HPFCheckBox, HPFTrackBar, SubsonicCheckBox,
        });
        tab3.Controls.Add(FilterBox);

        var methodPages = new[] { FreqTab, WaveTab };
        UseMethodButtons = new ThemedRadioButton[methodPages.Length];
        for (int k = 0; k < methodPages.Length; ++k)
        {
            var header = Element.Area(DockStyle.Top, 30);
            UseMethodButtons[k] = Element.Option(() => Messages.UseMethod, new Point(10, 7), 0);
            header.Controls.Add(UseMethodButtons[k]);
            methodPages[k].Controls.Add(header);
        }
        FreqPages.BringToFront();
        WavePages.BringToFront();

        Controls.AddRange(new Control[] { OkBitBtn, PageControl, ResetButton });

        ResumeLayout(false);
        PerformLayout();
    }
}
