// 起動の流れ。
//   --uninstall                  「アプリと機能」からの削除
//   導入先以外から起動 (download など) -> 導入して、導入先の copy を起動する (v3 にない)
//   導入先の copy                 -> 欠けた部品を埋め直してから起動
//   exe の横に lib\ がある開発 build -> その場で起動 (portable)
// 処理されなかった .NET の例外は、本当の stack trace を terminal と logs\ に残す。

using System.Diagnostics;
using System.Reflection;
using Utagoe.App;
using Utagoe.Forms;
using Utagoe.Native;

namespace Utagoe;

internal static class Program
{
    public static string Version =>
        typeof(Program).Assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion.Split('+')[0]
        ?? Installer.CurrentVersion.ToString(3);

    [STAThread]
    private static int Main(string[] args)
    {
        ApplicationConfiguration.Initialize();
        L.Set(null);
        Application.SetUnhandledExceptionMode(UnhandledExceptionMode.CatchException);
        Application.ThreadException += (_, e) => Unhandled("unhandled error in the window", e.Exception, fatal: false);
        AppDomain.CurrentDomain.UnhandledException += (_, e) =>
            Unhandled("unhandled error", e.ExceptionObject as Exception ?? new Exception(e.ExceptionObject?.ToString()), fatal: true);

        if (args.Contains("--uninstall", StringComparer.OrdinalIgnoreCase))
            return Installer.UninstallInteractive();

        // download した exe などから起動された: 導入するか、導入済みの copy に任せる。
        if (!AppPaths.IsPortable && !AppPaths.IsInstalledCopy && Installer.HasPayload)
            return InstallOrHandOff(args);

        if (AppPaths.IsInstalledCopy)
        {
            try
            {
                Installer.EnsurePayload(LogHub.Detail);
            }
            catch (Exception ex)
            {
                LogHub.Exception("Utagoe could not repair its components", ex);
                Utagoe.Forms.MessageForm.Show($"Utagoe could not repair its components:\n\n{ex.Message}", Messages.Title,
                                MessageBoxButtons.OK, MessageBoxIcon.Error);
                return 1;
            }
        }

        // 開発 build で lib\ が無い場合は、exe と同じ folder の DLL を使う。
        string lib = Directory.Exists(AppPaths.Lib) ? AppPaths.Lib : AppContext.BaseDirectory;
        NativeSetup.Configure(lib);
        LogHub.OpenFile(AppPaths.Logs);
        Banner();
        NativeSetup.ConnectLog(AppPaths.Logs);
        ReportPreviousCrash();

        if (!Core.IsAvailable)
        {
            LogHub.Add(LogKind.Error, 0, $"utagoe_core.dll could not be loaded from {lib}");
            Utagoe.Forms.MessageForm.Show(
                "Utagoe's processing core could not be loaded.\n\n" +
                "Run the downloaded Utagoe.exe again to repair the installation.",
                Messages.Title, MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 1;
        }

        var settings = AppSettings.Load();
        // 選んである app icon の色違いを、window を作る前に決める (導入版は Start menu の shortcut も合わせる)。
        AppIcons.Apply(settings.Values.AppIcon, updateShell: true);
        L.Set(settings.Values.Language);
        Application.Run(new MainForm(settings));
        return 0;
    }

    private static int InstallOrHandOff(string[] args)
    {
        string exe = AppPaths.InstalledExe;
        if (!Installer.InstalledIsCurrentOrNewer())
        {
            using var setup = new InstallForm();
            Application.Run(setup);
            if (setup.InstalledExe == null) return 1;
            exe = setup.InstalledExe;
        }
        try
        {
            Process.Start(new ProcessStartInfo(exe) { UseShellExecute = false, Arguments = string.Join(' ', args.Select(Quote)) });
            return 0;
        }
        catch (System.ComponentModel.Win32Exception ex)
        {
            Utagoe.Forms.MessageForm.Show($"Utagoe could not be started from {exe}:\n\n{ex.Message}", Messages.Title,
                            MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 1;
        }
    }

    private static string Quote(string a) => a.Contains(' ') || a.Contains('"') ? $"\"{a.Replace("\"", "\\\"")}\"" : a;

    private static void Banner()
    {
        LogHub.Line($"Utagoe {Version} ({(AppPaths.IsPortable ? "portable" : "installed")}) started {DateTime.Now:yyyy-MM-dd HH:mm:ss}");
        LogHub.Detail($"program {AppPaths.Root}");
        LogHub.Detail($"windows {Environment.OSVersion.Version}, {System.Runtime.InteropServices.RuntimeInformation.FrameworkDescription}, {Environment.ProcessorCount} logical CPUs");
    }

    // 前回が core の中で落ちていたら知らせる (crash 記録は core が logs\ に書く)。
    private static void ReportPreviousCrash()
    {
        try
        {
            var last = Directory.GetFiles(AppPaths.Logs, "crash-*.txt").OrderByDescending(f => f).FirstOrDefault();
            if (last != null && File.GetLastWriteTime(last) > DateTime.Now.AddDays(-7))
                LogHub.Warning($"a recent crash report exists: {Path.GetFileName(last)} (type 'crash' to show it)");
        }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
    }

    private static void Unhandled(string what, Exception ex, bool fatal)
    {
        LogHub.Exception(what, ex);
        try
        {
            Directory.CreateDirectory(AppPaths.Logs);
            File.WriteAllText(Path.Combine(AppPaths.Logs, $"crash-{DateTime.Now:yyyyMMdd-HHmmss}-ui.txt"), $"{what}\n{ex}");
        }
        catch (Exception io) when (io is IOException or UnauthorizedAccessException) { }
        if (fatal) return;
        Utagoe.Forms.MessageForm.Show($"{what}:\n\n{ex.GetType().Name}: {ex.Message}{Messages.SeeTerminal}", Messages.Title,
                        MessageBoxButtons.OK, MessageBoxIcon.Error);
    }
}
