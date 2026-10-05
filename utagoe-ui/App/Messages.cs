// 元バイナリ由来のユーザー向け文言。
// 英語版に残っている英語文は表記揺れも含めてそのまま使う。未翻訳の日本語だけここで英訳している。

namespace Utagoe.App;

internal static class Messages
{
    public const string Title = "Utagoe";

    // StartBtnClick の文言は元バイナリどおり。
    public static string SameInputOutput => L.T("The input & output file are the same.");
    public static string HaltProcess => L.T("Halt active process?");

    // v3 の「wav 16-bit のみ」「sample rate 一致が必要」という制限はなくなったので、その文言は使わない。
    public static string NeedFiles => L.T("Select the original, the instrumental and an output folder.");
    public static string ChooseFolder => L.T("Choose the folder Utagoe saves into.");
    public static string FolderError(string folder, string error) => L.F("The output folder cannot be used:\n{0}\n\n{1}", folder, error);
    public static string Unreadable(string which, string error) => which == "original"
        ? L.F("The original file cannot be read.\n\n{0}", error)
        : L.F("The instrumental file cannot be read.\n\n{0}", error);

    // 処理中の Start button caption も元バイナリどおり。
    public static string Start => L.T("Start");
    public static string Running => L.T("Running");

    // Help は元の PDF ではなく terminal を開く (v3 にない)。
    public static string SeeTerminal => L.T("\n\nClick Help... for the full log and stack trace.");

    public static string[] TerminalHelp => L.A(
        "commands:",
        "  guide          how Utagoe works, in short",
        "  info           versions, folders, codec libraries",
        "  gpu            GPU acceleration status",
        "  status         whether processing is running",
        "  crash          show the latest crash report",
        "  logs           open the logs folder",
        "  copy           copy the whole log (also Ctrl+C)",
        "  clear          clear the screen (also Ctrl+L)",
        "  exit           close this window",
        "Everything shown here is also written to the logs folder."
    );

    public static string[] TerminalGuide => L.A(
        "Utagoe extracts a vocal by subtracting a known instrumental from the original song.",
        "  1. Original      the full song (vocal + backing).",
        "  2. Instrumental  the same song without the vocal (off vocal / karaoke version).",
        "  3. Output        where the extracted vocal is written.",
        "The two inputs may differ in format, sample rate and channels; Utagoe converts the instrumental.",
        "Methods (Settings > Processing Method):",
        "  Frequency        works in the spectrum; forgiving when the two versions differ slightly.",
        "  Waveform         subtracts the waveform; cleanest when the instrumental matches exactly.",
        "                   Settings > Waveform Algorithm offers algorithms for remasters (EQ, stereo, saturation).",
        "If the result still contains backing: try Waveform with Auto or Robust, or mark vocal-free",
        "passages (e.g. the intro) in Settings > Waveform Algorithm.",
        "Processing writes stages, timings and decisions to this terminal; errors include a stack trace."
    );
    // drop されたものが音声ファイルでない場合のメッセージ。元は「WAVE File をドロップしてください。」。
    public static string DropWave => L.T("Please drop an audio file.");

    public static string DebugModeIs => L.T("Debug mode is ");

    public static string GpuGroup => L.T("GPU Acceleration");
    public static string GpuUse => L.T("Use GPU");
    public static string GpuExact => L.T("Exact");
    public static string GpuFastest => L.T("Fastest");
    public static string GpuChecking => L.T("Checking GPU...");
    public static string GpuUsing(string adapter) => L.F("Using {0}", adapter);
    public static string GpuUnavailable(string reason) => L.F("Not available: {0}", reason);
    public static string GpuNotice(string reason) =>
        L.F("GPU acceleration isn't available on this PC, so Utagoe will use the CPU instead.\n" +
            "Results are the same; processing may just take a little longer.\n\n({0})", reason);
    public static string DontShowAgain => L.T("Do not show this again.");
    public static string GpuUsed(string adapter) => L.F("GPU: {0}", adapter);

