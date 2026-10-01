// 初回起動の導入画面 (v3 にない)。Help の terminal と同じ XP 風の window に、導入の様子を 1 行ずつ出す。
// 成功したら少し待って閉じ、導入先の Utagoe を起動する。失敗したら原因と stack trace を残して開いたままにする。

using Utagoe.App;

namespace Utagoe.Forms;

internal sealed class InstallForm : XpTerminalWindow
{
    private bool _failed;

    public string? InstalledExe { get; private set; }

    public InstallForm()
    {
        Text = L.T("Utagoe Setup");
        Heading = "Setting up Utagoe";
        Subheading = "One-time setup, no admin rights needed.";
        ClientSize = new Size(640, 340);
        StartPosition = FormStartPosition.CenterScreen;
        Terminal.AcceptsInput = false;
        Shown += async (_, _) => await RunAsync();
    }

    protected override string FooterText() =>
        _failed ? L.T("Setup did not finish. The details are above; close this window to quit.")
                : $"Utagoe {Program.Version}  |  {AppPaths.InstallRoot}";

    private async Task RunAsync()
    {
        LogHub.Add(LogKind.StageBegin, 0, $"install Utagoe {Program.Version}");
        var clock = System.Diagnostics.Stopwatch.StartNew();
        try
        {
            InstalledExe = await Task.Run(() => Installer.Install(LogHub.Detail));
            LogHub.Add(LogKind.StageEnd, (int)clock.ElapsedMilliseconds, $"install Utagoe {Program.Version}");
            LogHub.Line(L.T("Utagoe is installed. Look for it in the Start menu. Starting it now..."));
            await Task.Delay(1500);
            DialogResult = DialogResult.OK;
            Close();
        }
        catch (Exception ex)
        {
            _failed = true;
            LogHub.Add(LogKind.StageEnd, -1 - (int)clock.ElapsedMilliseconds, $"install Utagoe {Program.Version}");
            LogHub.Exception(ex is InvalidOperationException ? ex.Message : "setup failed", ex);
        }
    }
}
