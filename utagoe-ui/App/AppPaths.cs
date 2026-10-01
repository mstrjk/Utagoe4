// 実行時の folder 構成 (v3 にない)。
// 配布は Utagoe.exe 1 つだけで、初回起動時に %LOCALAPPDATA%\Utagoe\ へ自分自身と必要な DLL を展開する。
//
//   %LOCALAPPDATA%\Utagoe\
//   ├── Utagoe.exe
//   ├── lib\          utagoe_core.dll と同梱ライブラリの DLL
//   ├── licenses\     同梱ライブラリの license 文書
//   ├── logs\         terminal の記録、crash 記録
//   ├── cache\        再生用の一時変換など
//   └── install.json  入っている版と file の一覧
//
// 開発中の build (exe の横に lib\ がある) は展開せず、その場で動かす (portable)。
// 設定 (INI) は v3 と同じ %LOCALAPPDATA%\UtagoeRip\ に置いたままにする。

namespace Utagoe.App;

internal static class AppPaths
{
    private static string LocalAppData => Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);

    /// 導入先。試験用に UTAGOE_INSTALL_ROOT で差し替えられる。
    public static string InstallRoot =>
        Environment.GetEnvironmentVariable("UTAGOE_INSTALL_ROOT") is { Length: > 0 } r ? r : Path.Combine(LocalAppData, "Utagoe");

    public static string InstalledExe => Path.Combine(InstallRoot, "Utagoe.exe");

    /// 今動いている exe。単一 file 配布でも実際の exe の path になる。
    public static string CurrentExe => Environment.ProcessPath ?? Path.Combine(AppContext.BaseDirectory, "Utagoe.exe");

    private static string ExeDir => Path.GetDirectoryName(CurrentExe) ?? AppContext.BaseDirectory;

    /// exe の横に lib\ がある開発用 build。展開せずにその場で動く。
    public static bool IsPortable =>
        File.Exists(Path.Combine(ExeDir, "lib", "utagoe_core.dll")) && !IsInstalledCopy;

    public static bool IsInstalledCopy =>
        string.Equals(Full(CurrentExe), Full(InstalledExe), StringComparison.OrdinalIgnoreCase);

    public static string Root => IsPortable ? ExeDir : InstallRoot;

    public static string Lib => Path.Combine(Root, "lib");
    public static string Logs => Path.Combine(Root, "logs");

    /// 出力先の既定 (v4)。利用者の Music\Utagoe。
    public static string DefaultOutputFolder
    {
        get
        {
            string music = Environment.GetFolderPath(Environment.SpecialFolder.MyMusic);
            if (music.Length == 0)
                music = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "Music");
            return Path.Combine(music, "Utagoe");
        }
    }
    public static string Cache => Path.Combine(Root, "cache");
    public static string Licenses => Path.Combine(Root, "licenses");
    public static string Manifest => Path.Combine(Root, "install.json");

    public static string Full(string p)
    {
        try { return Path.GetFullPath(p); } catch { return p; }
    }
}