    public static string ModelTab => L.T("Waveform Algorithm");
    public static string MainPage => L.T("Main");
    public static string UseMethod => L.T("Use this method");
    public static string FreqModelGroup => L.T("Frequency Algorithm");
    public static string[] FreqModelNames => L.A(
        "Utagoe v3 (frequency comparison)",
        "NMF: phase-independent (may dull vocals)",
        "Spatial: stereo image (may dull vocals)"
    );
    public static string ModelGroup => L.T("Waveform Processing Algorithm");
    public static readonly int[] ModelOrder = { 0, 1, 2, 6, 8, 11 };
    public static string[] ModelNames => L.A(
        "Utagoe v3 (time-domain subtraction)",
        "Robust: level, EQ, polarity, stereo, saturation",
        "Kalman: slowly changing level/EQ",
        "Ensemble: consensus of three",
        "Surface: smooth time-varying EQ/phase",
        "Low-rank: structured remaster changes"
    );
    public readonly record struct AlgoStats(double Sdr, double Sir, double Sar);
    public static readonly AlgoStats?[] ModelStats =
    {
        new(-2.75, 2.93, 1.03), new(-2.31, 1.66, 1.96), new(-2.00, 1.93, 1.46), new(-2.21, 1.66, 2.08), null, null, new(-2.08, 1.71, 2.09),
        new(-2.67, 1.88, 1.30), new(-2.55, 1.80, 1.38), new(-2.63, 1.20, 1.78), new(-2.47, 1.65, 2.09), new(-2.52, 1.92, 1.35), new(-2.30, 1.74, 1.86), new(-2.10, 1.77, 2.16),
    };
    public static readonly AlgoStats?[] FreqStats = { new(-2.39, 2.65, 0.64), new(0.01, -0.19, 1.80), new(-5.12, -3.69, 2.91) };
    public static string AlignGroup => L.T("Alignment");
    public static string LevelGroup => L.T("Instrumental Level Adjustment");
    public static readonly string[] ModelShortNames = { "v3", "Robust", "Kalman", "Robust", "NMF", "Spatial", "Ensemble", "Robust", "Surface", "Robust", "Robust", "Low-rank", "Ensemble", "Robust" };
    public static string[] AlignNames => L.A( "Utagoe v3 analysis", "Cross-correlation", "Dense time map"
    );
    public static string SpansGroup => L.T("Vocal-Free Passages");
    public static string MatchBandwidth => L.T("Match lowest bandwidth");
    public static string MatchLowEnd => L.T("Match low end");
    public static string RemoveSubsonic => L.T("Remove sub-bass rumble (below 20 Hz)");
    public static string FftGroup => L.T("Frequency Resolution");
    public static readonly int[] FftValues = { 0, 1024, 2048, 4096, 8192 };
    public static string[] FftNames => new[] { L.T("Automatic"), "1024", "2048", "4096", "8192" };
    public static string SpansHint => L.T("Times with no vocal, e.g. 0-10, 2:00-2:08. Calibration uses only these. Leave empty to use the whole song.");
    public static string ModelNote => L.T("Used by the Waveform method. Alignment also lines up the aligned pair output.");

    // 出力するもの (v3 にない)。揃えた組は出力欄の名前に _main / _inst を付けた 2 ファイル。
    public static string OutputGroup => L.T("Output");
    public static string OutputKindLabel => L.T("Save:");
    public static string[] SaveNames => L.A("Default", "Aligned pair", "Before post-processing", "Ensemble members");
    public static string MethodTab => L.T("Method");
    public static string MethodOptions => L.T("Method Options");
    public static string OverwriteFiles => L.T("Overwrite existing files");
    public static string NormaliseOutput => L.T("Normalise output");
    public static string NormaliseHint => L.T("Use if results are clipping");
    public static string CacheSteps => L.T("Cache steps");
    public static string CacheHint => L.T("Keeps decoding, matching and alignment in Music\\Utagoe\\cache, so trying another algorithm on the same files starts straight at the algorithm. Turning it off deletes the cache.");
    public static string OverwritePrompt(string name) => L.F("Do you want to overwrite {0}?", name);
    public const string PairSuffix      = "_aligned";

