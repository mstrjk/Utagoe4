// 自己導入 (v3 にない)。配布は Utagoe.exe 1 つで、必要な DLL と license 文書は exe の中に埋め込んである。
// 初回は %LOCALAPPDATA%\Utagoe\ へ自分自身を写して展開し、Start menu と「アプリと機能」に登録する。
// 管理者権限は使わない (すべて利用者ごとの場所)。導入済みの copy は起動時に足りない file を埋め直す。
//
// 埋め込みの名前は "payload/lib/<dll>" と "payload/licenses/<library>\<file>"。

using System.Diagnostics;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;
using Microsoft.Win32;
using Utagoe.Native;

namespace Utagoe.App;

internal static class Installer
{
    private const string Prefix = "payload/";
    private const string UninstallKey = @"Software\Microsoft\Windows\CurrentVersion\Uninstall\Utagoe";

    private static Assembly Asm => typeof(Installer).Assembly;

    private static IEnumerable<string> PayloadNames =>
        Asm.GetManifestResourceNames().Where(n => n.StartsWith(Prefix, StringComparison.Ordinal));

    /// この exe に展開できる中身があるか (開発中の build には無いこともある)。
    public static bool HasPayload => PayloadNames.Any();

    /// 試験用: UTAGOE_NO_SHELL=1 なら shortcut と registry には触れない。
    private static bool ShellIntegration => Environment.GetEnvironmentVariable("UTAGOE_NO_SHELL") != "1";

