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
    public static string NeedOriginal => L.T("Select the original and an output folder.");
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
    public static string ModelGroup => L.T("Waveform Processing Algorithm");
    public static readonly int[] ModelOrder = { 13, 0, 1, 2, 3, 4, 5, 6, 12, 7, 8, 9, 10, 11 };
    public static string[] ModelNames => L.A(
        "Auto: try them all, keep the best",
        "Utagoe v3 (time-domain subtraction)",
        "Robust: level, EQ, polarity, stereo",
        "Kalman: slowly changing level/EQ",
        "Hammerstein: mild saturation differences",
        "NMF: phase-independent (may dull vocals)",
        "Spatial: stereo image (may dull vocals)",
        "Ensemble Small: consensus of three",
        "Ensemble Large: consensus of all",
        "Rational: phase-matched subtraction",
        "Surface: smooth time-varying EQ/phase",
        "Trend: remaster level changes over time",
        "CTF: short smear/reverb differences",
        "Low-rank: structured remaster changes"
    );
    public static string AlignGroup => L.T("Alignment");
    // 代替モデルは level を自分で合わせるので、Instrumental Level Adjustment は使わない。そのとき group の見出しで理由を示す。
    public static string LevelGroup => L.T("Instrumental Level Adjustment");
    public static readonly string[] ModelShortNames = { "v3", "Robust", "Kalman", "Hammerstein", "NMF", "Spatial", "Ensemble Small", "Rational", "Surface", "Trend", "CTF", "Low-rank", "Ensemble Large", "Auto" };
    public static string LevelSetByModel(int model) =>
        L.F("Level set by the {0} algorithm", ModelShortNames[Math.Clamp(model, 0, ModelShortNames.Length - 1)]);
    public static string[] AlignNames => L.A( "Utagoe v3 analysis", "Cross-correlation", "Dense time map"
    );
    public static string SpansGroup => L.T("Vocal-Free Passages");
    public static string KickDuck => L.T("Match kick ducking");
    public static string SpansHint => L.T("Times with no vocal, e.g. 0-10, 2:00-2:08. Calibration uses only these. Leave empty to use the whole song.");
    public static string ModelNote => L.T("Used by the Waveform method. Alignment also lines up the aligned pair output.");

    // 出力するもの (v3 にない)。揃えた組は出力欄の名前に _main / _inst を付けた 2 ファイル。
    public static string OutputGroup => L.T("Output");
    public static string OutputKindLabel => L.T("Save:");
    public static string[] OutputKindNames => L.A( "Vocals", "Aligned pair"
    );
    public static string MethodTab => L.T("Method");
    public static string CentreTab => L.T("Centre / Sides");
    public static string MethodOptions => L.T("Method Options");
    public static string NeedsBoth => L.T("Needs the original and its instrumental.");
    public static string NeedsOriginal => L.T("Only the original is needed.");
    public static string CentreGroup => L.T("Centre / Sides Method");
    public static string[] CentreNames => L.A(
        "True M/S: exact mid and side, no guessing",
        "Phantom: equal level and phase in both",
        "Coherence: steady, smoothed centre image",
        "ADRess: narrow centre by pan nulling",
        "DUET: narrow centre by level and delay",
        "PCA: dominant centred component",
        "Pretty: softest, fewest artifacts"
    );
    public static string FindWithinGroup => L.T("FindWithin");
    public static string RepeatGuideLabel => L.T("Guide:");
    public static string[] RepeatGuideNames => L.A( "Auto", "Full mix", "Side (L-R)"
    );
    public static string RepeatSearchLabel => L.T("Search:");
    public static string[] RepeatSearchNames => L.A( "Normal", "Broad (slower)"
    );
    public static string OverwriteFiles => L.T("Overwrite existing files");
    public static string OverwritePrompt(string name) => L.F("Do you want to overwrite {0}?", name);
    public static string NoRepeats => L.T("No repeated passage passed the cancellation test, so no audio was saved. repeats.csv lists every passage that was checked.");
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
            "Cleanest when the instrumental is the exact same mix.\n\n" +
            "Centre + sides: no instrumental needed. Splits the original into what sits in the centre and everything else.\n\n" +
            "FindWithin: no instrumental needed. Finds passages that repeat within the original and saves " +
            "what each repeat shares with its earlier copy and what changed.");
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
        public static string Block => L.T("Block Length\n" +
            "The song is processed in blocks of this length. " +
            "Shorter blocks follow drift and level changes faster; longer ones are steadier.");
        public static string FileName => L.T("File Name Settings\n" +
            "Search For Instrumental File: when you choose an original, look next to it for a matching instrumental.\n" +
            "Automatically Name Output File: add the text below to the original's name. " +
            "When it's off, the output gets exactly the original's name (saved in the output folder).");
        public static string Output => L.T("Output\n" +
            "Save - Vocals: the extracted vocals (normal Utagoe).\n" +
            "Save - Aligned pair: no extraction. Saves the original (_main) and the instrumental lined up to it " +
            "in time and polarity (_inst), with the same length, rate and channels. Levels are left untouched. " +
            "The Alignment choice on the Waveform Algorithm tab decides how they are lined up.\n" +
            "Save is used by the Frequency and Waveform methods. Centre + sides and FindWithin always save their own files.\n\n" +
            "Format, Bit Depth and Bitrate apply to every file written.");
        public static string Gpu => L.T("GPU Acceleration\n" +
            "Exact: only the alignment searches run on the GPU. Results are identical to the CPU.\n" +
            "Fastest: more of the work runs on the GPU. Results can differ very slightly from the CPU.");
        public static string Model => L.T("Waveform Processing Algorithm\n" +
            "v3: Utagoe's original subtraction with one level for the instrumental.\n" +
            "The other algorithms measure how the instrumental differs from the original at every frequency " +
            "and correct for it (level, EQ, polarity, stereo, and more), so they set the level themselves.\n\n" +
            "Rational, Surface, Trend, CTF and Low-rank learn only from the stereo difference (left minus right), " +
            "so a centred vocal never steers them. They need stereo files.\n\n" +
            "Ensemble Small: Robust, Kalman and Hammerstein vote; where they agree their consensus is used, otherwise Robust.\n" +
            "Ensemble Large: every algorithm votes the same way (the stereo-difference ones only on stereo files). Slower.\n" +
            "Auto: picks the alignment, runs every algorithm, and keeps the one (or ensemble) that leaves the least backing " +
            "in the stereo difference, preferring the simpler one when they're close. The terminal shows every score. " +
            "Mono files use Ensemble Small.");
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

        public static string KickDuck => L.T("Match Kick Ducking\n" +
            "Some official instrumentals were mastered so the kick pushes the rest of the mix down differently than in the full song. " +
            "After subtraction that leaves a short pumping noise on every kick.\n\n" +
            "This finds the kicks, measures that dip from the stereo difference (left minus right) so a centred vocal never steers it, " +
            "and applies the same dip to the instrumental before the algorithm runs. " +
            "It checks the dip on kicks it wasn't measured on and changes nothing when it doesn't hold up. Needs stereo files.");

        public static string NotNow => L.T("\n\nNot used right now: ");
        public static string BecausePair => L.T("Save is set to Aligned pair, which doesn't extract anything.");
        public static string BecauseFreqOnly => L.T("only the Frequency method uses this.");
        public static string BecauseWaveOnly => L.T("only the Waveform method uses this.");
        public static string BecauseGcc => L.T("this alignment doesn't run v3's analysis.");
        public static string BecauseV3Align => L.T("v3 subtraction always uses v3's analysis. Alignment is used by the other algorithms and by the Aligned pair output.");
        public static string BecauseV3Model => L.T("only the algorithms other than v3 use this.");
        public static string BecauseModel(string name) => L.F("the {0} algorithm fits the level itself.", name);
        public static string BecauseNoSpans(string name) => L.F("the {0} algorithm learns from the whole song, using the stereo difference instead.", name);
        public static string BecauseSplit => L.T("The method is Centre + sides, which only splits the original.");
        public static string BecauseNotSplit => L.T("The method isn't Centre + sides.");
        public static string BecauseRepeats => L.T("The method is FindWithin, which only searches the original.");
        public static string BecauseNotRepeats => L.T("The method isn't FindWithin.");
        public static string Repeats => L.T("FindWithin\n" +
            "Finds passages that come back later in the same song (a chorus, a loop, a riff) and tests each one by cancelling " +
            "the later copy against the earlier one. Only pairs that really cancel are saved, in a _repeats folder: " +
            "_shared is the part of the later copy that the earlier one explains, _difference is what changed " +
            "(an added vocal, harmony or instrument). Together they are exactly the later passage. " +
            "repeats.csv lists every pair that was tested and how well it cancelled.\n\n" +
            "Guide: what the fit listens to. Auto uses the side (left minus right) when the song is wide enough, " +
            "so a centred vocal doesn't steer it. Full mix uses both channels. Side always uses left minus right.\n" +
            "Search: Normal tests the best 24 of 120 candidates. Broad tests 120 of 400 and takes several times longer.\n\n" +
            "The difference is a contrast, not a clean stem: anything the earlier copy had that the later one lacks shows up in it, inverted.");
        public static string Centre => L.T("Centre / Sides Method\n" +
            "How the original is split. The centre file holds what sits in the middle of the stereo image, " +
            "the sides file holds the rest, and the two always add back up to the original.\n\n" +
            "True M/S: plain mid and side. Anything equal in both channels counts as centre.\n" +
            "Phantom: only the part with the same level and phase in both channels.\n" +
            "Coherence: centre judged from steady, averaged channel similarity. Less flicker.\n" +
            "ADRess and DUET: very narrow centre; anything panned even a little goes to the sides.\n" +
            "PCA: the dominant local component, when it points to the centre.\n" +
            "Pretty: the softest blend of several sizes, with the fewest watery artifacts.\n\n" +
            "Two channels can't tell a centred vocal from a centred kick or bass, so the centre file holds everything centred. Needs a stereo file.");
        public static string BecauseAuto => L.T("Auto picks the alignment itself (dense time map on stereo files, Utagoe v3 analysis otherwise).");
        public static string BecauseAutoName => L.T("Automatically Name Output File is off.");
        public static string BecauseGpuOff => L.T("GPU acceleration is off.");
    }

    // main window を最大化して Settings を組み込んだときの button。
    public static string Reset => L.T("Reset");
    public static string ResetTitle => L.T("Reset to defaults?");
    public static string ResetConfirm => L.T("All processing settings go back to their defaults and are saved right away. Your theme and output folder are kept.");
    // 別 window の Settings は OK を押すまで保存しない。
    public static string ResetConfirmOk => L.T("All processing settings go back to their defaults. Nothing is saved until you press OK. Your theme and output folder are kept.");
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
