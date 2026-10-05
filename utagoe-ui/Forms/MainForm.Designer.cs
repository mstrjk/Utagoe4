// 元実装 Form1 の layout 再現。
// 位置と size は TFORM1 resource から取得した 96-DPI 座標。high DPI は AutoScaleMode.Dpi に任せる。

#nullable enable

using Utagoe.App;
using Utagoe.Ui;

namespace Utagoe.Forms;

partial class MainForm
{
    private System.ComponentModel.IContainer? components;

    private Label Label1 = null!;
    private Label Label2 = null!;
    private Label Label3 = null!;
    private Label WformLbl1 = null!;
    private Label WformLbl2 = null!;
    private Label InfoLabel = null!;
    private ProgressBar ProgBar1 = null!;
    private TextBox Edit1 = null!;
    private TextBox Edit2 = null!;
    private TextBox Edit3 = null!;
    private ThemedCheckBox OverwriteCheckBox = null!;
    private ThemedCheckBox NormaliseCheckBox = null!;
    private ThemedCheckBox CacheCheckBox = null!;
    private BitBtn BitBtn1 = null!;
    private BitBtn BitBtn2 = null!;
    private BitBtn BitBtn3 = null!;
    private BitBtn PlayBtn1 = null!;
    private BitBtn PlayBtn2 = null!;
    private BitBtn StartBtn = null!;
    private BitBtn SetBitBtn = null!;
    private BitBtn HelpBtn = null!;
    private BitBtn FaqBtn = null!;
    private BitBtn AboutBtn = null!;
    private BitBtn CloseBtn = null!;
    private Panel DbgPanel = null!;
    private ToolTip Hints = null!;
    private System.Windows.Forms.Timer ElapsedTimer = null!;

    protected override void Dispose(bool disposing)
    {
        if (disposing) components?.Dispose();
        base.Dispose(disposing);
    }

    private static readonly WindowSpec Definition = new()
    {
        ClientSize = new Size(569, 294),
        Buttons = TitleButtons.All,
        Taskbar = true,
        Placement = Placement.CenterScreen,
        Title = () => "Utagoe",
    };

    private void InitializeComponent()
    {
        components = new System.ComponentModel.Container();
        Hints = new ToolTip(components);
        ElapsedTimer = new System.Windows.Forms.Timer(components) { Interval = 1000 };

        SuspendLayout();
        AllowDrop = true;

        Label1 = Element.Label("Input File (Original)", 16, 24, transparent: true);
        Label2 = Element.Label("Input File (Instrumental)", 16, 96, transparent: true);
        Label3 = Element.Label("Output Folder", 16, 184, transparent: true);
        WformLbl1 = Element.Dynamic(196, 24, transparent: true);
        WformLbl2 = Element.Dynamic(196, 96, transparent: true);
        InfoLabel = Element.Dynamic(464, 272, transparent: true);

        ProgBar1 = Element.Progress(new Rectangle(8, 270, 447, 17), 15);

        Edit1 = Element.Field(new Rectangle(24, 48, 337, 20), 0, drop: true);
        Edit2 = Element.Field(new Rectangle(24, 120, 337, 20), 3, drop: true);
        Edit3 = Element.Field(new Rectangle(24, 208, 337, 20), 6, drop: true);
        OverwriteCheckBox = Element.Check(() => Messages.OverwriteFiles, new Rectangle(24, 236, 180, 17), 8);
        NormaliseCheckBox = Element.Check(() => Messages.NormaliseOutput, new Rectangle(212, 236, 149, 17), 9);
        UiText.Tip(Hints, NormaliseCheckBox, () => Messages.NormaliseHint);
        CacheCheckBox = Element.Check(() => Messages.CacheSteps, new Rectangle(370, 236, 80, 17), 10);
        UiText.Tip(Hints, CacheCheckBox, () => Messages.CacheHint);

        BitBtn1  = Element.Glyph("folder", new Rectangle(376, 46, 25, 25), 1);
        PlayBtn1 = Element.Glyph("play",   new Rectangle(416, 46, 25, 25), 2);
        BitBtn2  = Element.Glyph("folder", new Rectangle(376, 118, 25, 25), 4);
        PlayBtn2 = Element.Glyph("play",   new Rectangle(416, 118, 25, 25), 5);
        BitBtn3  = Element.Glyph("folder", new Rectangle(376, 206, 25, 25), 7);

        StartBtn = Element.Button(() => IsRunning ? Messages.Running : Messages.Start, new Rectangle(464, 16, 89, 57), 9, "start");
        StartBtn.Font = new Font("Tahoma", 9F);
        StartBtn.WithOwnFont();

        SetBitBtn = Element.Button("Settings...", new Rectangle(464, 112, 89, 25), 10, "settings");
        SetBitBtn.Spacing = 6;
        HelpBtn = Element.Button("Help...", new Rectangle(464, 152, 89, 25), 11, "question");
        FaqBtn = Element.Button("FAQ...", new Rectangle(464, 192, 89, 25), 12, "book");

        AboutBtn = Element.Glyph("info", new Rectangle(464, 232, 25, 25), 13);
        UiText.Tip(Hints, AboutBtn, () => L.T("Version Info"));

        CloseBtn = Element.Button("Quit", new Rectangle(490, 232, 63, 25), 14, "close");
        CloseBtn.Spacing = 5;

        DbgPanel = Element.Box(new Rectangle(9, 256, 8, 8), 16);
        DbgPanel.TabStop = false;

        Controls.AddRange(new Control[]
        {
            Label1, Label2, Label3, WformLbl1, WformLbl2, InfoLabel, ProgBar1,
            Edit1, BitBtn1, PlayBtn1, Edit2, BitBtn2, PlayBtn2, Edit3, BitBtn3, OverwriteCheckBox, NormaliseCheckBox, CacheCheckBox,
            StartBtn, SetBitBtn, HelpBtn, FaqBtn, AboutBtn, CloseBtn, DbgPanel,
        });

        ResumeLayout(false);
        PerformLayout();
    }
}
