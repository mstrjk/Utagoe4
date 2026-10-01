// GPU が使えないことを知らせる小さな dialog (v3 にない)。
// 処理は CPU で問題なく続くので、警告ではなく情報として控えめに出し、「今後表示しない」を選べるようにする。
// 見た目は他の dialog に合わせて Tahoma 9pt と VCL 風の OK button を使う。

using Utagoe.App;
using Utagoe.Vcl;

namespace Utagoe.Forms;

internal sealed class GpuNoticeForm : Form
{
    private readonly CheckBox _dontShow;

    /// dialog を閉じた後に、「今後表示しない」が選ばれたかを返す。
    public bool DontShowAgain => _dontShow.Checked;

    public GpuNoticeForm(string reason)
    {
        AutoScaleDimensions = new SizeF(96F, 96F);
        AutoScaleMode = AutoScaleMode.Dpi;
        ClientSize = new Size(380, 150);
        Font = new Font("Tahoma", 9F);
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = false;
        MinimizeBox = false;
        ShowInTaskbar = false;
        StartPosition = FormStartPosition.CenterParent;
        Text = Messages.Title;
        Icon = VclGlyph.AppIcon;

        var icon = new PictureBox
        {
            Bounds = new Rectangle(14, 14, 32, 32),
            Image = SystemIcons.Information.ToBitmap(),
            SizeMode = PictureBoxSizeMode.CenterImage,
        };
        var text = new ThemedLabel
        {
            Text = Messages.GpuNotice(reason),
            Bounds = new Rectangle(58, 14, 310, 80),
        };
        _dontShow = new ThemedCheckBox
        {
            Text = Messages.DontShowAgain,
            Bounds = new Rectangle(58, 112, 200, 20),
            TabIndex = 1,
        };
        var ok = new BitBtn { Bounds = new Rectangle(292, 110, 75, 25), TabIndex = 0 };
        ok.SetKind(BitBtnKind.OK);
        AcceptButton = ok;
        CancelButton = ok;

        Controls.AddRange(new Control[] { ok, _dontShow, icon, text });
        VclScaling.Apply(this);
        App.Theme.Paint(this);
    }
}