    // (?) で出す説明 (v4)。内容は core の実装 (engine.cpp / freq_engine.cpp / extract.cpp) に合わせている。
    public static class Tips
    {
        public static string Intro => L.T("Intro Analysis\n" +
            "Finds where the instrumental starts compared with the original.\n\n" +
            "Automatic: full analysis (also used for polarity, drift and level when those are on Automatic).\n" +
            "Normal: quick start-offset search.\n" +
            "Detailed: slower, more careful search.\n" +
            "None: assume both files start together.");
        public static string TimeShift => L.T("Time Shift Correction\n" +
            "Keeps the two files lined up when they slowly drift apart during the song.\n\n" +
            "Automatic: predicts the drift from the analysis.\n" +
            "Manual: searches a fixed Range around every block. A larger Range follows more drift but is slower.");
        public static string Method => L.T("Extraction Method\n" +
            "Frequency: compares the two in the frequency domain and removes what the instrumental covers. " +
            "More forgiving when the instrumental isn't an exact match.\n\n" +
            "Waveform: subtracts the lined-up instrumental sample by sample. " +
            "Cleanest when the instrumental is the exact same mix.");
        public static string Accuracy => L.T("Accuracy Priority (Frequency)\n" +
            "Quality: a frequency is removed only when the instrumental is stronger there and its phase matches. Keeps the vocal cleaner.\n\n" +
            "Extraction: removed whenever the instrumental is stronger. Removes more instrumental, but can thin the vocal.");
        public static string ExtractLevel => L.T("Extractable Level (Frequency)\n" +
            "How much is removed. Stronger removes more instrumental, but also more of the vocal.");
        public static string Level => L.T("Instrumental Level Adjustment (Waveform, v3 algorithm)\n" +
            "How loud the instrumental is made before it is subtracted.\n\n" +
            "Averaged: one level for the whole song. Adaptive: re-fitted for every block. " +
            "Manual: the slider value. None: left as it is.");
        public static string ProcMode => L.T("Processing Mode\n" +
            "Normal: left and right are handled separately.\n" +
            "L/R Difference: lines up using the difference of the two channels (left minus right).\n" +
            "Mono: works on the mix of both channels and outputs mono.\n\n" +
            "Mono input files always use Normal.");
        public static string Phase => L.T("Instrumental Phase\n" +
            "Whether the instrumental is upside down (polarity inverted) compared with the original.\n" +
            "Automatic detects it. Choose Positive or Inverted to force it.");
        public static string Filtering => L.T("Filtering\n" +
            "Applied to the result after extraction.\n\n" +
            "Extraction Centralization: keeps sound panned to the centre, where vocals usually are.\n" +
            "Low / High Pass Filter: removes everything above / below the chosen frequency.");
        public static string Oversampling => L.T("Oversampling (Waveform)\n" +
            "Lines the instrumental up in steps smaller than one sample. " +
            "The multiplier is the number of steps per sample: higher is more precise but slower.");
        public static string Fft => L.T("Frequency Resolution\n" +
            "How finely the algorithm splits the sound into frequencies when it measures how the instrumental differs from the original. " +
            "Larger sizes see finer detail and suit dense, steady mixes; smaller sizes follow fast changes. " +
            "Automatic uses each algorithm's best size: 2048 for Robust, Kalman and Ensemble, 1024 for Surface.");
        public static string Block => L.T("Block Length\n" +
            "The song is processed in blocks of this length. " +
            "Shorter blocks follow drift and level changes faster; longer ones are steadier.");
        public static string FileName => L.T("File Name Settings\n" +
            "Search For Instrumental File: when you choose an original, look next to it for a matching instrumental.\n" +
            "Automatically Name Output File: add the text below to the original's name. " +
            "When it's off, the output gets exactly the original's name (saved in the output folder).");
        public static string Output => L.T("Output\n" +
            "Save: tick every file you want. Default is the method's normal result.\n" +
            "Aligned pair: the original (_main) and the instrumental lined up to it in time and polarity (_inst), with the same length, " +
            "rate and channels. Levels are left untouched. On its own, it skips extraction.\n" +
            "Before post-processing: the vocal before Filtering and the sub-bass cut (_raw).\n" +
            "Ensemble members: each algorithm inside Ensemble, on its own (_robust, _kalman, ...).\n\n" +
            "Format, Bit Depth and Bitrate apply to every file written.");
        public static string Gpu => L.T("GPU Acceleration\n" +
            "Exact: only the alignment searches run on the GPU. Results are identical to the CPU.\n" +
            "Fastest: more of the work runs on the GPU. Results can differ very slightly from the CPU.");
        public static string Model => L.T("Waveform Processing Algorithm\n" +
            "v3: Utagoe's original subtraction with one level for the instrumental.\n" +
            "The other algorithms measure how the instrumental differs from the original at every frequency " +
            "and correct for it (level, EQ, polarity, stereo, and more), so they set the level themselves.\n\n" +
            "Surface and Low-rank learn only from the stereo difference (left minus right), " +
            "so a centred vocal never steers them. They need stereo files.\n\n" +
            "Ensemble: Robust (with and without its saturation correction) and Kalman vote; where they agree their consensus is used, otherwise Robust.");
        public static string FreqModel => L.T("Frequency Processing Algorithm\n" +
            "Utagoe v3: compares the two in the frequency domain and removes what the instrumental covers. " +
            "Accuracy Priority and Extractable Level set how much.\n" +
            "NMF: learns the instrumental's sound as a set of spectral patterns and removes them from the original, " +
            "ignoring phase. Copes with heavy processing differences, but can dull the vocal.\n" +
            "Spatial: separates by where each sound sits in the stereo image, using the instrumental as the guide. " +
            "Works best on stereo files, and can dull the vocal.\n\n" +
            "All three shape the original's spectrum rather than subtract the instrumental's waveform, " +
            "so they never cancel perfectly but forgive mismatches that a waveform subtraction cannot.");
        public static string Align => L.T("Alignment\n" +
            "How the instrumental is lined up with the original.\n\n" +
            "Utagoe v3 analysis: v3's block-by-block search. Follows drift; uses the Intro, Time Shift, Phase, " +
            "Processing Mode, Block Length and Oversampling settings.\n" +
            "Cross-correlation: one start offset plus a steady speed difference, with sub-sample precision. Ignores those settings.\n" +
            "Dense time map: follows a speed difference that changes during the song, with sub-sample precision, " +
            "using the stereo difference so the vocal doesn't steer it. Needs stereo files. Ignores those settings.");
        public static string Spans => L.T("Vocal-Free Passages\n" +
            "Parts of the song with no vocal, for example 0-10, 2:00-2:08. The algorithms learn how the instrumental " +
            "differs from the original only from these parts. Leave empty to use the whole song.");

