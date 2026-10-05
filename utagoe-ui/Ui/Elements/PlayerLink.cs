using Utagoe.App;

namespace Utagoe.Ui;

internal sealed class PlayerLink : IDisposable
{
    public const int Play = 0, Pause = 1, Stop = 2, Previous = 3;

    public readonly MciPlayer Mci = new();
    private readonly VclTrackBar _seek;
    private readonly System.Windows.Forms.Timer _timer;
    private bool _updating;

    public event Action? Synced;

    public PlayerLink(VclTrackBar seek, int interval)
    {
        _seek = seek;
        _timer = new System.Windows.Forms.Timer { Interval = interval };
        _timer.Tick += (_, _) => Sync();
        seek.ValueChanged += (_, _) => { if (!_updating) Mci.Seek(seek.Value); };
    }

    public void Quietly(Action change)
    {
        _updating = true;
        try { change(); }
        finally { _updating = false; }
    }

    public void StartTimer() => _timer.Start();
    public void StopTimer() => _timer.Stop();

    public void Press(int button)
    {
        switch (button)
        {
            case Play: Mci.Play(); break;
            case Pause: Mci.TogglePause(); break;
            case Stop: Mci.Stop(); break;
            case Previous: Mci.Rewind(); break;
        }
    }

    public void Sync()
    {
        if (Mci.IsOpen) Quietly(() => _seek.Value = Math.Clamp(Mci.Position, _seek.Minimum, _seek.Maximum));
        Synced?.Invoke();
    }

    public void Dispose()
    {
        _timer.Stop();
        _timer.Dispose();
        Mci.Dispose();
    }
}
