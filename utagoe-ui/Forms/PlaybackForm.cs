// 元実装の playback dialog を再現する。
// layout は TPLAYFORM1 resource から復元。title bar button はなく OK だけで閉じる。
// 実機では dialog open と同時に再生開始。seek bar は ms 単位で、track の 1/10 ごとに tick。
// TMediaPlayer は Windows MCI の薄い wrapper なので、同じ engine を使うため直接 MCI を叩く。

using System.Runtime.InteropServices;
using System.Text;
using Utagoe.Vcl;

namespace Utagoe.Forms;

internal sealed class PlaybackForm : Form
{
    // v4 では main window を止めずに何枚でも開けるので、MCI の alias は window ごとに分ける。
    private static int s_next;
    private readonly string Alias = "utagoe_play" + Interlocked.Increment(ref s_next);
    private const int BtnPlay = 0, BtnPause = 1, BtnStop = 2, BtnPrev = 3;

    private readonly string _path;
    private readonly MediaPlayerStrip _player;
    private readonly VclTrackBar _seek;
    private readonly BitBtn _ok;
    private readonly System.Windows.Forms.Timer _timer;
    private bool _open;
    private bool _updatingSeek;
    private string? _tempWav;   // MCI が直接開けない形式を再生するための一時 WAV

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
        _seek.ValueChanged += (_, _) => { if (!_updatingSeek) SeekTo(_seek.Value); };
        _timer.Tick += (_, _) => SyncSeek();

        VclScaling.Apply(this);
        App.Theme.Paint(this);
    }

    protected override void OnShown(EventArgs e)
    {
        base.OnShown(e);
        _open = Open(_path);
        if (!_open) _open = OpenConverted();
        if (!_open)
        {
            _player.Enabled = false;
            _seek.Enabled = false;
            return;
        }

        int length = Status("length");
        _seek.Maximum = Math.Max(1, length);
        _seek.TickFrequency = Math.Max(1, length / 10);
        _seek.LargeChange = Math.Max(1, length / 10);
        _timer.Start();
        Mci($"play {Alias}");
    }

    /// MCI は長い path をそのまま渡すと失敗することがあるので、その場合だけ 8.3 short path で再試行する。
    private bool Open(string path)
    {
        if (Mci($"open \"{path}\" type waveaudio alias {Alias}"))
            return Mci($"set {Alias} time format milliseconds");

        var sb = new StringBuilder(1024);
        if (GetShortPathNameW(path, sb, sb.Capacity) > 0 &&
            Mci($"open \"{sb}\" type waveaudio alias {Alias}"))
            return Mci($"set {Alias} time format milliseconds");

        return false;
    }

    private void OnPlayerButton(int button)
    {
        if (!_open) return;
        switch (button)
        {
            case BtnPlay:
                Mci($"play {Alias}");
                break;
            case BtnPause:
                // Pause は toggle 動作。再生中なら pause、pause 中なら resume。
                string mode = StatusText("mode");
                if (mode == "paused") Mci($"resume {Alias}");
                else if (mode == "playing") Mci($"pause {Alias}");
                break;
            case BtnStop:
                Mci($"stop {Alias}");
                break;
            case BtnPrev:
                bool playing = StatusText("mode") == "playing";
                Mci($"seek {Alias} to start");
                if (playing) Mci($"play {Alias}");
                break;
        }
        SyncSeek();
    }

    private void SeekTo(int ms)
    {
        if (!_open) return;
        bool playing = StatusText("mode") == "playing";
        Mci($"seek {Alias} to {ms}");
        if (playing) Mci($"play {Alias}");
    }

    private void SyncSeek()
    {
        if (!_open) return;
        _updatingSeek = true;
        _seek.Value = Math.Clamp(Status("position"), _seek.Minimum, _seek.Maximum);
        _updatingSeek = false;
    }

    /// MP3 / FLAC / 24-bit WAV など MCI の waveaudio が扱えない形式は、core で 16-bit WAV に変換して再生する。
    private bool OpenConverted()
    {
        // 再生用の変換は導入先の cache\ に置く。作れなければ Windows の一時 folder。
        string dir = App.AppPaths.Cache;
        try { Directory.CreateDirectory(dir); } catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { dir = Path.GetTempPath(); }
        _tempWav = Path.Combine(dir, $"utagoe-play-{Environment.ProcessId}-{Guid.NewGuid():N}.wav");
        Cursor = Cursors.WaitCursor;
        try
        {
            if (!Native.Core.TranscodeFile(_path, _tempWav, Native.OutputFormat.Wav, Native.OutputDepth.Int16, 0, out _))
                return false;
            return Open(_tempWav);
        }
        finally
        {
            Cursor = Cursors.Default;
        }
    }

    protected override void OnFormClosed(FormClosedEventArgs e)
    {
        _timer.Stop();
        _timer.Dispose();
        if (_open) Mci($"close {Alias}");
        if (_tempWav != null)
        {
            try { File.Delete(_tempWav); } catch (IOException) { }
        }
        base.OnFormClosed(e);
    }

    private int Status(string item) =>
        int.TryParse(StatusText(item), out int v) ? v : 0;

    private string StatusText(string item)
    {
        var sb = new StringBuilder(128);
        return mciSendStringW($"status {Alias} {item}", sb, sb.Capacity, IntPtr.Zero) == 0
            ? sb.ToString() : "";
    }

    private static bool Mci(string command) =>
        mciSendStringW(command, null, 0, IntPtr.Zero) == 0;

    [DllImport("winmm.dll", CharSet = CharSet.Unicode)]
    private static extern int mciSendStringW(string command, StringBuilder? returnString,
                                             int returnLength, IntPtr callback);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetShortPathNameW(string longPath, StringBuilder shortPath, int length);
}