        public static string RemoveSubsonic => L.T("Remove Sub-bass Rumble\n" +
            "Mixes often carry a slow swell below 20 Hz that follows the beat. When the instrumental was mastered with a low cut, " +
            "nothing in it can cancel that swell, so it stays in the vocal. It is too low to hear as a sound, but a limiter, " +
            "a compressor or a speaker reacts to it and the vocal pumps with the beat.\n\n" +
            "This filters the result below 20 Hz, where a voice has nothing. Unlike the rest of the waveform methods it removes " +
            "content instead of subtracting the instrumental, so it is off by default.");
        public static string MatchLowEnd => L.T("Match Low End\n" +
            "Two masters almost never share the same low-frequency phase: one has usually been through an extra DC or high-pass filter. " +
            "The mix's limiter also rides the bass slightly when the vocal comes in. Left alone, every kick and bass note leaks into the vocal " +
            "and pumps with the beat.\n\n" +
            "This measures both below 200 Hz, where the vocal can't get in the way, and corrects the instrumental before any method runs. " +
            "The vocal's own low end is left alone. Adds a few seconds per song.");
        public static string MatchBandwidth => L.T("Match Lowest Bandwidth\n" +
            "When one file has been through a lossy codec (MP3, AAC, Ogg Vorbis, Opus, WMA) and the other hasn't, or was coded harder, " +
            "the worse file is missing the top of the spectrum, and subtraction leaves the other file's high end behind as hiss.\n\n" +
            "This finds the file with less real data (the instrumental gets a little leeway, since it naturally has less high end) " +
            "and follows its cutoff moment by moment, removing the same high end from the better file before they are subtracted. " +
            "The vocal can only keep what the worse file still has. " +
            "Files that are stored lossless but were made from a lossy copy are recognised by their cutoff too.\n\n" +
            "The codec's other damage, such as quantization noise and pre-echo, is random and can't be copied onto the better file; " +
            "doing that would only add more noise. When both files are equally good, or the worse file's cutoff is too unsteady to follow, nothing is changed.");
        public static string Stats(string name, AlgoStats? s) => s is not { } v ? name :
            string.Format(System.Globalization.CultureInfo.CurrentCulture,
                "{0}\nSDR {1:+0.0;-0.0} dB\nSIR {2:+0.0;-0.0} dB\nSAR {3:+0.0;-0.0} dB", name, v.Sdr, v.Sir, v.Sar);

