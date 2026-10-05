// 元実装の playback dialog を再現する。
// layout は TPLAYFORM1 resource から復元。title bar button はなく OK だけで閉じる。
// 実機では dialog open と同時に再生開始。seek bar は ms 単位で、track の 1/10 ごとに tick。

using Utagoe.App;
using Utagoe.Ui;

namespace Utagoe.Forms;

internal sealed class PlaybackForm : UiWindow
{
    private static WindowSpec Definition(string path) => new()
    {
        ClientSize = new Size(250, 133),
        Buttons = TitleButtons.None,
        Placement = Placement.CenterScreen,
        Title = () => L.T("Playback") + " - " + Path.GetFileName(path),
    };

    private readonly string _path;
    private readonly MediaPlayerStrip _player;
    private readonly VclTrackBar _seek;
    private readonly PlayerLink _link;

    public PlaybackForm(string path) : base(Definition(path))
    {
        _path = path;

        BevelPainter.Attach(this,
            new Bevel(0, 8, 249, 9, BevelShape.TopLine),
            new Bevel(0, 88, 249, 9, BevelShape.BottomLine));

        _player = Element.Player(new Rectangle(76, 16, 97, 30), 0);
        _seek = Element.Seek(new Rectangle(8, 56, 234, 33), 1);
        var ok = Element.Ok(new Rectangle(87, 104, 75, 25), 2, closes: true);
        AcceptButton = ok;
        ok.Click += (_, _) => Close();
        _link = new PlayerLink(_seek, 400);

        Controls.AddRange(new Control[] { _player, _seek, ok });
        _player.ButtonClick += OnPlayerButton;
        Ready();
    }

    protected override void OnShown(EventArgs e)
    {
        base.OnShown(e);
        if (!_link.Mci.Load(_path))
        {
            _player.Enabled = false;
            _seek.Enabled = false;
            return;
        }
        int length = _link.Mci.Length;
        _seek.Maximum = Math.Max(1, length);
        _seek.TickFrequency = Math.Max(1, length / 10);
        _seek.LargeChange = Math.Max(1, length / 10);
        _link.StartTimer();
        _link.Mci.Play();
    }

    private void OnPlayerButton(int button)
    {
        if (!_link.Mci.IsOpen && button == PlayerLink.Play && _link.Mci.Load(_path))
        {
            _seek.Maximum = Math.Max(1, _link.Mci.Length);
            _link.StartTimer();
        }
        _link.Press(button);
        _link.Sync();
    }

    protected override void OnFormClosed(FormClosedEventArgs e)
    {
        _link.Dispose();
        base.OnFormClosed(e);
    }
}
