// 処理の記録を集める場所 (v3 にない)。core の callback (どの thread からでも来る) と UI 側の出来事を受け取り、
// 起動してからの履歴を保持して terminal に渡し、logs\ にも書く。
// terminal を後から開いても、それまでの記録を全部見られる。

using System.Collections.Concurrent;
using System.Text;

namespace Utagoe.App;

internal enum LogKind { Line = 0, Status = 1, StageBegin = 2, StageEnd = 3, Warning = 4, Error = 5, Detail = 6, Command = 7 }

internal sealed record LogEntry(DateTime Time, LogKind Kind, int Id, string Text);

internal static class LogHub
{
    private const int MaxEntries = 20000;

    private static readonly object Gate = new();
    private static readonly List<LogEntry> Entries = new();
    private static readonly Dictionary<int, string> StatusLines = new();
    private static readonly ConcurrentQueue<string> FileQueue = new();
    private static long _version;
    private static StreamWriter? _file;

    /// 履歴か進捗行が変わるたびに増える。表示側はこれを見て描き直す。
    public static long Version => Interlocked.Read(ref _version);

    public static string? LogFile { get; private set; }

    /// 処理中かどうか。terminal の RUNNING / IDLE 表示に使う。
    public static bool Busy { get; set; }

    /// logs\ に今回の記録 file を作る。古いものは新しい順に 20 個だけ残す。
    public static void OpenFile(string directory)
    {
        try
        {
            Directory.CreateDirectory(directory);
            foreach (var old in Directory.GetFiles(directory, "utagoe-*.log").OrderByDescending(f => f).Skip(19))
                File.Delete(old);
            LogFile = Path.Combine(directory, $"utagoe-{DateTime.Now:yyyyMMdd-HHmmss}.log");
            _file = new StreamWriter(LogFile, append: true, new UTF8Encoding(false)) { AutoFlush = true };
            lock (Gate)
                foreach (var e in Entries) _file.WriteLine(Format(e));
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            _file = null;
        }
    }

    public static void Add(LogKind kind, int id, string text)
    {
        var e = new LogEntry(DateTime.Now, kind, id, text ?? "");
        lock (Gate)
        {
            if (kind == LogKind.Status)
            {
                if (e.Text.Length == 0) StatusLines.Remove(id);
                else StatusLines[id] = e.Text;
            }
            else
            {
                Entries.Add(e);
                if (Entries.Count > MaxEntries) Entries.RemoveRange(0, Entries.Count - MaxEntries);
                try { _file?.WriteLine(Format(e)); } catch (IOException) { }
            }
        }
        Interlocked.Increment(ref _version);
    }

    public static void Line(string text) => Add(LogKind.Line, 0, text);
    public static void Detail(string text) => Add(LogKind.Detail, 0, text);
    public static void Warning(string text) => Add(LogKind.Warning, 0, text);

    /// .NET の例外は型と message、内側の例外、実際の stack trace をそのまま記録する。
    public static void Exception(string what, Exception ex) => Add(LogKind.Error, 0, $"{what}\n{ex}");

    public static void Clear()
    {
        lock (Gate) Entries.Clear();
        Interlocked.Increment(ref _version);
    }

    public static (List<LogEntry> entries, List<string> status) Snapshot()
    {
        lock (Gate)
            return (new List<LogEntry>(Entries), StatusLines.OrderBy(k => k.Key).Select(k => k.Value).ToList());
    }

    public static string Format(LogEntry e)
    {
        string prefix = e.Kind switch
        {
            LogKind.StageBegin => "> ",
            LogKind.StageEnd => e.Id < 0 ? "x " : "+ ",
            LogKind.Warning => "! ",
            LogKind.Error => "ERROR ",
            LogKind.Command => "UTAGOE> ",
            _ => "",
        };
        string text = e.Kind == LogKind.StageEnd ? StageEndText(e) : e.Text;
        return $"[{e.Time:HH:mm:ss.fff}] {prefix}{text}";
    }

    public static string StageEndText(LogEntry e)
    {
        double secs = (e.Id < 0 ? -1 - e.Id : e.Id) / 1000.0;
        return e.Id < 0 ? $"{e.Text} failed after {secs:0.00} s" : $"{e.Text} ({secs:0.00} s)";
    }
}
