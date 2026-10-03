using Utagoe.Native;

namespace Utagoe.App;

internal static class SaveFiles
{
    public const int Default = 1, Aligned = 2, Raw = 4, Members = 8;

    public static readonly string[] MemberSuffixes =
        { "_robust", "_kalman", "_hammerstein", "_rational", "_surface", "_trend", "_ctf", "_low_rank", "_ensemble_small", "_ensemble_large" };

    public static bool MembersAvailable(int mergeMode, int waveModel) => mergeMode == 1 && waveModel is 6 or 12 or 13;

    public static int Effective(int kind, int mask, bool membersAvailable)
    {
        if (kind == 1) return Aligned;
        if (kind != 0) return Default;
        int m = mask & 15;
        if (!membersAvailable) m &= ~Members;
        return m == 0 ? Default : m;
    }

    public static int Effective(in CoreSettings s) => Effective(s.OutputKind, s.SaveMask, MembersAvailable(s.MergeMode, s.WaveModel));

    public static int Kind(in CoreSettings s) => s.OutputKind == 0 && Effective(s) == Aligned ? 1 : s.OutputKind;

    public static List<string> Extras(string output, in CoreSettings s)
    {
        var files = new List<string>();
        if (Kind(s) != 0) return files;
        int m = Effective(s);
        string dir = Path.GetDirectoryName(output) ?? "";
        string stem = Path.GetFileNameWithoutExtension(output), ext = Path.GetExtension(output);
        string With(string suffix) => Path.Combine(dir, stem + suffix + ext);
        if ((m & Raw) != 0) files.Add(With("_raw"));
        if ((m & Members) != 0) files.AddRange(MemberSuffixes.Select(With));
        if ((m & Aligned) != 0)
        {
            var (main, inst) = Core.AlignedPairPaths(output);
            files.Add(main);
            files.Add(inst);
        }
        return files;
    }
}
