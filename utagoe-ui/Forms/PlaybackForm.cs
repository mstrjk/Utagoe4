// 元実装の playback dialog を再現する。
// layout は TPLAYFORM1 resource から復元。title bar button はなく OK だけで閉じる。
// 実機では dialog open と同時に再生開始。seek bar は ms 単位で、track の 1/10 ごとに tick。
// TMediaPlayer は Windows MCI の薄い wrapper なので、同じ engine を使うため直接 MCI を叩く。

using Utagoe.Vcl;

namespace Utagoe.Forms;

internal sealed class PlaybackForm : Form
{
    private const int BtnPlay = 0, BtnPause = 1, BtnStop = 2, BtnPrev = 3;

    private readonly string _path;
    private readonly MediaPlayerStrip _player;
    private readonly VclTrackBar _seek;
    private readonly BitBtn _ok;
    private readonly System.Windows.Forms.Timer _timer;
    private readonly App.MciPlayer _mci = new();
    private bool _updatingSeek;

    public PlaybackForm(string path)
    {
        _path = path;

        AutoScaleDimensions = new SizeF(96F, 96F);
        AutoScaleMode = AutoScaleMode.Dpi;
        ClientSize = new Size(250, 133);
        Font = new Font("Tahoma", 9F);
        FormBorderStyle = FormBorderStyle.FixedSingle;
        ControlBox = false;
        ShowInTaskbar = false;
        StartPosition = FormStartPosition.CenterScreen;
        Text = Utagoe.App.L.T("Playback") + " - " + Path.GetFileName(path);
        Icon = VclGlyph.AppIcon;

        BevelPainter.Attach(this,
            new Bevel(0, 8, 249, 9, BevelShape.TopLine),
            new Bevel(0, 88, 249, 9, BevelShape.BottomLine));

        // player の Width=-3 は自動 size。実機では scaling 後 113x35。
        _player = new MediaPlayerStrip("MPPLAY", "MPPAUSE", "MPSTOP", "MPPREV")
        {
            Bounds = new Rectangle(68, 16, 97, 30), TabIndex = 0,
        };
        _seek = new VclTrackBar { Bounds = new Rectangle(8, 56, 233, 33), TabIndex = 1 };
        _ok = new BitBtn { Bounds = new Rectangle(88, 104, 75, 25), TabIndex = 2 };
        _ok.SetKind(BitBtnKind.OK);
        AcceptButton = _ok;
        _ok.DialogResult = DialogResult.None;
        _ok.Click += (_, _) => Close();

        _timer = new System.Windows.Forms.Timer { Interval = 400 };

        Controls.AddRange(new Control[] { _player, _seek, _ok });

        _player.ButtonClick += OnPlayerButton;
        _seek.ValueChanged += (_, _) => { if (!_updatingSeek) _mci.Seek(_seek.Value); };
        _timer.Tick += (_, _) => SyncSeek();

        VclScaling.Apply(this);
        App.Theme.Paint(this);
    }

    protected override void OnShown(EventArgs e)
    {
        base.OnShown(e);
        if (!_mci.Load(_path))
        {
            _player.Enabled = false;
            _seek.Enabled = false;
            return;
        }
        int length = _mci.Length;
        _seek.Maximum = Math.Max(1, length);
        _seek.TickFrequency = Math.Max(1, length / 10);
        _seek.LargeChange = Math.Max(1, length / 10);
        _timer.Start();
        _mci.Play();
    }

    private void OnPlayerButton(int button)
    {
        if (!_mci.IsOpen && button == BtnPlay && _mci.Load(_path))
        {
            _seek.Maximum = Math.Max(1, _mci.Length);
            _timer.Start();
        }
        switch (button)
        {
            case BtnPlay: _mci.Play(); break;
            case BtnPause: _mci.TogglePause(); break;
            case BtnStop: _mci.Stop(); break;
            case BtnPrev: _mci.Rewind(); break;
        }
        SyncSeek();
    }

    private void SyncSeek()
    {
        if (!_mci.IsOpen) return;
        _updatingSeek = true;
        _seek.Value = Math.Clamp(_mci.Position, _seek.Minimum, _seek.Maximum);
        _updatingSeek = false;
    }

    protected override void OnFormClosed(FormClosedEventArgs e)
    {
        _timer.Stop();
        _timer.Dispose();
        _mci.Dispose();
        base.OnFormClosed(e);
    }
}
