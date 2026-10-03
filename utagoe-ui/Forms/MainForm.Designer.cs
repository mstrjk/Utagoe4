// 元実装 Form1 の layout 再現。
// 位置と size は TFORM1 resource から取得した 96-DPI 座標。high DPI は AutoScaleMode.Dpi に任せる。

#nullable enable

using Utagoe.Vcl;

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
    private BitBtn BitBtn1 = null!;
    private BitBtn BitBtn2 = null!;
    private BitBtn BitBtn3 = null!;
    private BitBtn PlayBtn1 = null!;
    private BitBtn PlayBtn2 = null!;
    private BitBtn StartBtn = null!;
    private BitBtn SetBitBtn = null!;
    private BitBtn HelpBtn = null!;
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

    private void InitializeComponent()
    {
        components = new System.ComponentModel.Container();
        Hints = new ToolTip(components);
        ElapsedTimer = new System.Windows.Forms.Timer(components) { Interval = 1000 };

        SuspendLayout();

        // window は single border、screen center、Tahoma 9pt。v3 は system menu + minimize のみ。
        // v4 は最大化もでき、最大化すると Settings と terminal を中に並べる (MainForm.Panels.cs)。
        AutoScaleDimensions = new SizeF(96F, 96F);
        AutoScaleMode = AutoScaleMode.Dpi;
        ClientSize = new Size(569, 294);
        Font = new Font("Tahoma", 9F);
        FormBorderStyle = FormBorderStyle.FixedSingle;
        MaximizeBox = true;
        StartPosition = FormStartPosition.CenterScreen;
        Text = "Utagoe";
        AllowDrop = true;

        // v4: 入力はどの形式でもよいので ".WAV" を外し、出力はファイルではなく folder を選ぶ。
        Label1 = MakeLabel("Input File (Original)", 16, 24);
        Label2 = MakeLabel("Input File (Instrumental)", 16, 96);
        Label3 = MakeLabel("Output Folder", 16, 184);
        WformLbl1 = MakeLabel("", 196, 24);
        WformLbl2 = MakeLabel("", 196, 96);
        InfoLabel = MakeLabel("", 464, 272);

        ProgBar1 = new ThemedProgressBar { Bounds = new Rectangle(8, 270, 447, 17), TabIndex = 14 };

        Edit1 = new TextBox { Bounds = new Rectangle(24, 48, 337, 20), TabIndex = 0, AllowDrop = true };
        Edit2 = new TextBox { Bounds = new Rectangle(24, 120, 337, 20), TabIndex = 3, AllowDrop = true };
        Edit3 = new TextBox { Bounds = new Rectangle(24, 208, 337, 20), TabIndex = 6, AllowDrop = true };
        OverwriteCheckBox = new ThemedCheckBox { Text = App.Messages.OverwriteFiles, Bounds = new Rectangle(24, 236, 180, 17), TabIndex = 8 };
        NormaliseCheckBox = new ThemedCheckBox { Text = App.Messages.NormaliseOutput, Bounds = new Rectangle(212, 236, 149, 17), TabIndex = 9 };
        Hints.SetToolTip(NormaliseCheckBox, App.Messages.NormaliseHint);

        BitBtn1  = MakeGlyphButton("folder",   376, 46,  1);
        PlayBtn1 = MakeGlyphButton("play",     416, 46,  2);
        BitBtn2  = MakeGlyphButton("folder",   376, 118, 4);
        PlayBtn2 = MakeGlyphButton("play",     416, 118, 5);
        BitBtn3  = MakeGlyphButton("folder",   376, 206, 7);
        // v4: 出力欄は folder なので、v3 の出力の再生 button (PlayBtn3) は置かない。

        // ParentFont=False の control も VCL と同様に font scaling される。
        // v4: Settings / Help / About は v3 の DFM では glyph を左端から固定 margin で置いていたが、
        // 拡大後の button では中身が左に寄って見えるので、glyph と caption をまとめて中央に置く (GlyphMargin 既定 -1)。
        StartBtn = new BitBtn { Bounds = new Rectangle(464, 16, 89, 57), Text = "Start", TabIndex = 9, Font = new Font("Tahoma", 9F) }.WithOwnFont();
        StartBtn.SetGlyph("start");

        SetBitBtn = new BitBtn
        {
            Bounds = new Rectangle(464, 152, 89, 25), Text = "Settings...", TabIndex = 10,
            Spacing = 6,
        };
        SetBitBtn.SetGlyph("settings");

        HelpBtn = new BitBtn
        {
            Bounds = new Rectangle(464, 192, 89, 25), Text = "Help...", TabIndex = 11,
        };
        HelpBtn.SetKind(BitBtnKind.Help);
        HelpBtn.SetGlyph("book");

        AboutBtn = new BitBtn { Bounds = new Rectangle(464, 232, 25, 25), TabIndex = 12 };
        AboutBtn.SetGlyph("info");
        Hints.SetToolTip(AboutBtn, "Version Info");

        CloseBtn = new BitBtn
        {
            Bounds = new Rectangle(490, 232, 63, 25), Text = "Quit", TabIndex = 13, Spacing = 5,
        };
        CloseBtn.SetGlyph("close");

        // 角の 8x8 panel はほぼ見えないが、double-click で debug mode を切り替える hidden control。
        DbgPanel = new Panel { Bounds = new Rectangle(9, 256, 8, 8), TabIndex = 15, TabStop = false };

        Controls.AddRange(new Control[]
        {
            Label1, Label2, Label3, WformLbl1, WformLbl2, InfoLabel, ProgBar1,
            Edit1, BitBtn1, PlayBtn1, Edit2, BitBtn2, PlayBtn2, Edit3, BitBtn3, OverwriteCheckBox, NormaliseCheckBox,
            StartBtn, SetBitBtn, HelpBtn, AboutBtn, CloseBtn, DbgPanel,
        });

        ResumeLayout(false);
        PerformLayout();
    }

    private static Label MakeLabel(string text, int left, int top) => new ThemedLabel()
    {
        Text = text,
        Location = new Point(left, top),
        AutoSize = true,
        BackColor = Color.Transparent,
    };

    private static BitBtn MakeGlyphButton(string glyph, int left, int top, int tabIndex)
    {
        var b = new BitBtn { Bounds = new Rectangle(left, top, 25, 25), TabIndex = tabIndex };
        b.SetGlyph(glyph);
        return b;
    }
}
