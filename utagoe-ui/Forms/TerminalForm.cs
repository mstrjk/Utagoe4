// Help... で開く terminal (v3 にない。元は PDF の help を開いていた)。
// 処理の段階、所要時間、残り時間、解析の判断、失敗したときの本当の stack trace を見られる。
// 処理中も開いたまま使えるよう modeless にし、同時には 1 つだけ開く。簡単な command も受け付ける。

using System.Diagnostics;
using System.Runtime.InteropServices;
using Utagoe.App;
using Utagoe.Native;

namespace Utagoe.Forms;

internal sealed class TerminalForm : XpTerminalWindow
{
    private static TerminalForm? s_open;

    /// 開いている terminal (別 window でも main window の中でも)。無ければ null。
    public static TerminalForm? Current => s_open is { IsDisposed: false } ? s_open : null;

    /// 開いていればそれを、無ければ表示せずに作って返す。main window に組み込むときに使う。
    public static TerminalForm Create(Form owner)
    {
        if (Current is { } t) return t;
        return s_open = new TerminalForm { Owner = owner, StartPosition = FormStartPosition.CenterParent };
    }

    public static void ShowFor(Form owner)
    {
        if (s_open is { IsDisposed: false })
        {
            if (s_open.WindowState == FormWindowState.Minimized) s_open.WindowState = FormWindowState.Normal;
            s_open.Activate();
            return;
        }
        s_open = new TerminalForm { Owner = owner, StartPosition = FormStartPosition.CenterParent };
        s_open.Show(owner);
    }

    private TerminalForm()
    {
        Text = "Utagoe Terminal";
        Heading = "Utagoe Terminal";
        Subheading = "Type 'help' for commands.";
        Terminal.Command += Run;
        FormClosed += (_, _) => s_open = null;
    }

    protected override string FooterText() =>
        $"Utagoe {Program.Version}  |  {CoreVersion()}  |  log: {(LogHub.LogFile is { } f ? "logs\\" + Path.GetFileName(f) : "(not written)")}  (type 'logs' to open the folder)";

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
