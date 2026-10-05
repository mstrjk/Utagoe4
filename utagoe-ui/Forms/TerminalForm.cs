// Help... で開く terminal (v3 にない。元は PDF の help を開いていた)。
// 処理の段階、所要時間、残り時間、解析の判断、失敗したときの本当の stack trace を見られる。
// 処理中も開いたまま使えるよう modeless にし、同時には 1 つだけ開く。簡単な command も受け付ける。

using System.Diagnostics;
using System.Runtime.InteropServices;
using Utagoe.App;
using Utagoe.Ui;
using Utagoe.Native;

namespace Utagoe.Forms;

internal sealed class TerminalForm : XpTerminalWindow
{
    private static readonly WindowSlot<TerminalForm> Slot = new();

    public static TerminalForm? Current => Slot.Current;

    public static TerminalForm Create(Form owner) => Current ?? Slot.Hold(New(owner));

    public static TerminalForm Docked() => new();

    public static void ShowFor(Form owner) => Slot.Show(owner, () => New(owner));

    private static TerminalForm New(Form owner) => new() { Owner = owner, StartPosition = FormStartPosition.CenterParent };

    private TerminalForm() : base(Template(() => L.T("Utagoe Terminal"), Placement.CenterParent))
    {
        Heading = "Utagoe Terminal";
        Subheading = "Type 'help' for commands.";
        Terminal.Command += Run;
    }

    protected override string FooterText() =>
        $"Utagoe {Program.Version}  |  {CoreVersion()}  |  " +
        L.F("log: {0}  (type 'logs' to open the folder)", LogHub.LogFile is { } f ? "logs\\" + Path.GetFileName(f) : L.T("(not written)"));

    private static string CoreVersion()
    {
        try { return Core.Version; }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException) { return "core not loaded"; }
    }

    private void Run(string line)
    {
        string[] parts = line.Split(' ', 2, StringSplitOptions.RemoveEmptyEntries);
        string cmd = parts[0].ToLowerInvariant();
        switch (cmd)
        {
            case "help" or "?":
                foreach (string l in Messages.TerminalHelp) LogHub.Line(l);
                break;
            case "guide":
                foreach (string l in Messages.TerminalGuide) LogHub.Line(l);
                break;
            case "info" or "version" or "ver":
                foreach (string l in SystemInfo()) LogHub.Detail(l);
                break;
            case "gpu":
                var gpu = Core.Gpu;
                LogHub.Detail(gpu.Available ? $"GPU: {gpu.Adapter} (Direct3D 11)" : $"GPU: not available ({gpu.Reason})");
                break;
            case "status":
                LogHub.Detail(LogHub.Busy ? "processing is running; progress lines are shown below the log" : "idle");
                break;
            case "logs":
                TerminalView.OpenLogs();
                break;
            case "crash":
                ShowLastCrash();
                break;
            case "copy":
                TerminalView.CopyAll();
                LogHub.Detail("copied the whole log to the clipboard");
                break;
            case "clear" or "cls":
                LogHub.Clear();
                break;
            case "exit" or "close":
                Close();
                break;
            default:
                LogHub.Warning($"unknown command '{cmd}'. Type 'help' for the list.");
                break;
        }
    }

    public static IEnumerable<string> SystemInfo()
    {
        yield return $"Utagoe {Program.Version} ({(AppPaths.IsPortable ? "portable" : "installed")}), {CoreVersion()}";
        yield return $"Windows {Environment.OSVersion.Version}, {RuntimeInformation.FrameworkDescription}, {Environment.ProcessorCount} logical CPUs";
        yield return $"program  {AppPaths.Root}";
        yield return $"settings {AppSettings.IniPath}";
        yield return $"logs     {AppPaths.Logs}";
        foreach (string l in Core.BuildInfo.Split('\n', StringSplitOptions.RemoveEmptyEntries))
            yield return l;
    }

    private static void ShowLastCrash()
    {
        string? last = Directory.Exists(AppPaths.Logs)
            ? Directory.GetFiles(AppPaths.Logs, "crash-*.txt").OrderByDescending(f => f).FirstOrDefault()
            : null;
        if (last == null)
        {
            LogHub.Detail("no crash reports. Good.");
            return;
        }
        LogHub.Add(LogKind.Error, 0, $"{Path.GetFileName(last)}\n{File.ReadAllText(last)}");
    }
}