        public static string NotNow => L.T("\n\nNot used right now: ");
        public static string BecausePair => L.T("Save is set to Aligned pair, which doesn't extract anything.");
        public static string BecauseFreqOnly => L.T("only the Frequency method uses this.");
        public static string BecauseV3FreqOnly => L.T("only the Utagoe v3 frequency algorithm uses this.");
        public static string BecauseWaveOnly => L.T("only the Waveform method uses this.");
        public static string BecauseGcc => L.T("this alignment doesn't run v3's analysis.");
        public static string BecauseV3Align => L.T("v3 subtraction always uses v3's analysis. Alignment is used by the other algorithms and by the Aligned pair output.");
        public static string BecauseV3Model => L.T("only the algorithms other than v3 use this.");
        public static string BecauseV3Only => L.T("only Utagoe v3 uses this.");
        public static string BecauseNoSpans(string name) => L.F("the {0} algorithm learns from the whole song, using the stereo difference instead.", name);
        public static string BecauseFixedFft(string name) => L.F("the {0} algorithm works at a fixed resolution.", name);
        public static string BecauseAutoName => L.T("Automatically Name Output File is off.");
        public static string BecauseGpuOff => L.T("GPU acceleration is off.");
    }

    // main window を最大化して Settings を組み込んだときの button。
    public static string Reset => L.T("Reset");
    public static string ResetTitle => L.T("Reset to defaults?");
    public static string ResetConfirm => L.T("All processing settings go back to their defaults and are saved right away. Your theme and output folder are kept.");
    // 別 window の Settings は OK を押すまで保存しない。
    public static string SettingsPanel => L.T("Settings");
    public static string MainPanel => L.T("Utagoe");
    public static string InfoPanel => L.T("Information");
    // 最大化したときの説明欄の最初の文。__ で囲んだ部分は link と同じ青い下線で描く。
    public static string Welcome => L.T("Welcome to Utagoe!\n" +
        "Let's get you learning about Utagoe.\n" +
        "Hover over __blue and underlined__ messages, and see information on this pane.");
    public static string PairNeedsTwo => L.T("The aligned pair needs two different files: the original and its instrumental.");
    public static string BadSpans(string error) => L.F("The vocal-free passages cannot be read.\n\n{0}", error);

    // 経過時間表示の元形式は「%d 秒経過」。
    public static string Elapsed(int seconds) => L.F("{0} sec", seconds);

    // 読み込める拡張子。最終的な判定は中身で行うので、ここは file dialog と drop 用の目安。
    public static readonly string[] AudioExtensions =
    {
        ".wav", ".wave", ".w64", ".rf64", ".aif", ".aiff", ".aifc", ".flac", ".mp3",
        ".m4a", ".mp4", ".aac", ".ogg", ".oga", ".opus", ".wma",
    };

    public static string OpenFilter =>
        L.T("Audio Files") + "|" + string.Join(";", AudioExtensions.Select(e => "*" + e)) +
        "|" + L.T("Wave File") + " (*.wav)|*.WAV|" + L.T("All Files") + " (*.*)|*.*";

    // 保存 dialog の filter。並びは OutputFormat の値と同じにして、FilterIndex と対応させる。
    public const string SaveFilter =
        "WAV (*.wav)|*.wav|AIFF (*.aiff)|*.aiff|FLAC (*.flac)|*.flac|ALAC (*.m4a)|*.m4a|" +
        "MP3 (*.mp3)|*.mp3|AAC (*.m4a)|*.m4a|Ogg Vorbis (*.ogg)|*.ogg|Opus (*.opus)|*.opus|WMA (*.wma)|*.wma";

    public static readonly string[] FormatNames =
        { "WAV", "AIFF", "FLAC", "ALAC (.m4a)", "MP3", "AAC (.m4a)", "Ogg Vorbis", "Opus", "WMA" };

    // 形式表示。WAV は元実装の "%.3fkHz %d-Bit   %s" そのままで、ほかの形式は codec 名を前に付ける。
    public static string AudioFormat(in Native.CoreAudioInfo i)
    {
        var inv = System.Globalization.CultureInfo.InvariantCulture;
        string ch = i.Channels switch { 1 => L.T("Mono"), 2 => L.T("Stereo"), _ => $"{i.Channels}ch" };
        string depth = i.Lossless != 0
            ? (i.FloatingPoint != 0 ? $"{i.Bits}-Bit Float" : $"{i.Bits}-Bit")
            : (i.BitrateKbps > 0 ? $"{i.BitrateKbps}kbps" : "");
        string codec = i.Codec == "WAV" ? "" : i.Codec + " ";
        return string.Format(inv, "{0}{1:F3}kHz {2}   {3}", codec, i.SampleRate / 1000.0, depth, ch);
    }

    // 処理時に memory 確保へ失敗した場合の元メッセージ。
    public static string OutOfMemory => L.T("Failed to allocate memory.");
    // 出力 file 作成に失敗した場合の元メッセージ。
    public static string FileCreateError => L.T("An error occurred while creating the file.");
}
