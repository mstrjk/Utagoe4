using Utagoe.App;
using Utagoe.Ui;

namespace Utagoe.Forms;

internal sealed class FaqForm : UiWindow
{
    private sealed record Link(string Text, string Url, string Detail = "");
    private sealed record Entry(Func<string> Question, Func<string> Answer, Link[] Links, Func<(string Text, Link Link)>? Note);

    private static readonly Link Unwa = new("Unwa - Big Beta 5e (Mel-RoFormer)", "https://huggingface.co/pcunwa/Mel-Band-Roformer-big/tree/main");

    private static readonly Entry[] Entries =
    {
        new(() => L.T("I processed my audio already, but it didn't make the acapella clean."),
            () => L.T("Subtraction only removes what the two files share exactly. Official instrumentals are mastered on their own, without the vocal. The mastering limiter reacts to everything it hears, so with the vocal in the song it turns the music down a little more, and a little differently, on every loud hit. The instrumental never had that, so no subtraction can remove it. It stays in the acapella, usually as a crunch or thump on the kick drum in the loudest parts. Utagoe reduces what it can measure, but a cleaner trained on this kind of leftover can tidy up the result afterwards:"),
            new[]
            {
                new Link("Gillian - CleanInvert (BS-RoFormer)", "https://huggingface.co/gilliaan/Stem-Separation-Models/tree/main/CleanInvert/BandSplitRoformer"),
                new Link("Becruily - Invert Clean (Mel-RoFormer)", "https://huggingface.co/becruily/invert-clean/tree/main"),
            },
            () => (L.F("Becruily recommends pre-processing with {0}.", Unwa.Text), Unwa)),
        new(() => L.T("Where can I learn more, or find other tools for separating songs?"),
            () => L.T("Utagoe is one tool among many. Ultimate Vocal Remover (UVR) is a free app that runs separation models, including the cleaners above. deton24's guide is the community's big reference for separating instrumentals, vocals and other stems, and for mixing and mastering the results:"),
            new[]
            {
                new Link("Anjok07 & aufr33 - Ultimate Vocal Remover (UVR)", "https://github.com/anjok07/ultimatevocalremovergui",
                         "Discord: Anjok07 (.anjok, 770528998484082689), aufr33 (767947337078800404)"),
                new Link("deton24 - Instrumental, vocal & other stems separation & mix/master guide",
                         "https://docs.google.com/document/d/17fjNvJzj8ZGSer7c7OFe_CNfUKbAxEh_OBv94ZdRG5c",
                         "Discord: deton24 (408356309310636053)"),
            },
            null),
    };

    private static readonly WindowSlot<FaqForm> Slot = new();
    private static readonly WindowSpec Definition = new()
    {
        ClientSize = new Size(460, 440),
        Title = () => L.T("Frequently Asked Questions"),
    };

    private readonly Panel _body;
    private readonly BitBtn _ok;

    public static void ShowFor(IWin32Window owner) => Slot.Show(owner, () => new FaqForm());

    private FaqForm() : base(Definition)
    {
        _body = Element.ScrollArea(1);
        _ok = Element.Close(new Rectangle(0, 0, 75, 25), 0);
        _ok.Click += (_, _) => Close();
        AcceptButton = _ok;
        CancelButton = _ok;

        Controls.AddRange(new Control[] { _body, _ok });
        UiText.OnChange(this, Fill);
        BevelPainter.Attach(this, new Bevel(8, 8, 444, 310));

        VclScaling.Apply(this);
        Fill();
    }

    private void Fill()
    {
        if (IsDisposed) return;
        _body.SuspendLayout();
        foreach (Control c in _body.Controls.Cast<Control>().ToArray()) c.Dispose();
        _body.Controls.Clear();
        foreach (var e in Entries)
        {
            _body.Controls.Add(Element.Para(L.F("Q: {0}", e.Question()), bold: Font, tag: "q"));
            _body.Controls.Add(Element.Para(L.F("A: {0}", e.Answer())));
            foreach (var l in e.Links)
                _body.Controls.Add(Element.Link("• " + l.Text + (l.Detail.Length > 0 ? "\n" + l.Detail : ""), l.Url, 2, l.Text.Length));
            if (e.Note != null)
            {
                var (text, link) = e.Note();
                int at = text.IndexOf(link.Text, StringComparison.Ordinal);
                _body.Controls.Add(Element.Link(text, link.Url, Math.Max(0, at), at < 0 ? 0 : link.Text.Length));
            }
        }
        _body.ResumeLayout();
        Relayout();
    }

    private void Relayout()
    {
        int frame = S(8), pad = S(16);
        int bevelBottom = ClientSize.Height - _ok.Height - S(17);
        BevelPainter.Replace(this, new Bevel(frame, frame, ClientSize.Width - frame * 2, bevelBottom - frame));
        _ok.Location = new Point((ClientSize.Width - _ok.Width) / 2, bevelBottom + S(10));
        _body.SetBounds(pad, pad, ClientSize.Width - pad * 2, bevelBottom - pad - S(6));
        int width = _body.ClientSize.Width - SystemInformation.VerticalScrollBarWidth - S(4);
        int y = _body.AutoScrollPosition.Y;
        Control? prev = null;
        foreach (Control c in _body.Controls)
        {
            c.MaximumSize = new Size(Math.Max(S(40), width), 0);
            if (prev != null) y += (c.Tag as string == "q" ? S(14) : c is LinkLabel && prev is LinkLabel ? S(2) : S(6));
            c.Location = new Point(c is LinkLabel && c.Text.StartsWith("• ") ? S(8) : 0, y);
            y += c.Height;
            prev = c;
        }
    }
}