    private static string StartMenuLink =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Programs), "Utagoe.lnk");

    private sealed record Manifest(string Version, Dictionary<string, long> Files);

    /// Start menu の shortcut と「アプリ」一覧の icon を ico に向ける (About で選んだ色違い)。shortcut が無ければ何もしない。
    public static bool SetShellIcon(string ico)
    {
        if (!ShellIntegration || !File.Exists(StartMenuLink)) return false;
        ShellLink.Create(StartMenuLink, AppPaths.InstalledExe, "Utagoe - vocal extraction by instrumental subtraction", ico);
        using var key = Registry.CurrentUser.OpenSubKey(UninstallKey, writable: true);
        key?.SetValue("DisplayIcon", ico);
        return true;
    }


    public static Version CurrentVersion => Asm.GetName().Version ?? new Version(0, 0);

    public static Version? InstalledVersion
    {
        get
        {
            try
            {
                if (!File.Exists(AppPaths.InstalledExe)) return null;
                var v = FileVersionInfo.GetVersionInfo(AppPaths.InstalledExe);
                return new Version(v.FileMajorPart, v.FileMinorPart, v.FileBuildPart, v.FilePrivatePart);
            }
            catch (Exception ex) when (ex is IOException or ArgumentException) { return null; }
        }
    }

    /// 導入済みの copy をそのまま使えるか。新しい版か、同じ版で中身も同じなら使う。
    public static bool InstalledIsCurrentOrNewer()
    {
        var installed = InstalledVersion;
        if (installed == null) return false;
        if (installed > CurrentVersion) return true;
        if (installed < CurrentVersion) return false;
        return SameFile(AppPaths.InstalledExe, AppPaths.CurrentExe);
    }

    private static bool SameFile(string a, string b)
    {
        try
        {
            var fa = new FileInfo(a);
            var fb = new FileInfo(b);
            if (!fa.Exists || !fb.Exists || fa.Length != fb.Length) return false;
            using var sa = fa.OpenRead();
            using var sb = fb.OpenRead();
            return SHA256.HashData(sa).AsSpan().SequenceEqual(SHA256.HashData(sb));
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { return false; }
    }


    /// 導入して、導入先の exe の path を返す。進み具合は report に 1 行ずつ渡す。
    public static string Install(Action<string> report)
    {
        string root = AppPaths.InstallRoot;
        report($"installing to {root}");
        foreach (string d in new[] { root, Path.Combine(root, "lib"), Path.Combine(root, "logs"), Path.Combine(root, "cache"), Path.Combine(root, "licenses") })
            Directory.CreateDirectory(d);

        string exe = AppPaths.InstalledExe;
        if (!string.Equals(AppPaths.Full(exe), AppPaths.Full(AppPaths.CurrentExe), StringComparison.OrdinalIgnoreCase))
        {
            long size = new FileInfo(AppPaths.CurrentExe).Length;
            report($"  Utagoe.exe ({size / 1048576.0:0.0} MB)");
            try
            {
                File.Copy(AppPaths.CurrentExe, exe, overwrite: true);
            }
            catch (IOException ex) when (IsLocked(ex))
            {
                throw new InvalidOperationException(
                    "Utagoe is already running from the install folder. Close it and run this file again.", ex);
            }
        }
        Extract(root, report);

        if (ShellIntegration)
        {
            ShellLink.Create(StartMenuLink, exe, "Utagoe - vocal extraction by instrumental subtraction");
            report("  Start menu shortcut");
            Register(exe, root);
            report("  registered in Apps & features (uninstall from there)");
        }
        return exe;
    }

    /// 導入済みの copy が起動時に呼ぶ。版が変わったか、file が欠けていれば埋め直す。
    public static void EnsurePayload(Action<string> report)
    {
        if (!HasPayload) return;
        string root = AppPaths.InstallRoot;
        var manifest = ReadManifest(root);
        bool intact = manifest != null && manifest.Version == CurrentVersion.ToString() &&
                      manifest.Files.All(kv => new FileInfo(Path.Combine(root, kv.Key)).Exists &&
                                               new FileInfo(Path.Combine(root, kv.Key)).Length == kv.Value);
        if (intact) return;
        report(manifest == null ? "first start: unpacking components" : "repairing missing or outdated components");
        Extract(root, report);
    }

    private static void Extract(string root, Action<string> report)
    {
        var files = new Dictionary<string, long>();
        int licenses = 0;
        foreach (string name in PayloadNames.OrderBy(n => n, StringComparer.Ordinal))
        {
            string rel = name[Prefix.Length..].Replace('\\', '/');
            string dest = Path.Combine(root, rel.Replace('/', Path.DirectorySeparatorChar));
            Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
            using (var src = Asm.GetManifestResourceStream(name)!)
            {
                // 同じ中身ならそのまま (使用中の DLL を書き換えようとしない)。
                if (!SameContent(src, dest))
                {
                    src.Position = 0;
                    try
                    {
                        using var dst = File.Create(dest);
                        src.CopyTo(dst);
                    }
                    catch (IOException ex) when (IsLocked(ex))
                    {
                        throw new InvalidOperationException($"{rel} is in use. Close every Utagoe window and try again.", ex);
                    }
                }
                files[rel] = src.Length;
            }
            if (rel.StartsWith("licenses/", StringComparison.Ordinal)) licenses++;
            else report($"  {rel} ({files[rel] / 1024.0:0} KB)");
        }
        if (licenses > 0) report($"  licenses ({licenses} files)");
        var json = JsonSerializer.Serialize(new Manifest(CurrentVersion.ToString(), files), new JsonSerializerOptions { WriteIndented = true });
        File.WriteAllText(Path.Combine(root, "install.json"), json);
    }

    private static bool SameContent(Stream src, string dest)
    {
        try
        {
            var fi = new FileInfo(dest);
            if (!fi.Exists || fi.Length != src.Length) return false;
            byte[] a = SHA256.HashData(src);
            using var f = fi.OpenRead();
            return a.AsSpan().SequenceEqual(SHA256.HashData(f));
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { return false; }
    }

    private static Manifest? ReadManifest(string root)
    {
        try
        {
            string p = Path.Combine(root, "install.json");
            return File.Exists(p) ? JsonSerializer.Deserialize<Manifest>(File.ReadAllText(p)) : null;
        }
        catch (Exception ex) when (ex is IOException or JsonException) { return null; }
    }

    private static bool IsLocked(IOException ex) => (ex.HResult & 0xFFFF) is 32 or 33;

    private static void Register(string exe, string root)
    {
        using var key = Registry.CurrentUser.CreateSubKey(UninstallKey);
        key.SetValue("DisplayName", "Utagoe");
        key.SetValue("DisplayVersion", CurrentVersion.ToString(2));
        key.SetValue("DisplayIcon", $"\"{exe}\",0");
        key.SetValue("InstallLocation", root);
        key.SetValue("UninstallString", $"\"{exe}\" --uninstall");
        key.SetValue("Publisher", "Utagoe");
        key.SetValue("NoModify", 1, RegistryValueKind.DWord);
        key.SetValue("NoRepair", 1, RegistryValueKind.DWord);
        long bytes = Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories).Sum(f => new FileInfo(f).Length);
        key.SetValue("EstimatedSize", (int)(bytes / 1024), RegistryValueKind.DWord);
    }


    /// 「アプリと機能」から呼ばれる。設定 (UtagoeRip の INI) は残す。
    public static int UninstallInteractive()
    {
        string root = AppPaths.InstallRoot;
        var answer = Utagoe.Forms.MessageForm.Show(
            L.F("Remove Utagoe from this PC?\n\n{0}\n\nYour settings in {1} are kept.", root, AppSettings.Folder),
            Messages.Title, MessageBoxButtons.YesNo, MessageBoxIcon.Question);
        if (answer != DialogResult.Yes) return 1;
        try
        {
            if (ShellIntegration)
            {
                if (File.Exists(StartMenuLink)) File.Delete(StartMenuLink);
                Registry.CurrentUser.DeleteSubKeyTree(UninstallKey, throwOnMissingSubKey: false);
            }
            foreach (string d in new[] { "lib", "licenses", "logs", "cache" })
            {
                string p = Path.Combine(root, d);
                if (Directory.Exists(p)) Directory.Delete(p, recursive: true);
            }
            string m = Path.Combine(root, "install.json");
            if (File.Exists(m)) File.Delete(m);
            // 動いている exe 自身は消せないので、終了後に cmd で folder ごと消す。
            Process.Start(new ProcessStartInfo("cmd.exe", $"/c ping 127.0.0.1 -n 3 > nul & rmdir /s /q \"{root}\"")
            {
                CreateNoWindow = true,
                UseShellExecute = false,
                WindowStyle = ProcessWindowStyle.Hidden,
            });
            Utagoe.Forms.MessageForm.Show(L.T("Utagoe has been removed."), Messages.Title, MessageBoxButtons.OK, MessageBoxIcon.Information);
            return 0;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            Utagoe.Forms.MessageForm.Show(L.F("Utagoe could not be removed completely:\n\n{0}\n\nClose every Utagoe window and try again.", ex.Message),
                            Messages.Title, MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return 2;
        }
    }
}
