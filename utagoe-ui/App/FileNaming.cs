// インスト音源の自動検索と出力名の自動生成。
// keyword / separator / 禁止文字 / suffix はバイナリから復元したが、候補選択 policy は完全追跡できていないので再構成。

using System.Globalization;

namespace Utagoe.App;

internal static class FileNaming
{
    public static string Clean(string path)
    {
        if (string.IsNullOrEmpty(path)) return path;
        path = path.Trim().Trim('"');
        if (path.StartsWith(@"\\?\UNC\", StringComparison.OrdinalIgnoreCase)) return @"\\" + path[8..];
        if (path.StartsWith(@"\\?\", StringComparison.Ordinal) || path.StartsWith(@"\\.\", StringComparison.Ordinal)) return path[4..];
        return path;
    }

    /// インスト版を示す keyword 群。
    /// 英語版バイナリでは翻訳時に一部の日本語 keyword が同じ byte 長の英語で上書きされている。
    /// 欠けた元語は full-width の「インスト」「カラオケ」「オフボーカル」「オフヴォーカル」と判断できるため、英語語彙と一緒に復元している。
    public static readonly string[] InstrumentalKeywords =
    {
        "inst", "karaoke", "without", "vocalless", "vocal less", "vocal-less",
        "off vocal", "less vocal", "music only",
        "ｲﾝｽﾄ", "ｶﾗｵｹ", "ｵﾌﾎﾞｰｶﾙ", "ｵﾌｳﾞｫｰｶﾙ", "からおけ",
        "インスト", "カラオケ", "オフボーカル", "オフヴォーカル",
        "instrumental", "off-vocal",
    };

    private const string StemSeparators = " _-([";
    public const string ForbiddenSuffixChars = "\\/:*?\"<>|";

    // 比較は case / width / kana を無視する。例: ｶﾗｵｹ、カラオケ、からおけ を同一視する。
    private const CompareOptions Loose =
        CompareOptions.IgnoreCase | CompareOptions.IgnoreWidth | CompareOptions.IgnoreKanaType;

    private static readonly CompareInfo Cmp = CultureInfo.InvariantCulture.CompareInfo;

    /// originalPath と同じ title を持ち、instrumental keyword を含む WAV を同じ directory から探す。見つからなければ null。
    public static string? FindInstrumental(string originalPath)
    {
        string? dir = Path.GetDirectoryName(originalPath);
        if (string.IsNullOrEmpty(dir) || !Directory.Exists(dir)) return null;

        string origName = Path.GetFileNameWithoutExtension(originalPath);
        string stem = Stem(origName);

        var candidates = new List<string>();
        try
        {
            foreach (string f in Directory.EnumerateFiles(dir))
            {
                // 対応形式ならどれでもインスト候補にする。原曲と形式が違ってもよい。
                if (!Messages.AudioExtensions.Contains(Path.GetExtension(f), StringComparer.OrdinalIgnoreCase)) continue;
                if (string.Equals(Path.GetFullPath(f), Path.GetFullPath(originalPath),
                                  StringComparison.OrdinalIgnoreCase))
                    continue;

                string name = Path.GetFileNameWithoutExtension(f);
                if (!Cmp.IsPrefix(name, stem, Loose)) continue;
                if (!InstrumentalKeywords.Any(k => Cmp.IndexOf(name, k, Loose) >= 0)) continue;
                candidates.Add(f);
            }
        }
        catch (IOException) { return null; }
        catch (UnauthorizedAccessException) { return null; }

        // 候補が複数なら短い名前を優先し、同長なら alphabetic order。
        return candidates
            .OrderBy(f => Path.GetFileName(f).Length)
            .ThenBy(f => f, StringComparer.OrdinalIgnoreCase)
            .FirstOrDefault();
    }

    /// 出力名は元ファイル名の拡張子前に suffix を付け、拡張子は出力形式のものにする。
    public static string AutoOutputName(string originalPath, string suffix, string extension)
    {
        string dir = Path.GetDirectoryName(originalPath) ?? "";
        string name = Path.GetFileNameWithoutExtension(originalPath);
        return Path.Combine(dir, name + SanitizeSuffix(suffix) + extension);
    }

    public static string SanitizeSuffix(string suffix) =>
        new(suffix.Where(c => ForbiddenSuffixChars.IndexOf(c) < 0).ToArray());

    /// title は file name の最初の separator より前。
    private static string Stem(string name)
    {
        int cut = name.IndexOfAny(StemSeparators.ToCharArray());
        return cut > 0 ? name[..cut] : name;
    }
}
