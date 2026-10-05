// GPU が使えないことを知らせる小さな dialog (v3 にない)。
// 処理は CPU で問題なく続くので、警告ではなく情報として控えめに出し、「今後表示しない」を選べるようにする。

using Utagoe.App;
using Utagoe.Ui;

namespace Utagoe.Forms;

internal sealed class GpuNoticeForm : UiWindow
{
    private static readonly WindowSpec Definition = new()
    {
        ClientSize = new Size(380, 150),
        Border = Border.Dialog,
        Title = () => Messages.Title,
    };

    private readonly CheckBox _dontShow;

    public bool DontShowAgain => _dontShow.Checked;

    public GpuNoticeForm(string reason) : base(Definition)
    {
        var icon = Element.Picture(SystemIcons.Information.ToBitmap(), new Rectangle(14, 14, 32, 32), PictureBoxSizeMode.CenterImage);
        icon.BackColor = default;
        var text = Element.Text(() => Messages.GpuNotice(reason), new Rectangle(58, 14, 310, 80));
        text.UseMnemonic = true;
        _dontShow = Element.Check(() => Messages.DontShowAgain, new Rectangle(58, 112, 200, 20), 1);
        var ok = Element.Ok(new Rectangle(292, 110, 75, 25), 0);
        AcceptButton = ok;
        CancelButton = ok;

        Controls.AddRange(new Control[] { ok, _dontShow, icon, text });
        Ready();
    }
}
