// [V30_Option] を元実装と同じ形式で保存する設定クラス。
// INI の読み書きは native core に集約し、format 定義を一か所に保つ。

using Utagoe.Native;

namespace Utagoe.App;

internal sealed class AppSettings
{
    public CoreSettings Values;

    /// INI path は元実装と同じ規則で %LOCALAPPDATA%\UtagoeRip\<exe name>.ini。
    public static string Folder =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "UtagoeRip");

    public static string IniPath =>
        Path.Combine(Folder, Path.GetFileNameWithoutExtension(Environment.ProcessPath ?? "Utagoe") + ".ini");

    public static AppSettings Load()
    {
        var s = new AppSettings();
        Core.DefaultSettings(out s.Values);

        string? source = File.Exists(IniPath) ? IniPath : FindOriginalIni();
        if (source != null) Core.LoadIni(source, ref s.Values);
        return s;
    }

    /// 初回だけ同じ folder にある Utagoe v3 の設定を読み継ぐ。元ファイルは書き換えない。
    private static string? FindOriginalIni()
    {
        if (!Directory.Exists(Folder)) return null;
        return Directory.EnumerateFiles(Folder, "*.ini")
            .Where(f => File.ReadLines(f).Take(4).Any(l => l.Trim() == "[V30_Option]"))
            .OrderByDescending(File.GetLastWriteTimeUtc)
            .FirstOrDefault();
    }

    public bool Save()
    {
        Directory.CreateDirectory(Folder);
        return Core.SaveIni(IniPath, Values) == 0;
    }

    public static CoreSettings Defaults()
    {
        Core.DefaultSettings(out var d);
        return d;
    }
}

/// トラックバー横の表示値は元実装の OnChange 計算に合わせる。
internal static class TrackLabels
{
    private static readonly IFormatProvider Inv = System.Globalization.CultureInfo.InvariantCulture;

    /// Extractable Level は position 10 を境に傾きが変わる。
    public static string ExtractLevel(int pos)
    {
        float v = pos < 10 ? (float)(pos * 0.1 + 0.6) : (float)((pos - 9) * 0.3 + 1.5);
        return v.ToString("F1", Inv);
    }

    public static string InstLevel(int pos) => ((float)(pos * 0.03 + 0.7)).ToString("F2", Inv);

    public static string Centralize(int pos) => (pos * 0.25f + 0.5f).ToString("F2", Inv);

    public static string LowPass(int pos) => ((pos * 800 + 2000) * 0.001).ToString("F1", Inv) + "kHz";

    /// High-pass の丸めは Math.Round 既定の half-to-even で、x87 既定動作に合わせる。
    public static string HighPass(int pos)
    {
        double p = pos + 1;
        return ((int)Math.Round(p * 1.5 * p + 49.0)).ToString(Inv) + "Hz";
    }
}
