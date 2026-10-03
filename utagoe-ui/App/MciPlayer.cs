using System.Runtime.InteropServices;
using System.Text;

namespace Utagoe.App;

internal sealed class MciPlayer : IDisposable
{
    private static int s_next;
    private readonly string _alias = "utagoe_play" + Interlocked.Increment(ref s_next);
    private string? _tempWav;
    private static readonly HashSet<MciPlayer> s_open = new();

    public event Action? Released;

    public static List<string> ReleaseWhere(Func<string, bool> match)
    {
        var released = new List<string>();
        foreach (var p in s_open.ToList())
            if (p.Path is { } path && match(path))
            {
                p.Close();
                p.Released?.Invoke();
                released.Add(path);
            }
        return released;
    }

    public bool IsOpen { get; private set; }
    public string? Path { get; private set; }

    public bool Load(string path)
    {
        Close();
        Path = path;
        IsOpen = Open(path) || OpenConverted(path);
        if (IsOpen) s_open.Add(this);
        return IsOpen;
    }

    public int Length => IsOpen ? Status("length") : 0;
    public int Position => IsOpen ? Status("position") : 0;
    public string Mode => IsOpen ? StatusText("mode") : "";
    public bool Playing => Mode == "playing";

    public void Play()
    {
        if (IsOpen) Mci($"play {_alias}");
    }

    public void TogglePause()
    {
        if (!IsOpen) return;
        string mode = Mode;
        if (mode == "paused") Mci($"resume {_alias}");
        else if (mode == "playing") Mci($"pause {_alias}");
    }

    public void Stop()
    {
        if (IsOpen) Mci($"stop {_alias}");
    }

    public void Rewind() => Seek(0);

    public void Seek(int ms)
    {
        if (!IsOpen) return;
        bool playing = Playing;
        Mci(ms <= 0 ? $"seek {_alias} to start" : $"seek {_alias} to {ms}");
        if (playing) Mci($"play {_alias}");
    }

    public void Close()
    {
        if (IsOpen) Mci($"close {_alias}");
        s_open.Remove(this);
        IsOpen = false;
        Path = null;
        if (_tempWav != null)
        {
            try { File.Delete(_tempWav); } catch (IOException) { } catch (UnauthorizedAccessException) { }
            _tempWav = null;
        }
    }

    public void Dispose() => Close();

    private bool Open(string path)
    {
        if (Mci($"open \"{path}\" type waveaudio alias {_alias}"))
            return Mci($"set {_alias} time format milliseconds");
        var sb = new StringBuilder(1024);
        if (GetShortPathNameW(path, sb, sb.Capacity) > 0 && Mci($"open \"{sb}\" type waveaudio alias {_alias}"))
            return Mci($"set {_alias} time format milliseconds");
        return false;
    }

    private bool OpenConverted(string path)
    {
        string dir = AppPaths.Cache;
        try { Directory.CreateDirectory(dir); }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { dir = System.IO.Path.GetTempPath(); }
        _tempWav = System.IO.Path.Combine(dir, $"utagoe-play-{Environment.ProcessId}-{Guid.NewGuid():N}.wav");
        var cursor = Cursor.Current;
        Cursor.Current = Cursors.WaitCursor;
        try
        {
            if (!Native.Core.TranscodeFile(path, _tempWav, Native.OutputFormat.Wav, Native.OutputDepth.Int16, 0, out _))
                return false;
            return Open(_tempWav);
        }
        finally
        {
            Cursor.Current = cursor;
        }
    }

    private int Status(string item) => int.TryParse(StatusText(item), out int v) ? v : 0;

    private string StatusText(string item)
    {
        var sb = new StringBuilder(128);
        return mciSendStringW($"status {_alias} {item}", sb, sb.Capacity, IntPtr.Zero) == 0 ? sb.ToString() : "";
    }

    private static bool Mci(string command) => mciSendStringW(command, null, 0, IntPtr.Zero) == 0;

    [DllImport("winmm.dll", CharSet = CharSet.Unicode)]
    private static extern int mciSendStringW(string command, StringBuilder? returnString, int returnLength, IntPtr callback);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetShortPathNameW(string longPath, StringBuilder shortPath, int length);
}
