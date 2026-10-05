using Utagoe.App;
using Utagoe.Ui;

namespace Utagoe.Forms;

internal sealed class MessageForm : UiWindow
{
    public static DialogResult Show(IWin32Window? owner, string text, string caption,
                                    MessageBoxButtons buttons = MessageBoxButtons.OK, MessageBoxIcon icon = MessageBoxIcon.None,
                                    string? heading = null, string? confirm = null)
    {
        using var f = new MessageForm(text, caption, buttons, icon, owner != null, heading, confirm);
        return owner != null ? f.ShowDialog(owner) : f.ShowDialog();
    }

    public static DialogResult Show(string text, string caption,
                                    MessageBoxButtons buttons = MessageBoxButtons.OK, MessageBoxIcon icon = MessageBoxIcon.None) =>
        Show(null, text, caption, buttons, icon);

    private static WindowSpec Definition(string caption, bool owned) => new()
    {
        Border = Border.Dialog,
        Scaling = Scaling.None,
        Taskbar = !owned,
        Placement = owned ? Placement.CenterParent : Placement.CenterScreen,
        Title = () => caption,
    };

    private MessageForm(string text, string caption, MessageBoxButtons buttons, MessageBoxIcon icon, bool owned,
                        string? heading, string? confirm) : base(Definition(caption, owned))
    {
        Icon? sys = icon switch
        {
            MessageBoxIcon.Error => SystemIcons.Error,
            MessageBoxIcon.Warning => SystemIcons.Warning,
            MessageBoxIcon.Information => SystemIcons.Information,
            MessageBoxIcon.Question => SystemIcons.Question,
            _ => null,
        };
        int left = S(16);
        int iconBottom = 0;
        if (sys != null)
        {
            var pic = Element.Picture(new Icon(sys, S(32), S(32)).ToBitmap(), new Rectangle(S(16), S(16), S(32), S(32)), PictureBoxSizeMode.Zoom);
            Controls.Add(pic);
            left = S(60);
            iconBottom = pic.Bottom;
        }

        var flags = TextFormatFlags.WordBreak | TextFormatFlags.TextBoxControl | TextFormatFlags.NoPrefix;
        int maxWidth = S(440);
        Size need = TextRenderer.MeasureText(text, Font, new Size(maxWidth, int.MaxValue), flags);
        int textWidth = Math.Clamp(need.Width + S(4), S(220), maxWidth);
        need = TextRenderer.MeasureText(text, Font, new Size(textWidth, int.MaxValue), flags);
        int textHeight = Math.Min(need.Height + S(2), S(340));
        int textTop = sys != null ? S(16) + Math.Max(0, (S(32) - Math.Min(textHeight, S(32))) / 2) : S(16);
        if (heading != null)
        {
            var bold = new Font(Font, FontStyle.Bold);
            int hh = TextRenderer.MeasureText(heading, bold, new Size(textWidth, int.MaxValue), flags).Height;
            var head = Element.Text(() => heading, new Rectangle(left, S(16), textWidth, hh + S(2)), transparent: true);
            head.Font = bold;
            Controls.Add(head);
            textTop = S(16) + hh + S(8);
        }
        var label = Element.Text(() => text, new Rectangle(left, textTop, textWidth, textHeight), transparent: true);
        Controls.Add(label);

        var specs = buttons switch
        {
            MessageBoxButtons.OKCancel => new[] { (BitBtnKind.OK, "", DialogResult.OK), (BitBtnKind.Cancel, "", DialogResult.Cancel) },
            MessageBoxButtons.YesNo => new[] { (BitBtnKind.Custom, "Yes", DialogResult.Yes), (BitBtnKind.Custom, "No", DialogResult.No) },
            MessageBoxButtons.YesNoCancel => new[] { (BitBtnKind.Custom, "Yes", DialogResult.Yes), (BitBtnKind.Custom, "No", DialogResult.No),
                                                     (BitBtnKind.Cancel, "", DialogResult.Cancel) },
            _ => new[] { (BitBtnKind.OK, "", DialogResult.OK) },
        };
        int bw = S(84), bh = S(26), gap = S(8);
        int rowTop = Math.Max(label.Bottom, iconBottom) + S(18);
        int rowWidth = specs.Length * bw + (specs.Length - 1) * gap;
        int width = Math.Max(left + textWidth + S(18), rowWidth + S(32));
        var made = new List<BitBtn>();
        int x = width - S(16) - rowWidth;
        foreach (var (kind, key, result) in specs)
        {
            var bounds = new Rectangle(x, rowTop, bw, bh);
            var b = kind switch
            {
                BitBtnKind.OK => Element.Ok(bounds, made.Count),
                BitBtnKind.Cancel => Element.Cancel(bounds, made.Count),
                _ => Element.Button(key, bounds, made.Count),
            };
            if (confirm != null && made.Count == 0) UiText.Bind(b, () => confirm);
            b.DialogResult = result;
            Controls.Add(b);
            made.Add(b);
            x += bw + gap;
        }
        AcceptButton = made[0];
        CancelButton = made[^1];
        ClientSize = new Size(width, rowTop + bh + S(14));
        Ready();
    }
}
