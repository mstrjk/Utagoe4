// utagoe_core.dll の P/Invoke binding。
// struct layout は C 側と field 単位で一致させる。string は UTF-8。

using System.Runtime.InteropServices;
using System.Text;

namespace Utagoe.Native;

// 値は utagoe::OutputFormat / OutputDepth と同じ。
internal enum OutputFormat { Wav = 0, Aiff = 1, Flac = 2, Alac = 3, Mp3 = 4, Aac = 5, Vorbis = 6, Opus = 7, Wma = 8 }
internal enum OutputDepth { Auto = 0, Int16 = 1, Int24 = 2, Float32 = 3 }

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct CoreSettings
{
    public const int SuffixMax = 64;

    public int ProcMode;
    public int MergeMode;
    public int IntroMode;
    public int LevelAdpt;
    public int AdptMode;
    public int KrkPhase;
    public int SoundQty;

    public int Oversample;
    public int OversampleMul;
    public int BlockSizeMs;
    public int AdptRange;

    public int Centralize;
    public int CentralizePos;

    public int LowPass;
    public int LowPassPos;
    public int HighPass;
    public int HighPassPos;

    public int ExtractLevel;
    public int InstLevel;

    public int SearchInstFile;
    public int AutoNameOutput;
    public fixed byte OutputSuffixBytes[SuffixMax];

    public int OutputFormat;
    public int OutputDepth;
    public int OutputBitrate;

    // GPU。GpuMode は 0 = CPU と同じ結果 (Exact)、1 = 速度優先 (Fastest)。
    public int UseGpu;
    public int GpuMode;
    public int GpuNoticeHidden;

    // By Waveform の代替モデル。WaveModel: 0 v3, 1 Robust, 2 Kalman, 3 Hammerstein, 4 NMF, 5 Spatial, 6 Ensemble。
    // WaveAlign: 0 v3 の block 位置合わせ, 1 相互相関 (GCC)。
    public const int SpansMax = 256;
    public int WaveModel;
    public int WaveAlign;
    public fixed byte FitSpansBytes[SpansMax];

    // 出力するもの。0 声、1 原曲と揃えたインストの組 (_main / _inst の 2 ファイル)。
    public int OutputKind;

    // 出力先の folder (UTF-8)。空なら Music\Utagoe。
    public const int PathMax = 1024;
    public fixed byte OutputFolderBytes[PathMax];

    // app icon の色違いの名前 (UTF-8)。空なら standard。
    public const int NameMax = 32;
    public fixed byte AppIconBytes[NameMax];

    public fixed byte LanguageBytes[NameMax];

    public int OverwriteOutput;

    public int MatchBandwidth;

    public int NormalizeOutput;

    public int MatchLowEnd;
    public int RemoveSubsonic;
    public int FreqModel;
    public int SaveMask;
    public int WaveFft;
    public int CacheSteps;

    public string Language
    {
        get
        {
            fixed (byte* p = LanguageBytes)
            {
                int n = 0;
                while (n < NameMax && p[n] != 0) n++;
                return Encoding.UTF8.GetString(p, n);
            }
        }
        set
        {
            byte[] b = Encoding.UTF8.GetBytes(value ?? "");
            int n = Math.Min(b.Length, NameMax - 1);
            fixed (byte* p = LanguageBytes)
            {
                for (int i = 0; i < NameMax; i++) p[i] = i < n ? b[i] : (byte)0;
            }
        }
    }

    public string AppIcon
    {
        get
        {
            fixed (byte* p = AppIconBytes)
            {
                int n = 0;
                while (n < NameMax && p[n] != 0) n++;
                return Encoding.UTF8.GetString(p, n);
            }
        }
        set
        {
            byte[] b = Encoding.UTF8.GetBytes(value ?? "");
            int n = Math.Min(b.Length, NameMax - 1);
            fixed (byte* p = AppIconBytes)
            {
                for (int i = 0; i < NameMax; i++) p[i] = i < n ? b[i] : (byte)0;
            }
        }
    }

    public string OutputFolder
    {
        get
        {
            fixed (byte* p = OutputFolderBytes)
            {
                int n = 0;
                while (n < PathMax && p[n] != 0) n++;
                return Encoding.UTF8.GetString(p, n);
            }
        }
        set
        {
            byte[] b = Encoding.UTF8.GetBytes(value ?? "");
            int n = Math.Min(b.Length, PathMax - 1);
            while (n > 0 && n < b.Length && (b[n] & 0xC0) == 0x80) n--;
            fixed (byte* p = OutputFolderBytes)
            {
                for (int i = 0; i < PathMax; i++) p[i] = i < n ? b[i] : (byte)0;
            }
        }
    }

    public string FitSpans
    {
        get
        {
            fixed (byte* p = FitSpansBytes)
            {
                int n = 0;
                while (n < SpansMax && p[n] != 0) n++;
                return Encoding.UTF8.GetString(p, n);
            }
        }
        set
        {
            byte[] b = Encoding.UTF8.GetBytes(value ?? "");
            int n = Math.Min(b.Length, SpansMax - 1);
            while (n > 0 && n < b.Length && (b[n] & 0xC0) == 0x80) n--;
            fixed (byte* p = FitSpansBytes)
            {
                for (int i = 0; i < SpansMax; i++) p[i] = i < n ? b[i] : (byte)0;
            }
        }
    }

    public string OutputSuffix
    {
        get
        {
            fixed (byte* p = OutputSuffixBytes)
            {
                int n = 0;
                while (n < SuffixMax && p[n] != 0) n++;
                return Encoding.UTF8.GetString(p, n);
            }
        }
        set
        {
            byte[] b = Encoding.UTF8.GetBytes(value ?? "");
            int n = Math.Min(b.Length, SuffixMax - 1);
            // UTF-8 を切り詰めるとき multi-byte 文字の途中で切らない。
            while (n > 0 && n < b.Length && (b[n] & 0xC0) == 0x80) n--;
            fixed (byte* p = OutputSuffixBytes)
            {
                for (int i = 0; i < SuffixMax; i++) p[i] = i < n ? b[i] : (byte)0;
            }
        }
    }
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct CoreResult
{
    public int Offset;
    public int Inverted;
    public float Gain;
    public double Residual;
    public fixed byte DebugBytes[160];

    public int OutputFormat;
    public int OutputDepth;
    public int OutputRate;
    public int OutputChannels;
    public int InstrumentalRateIn;
    public int InstrumentalChannelsIn;
    public int GpuUsed;
    public fixed byte GpuAdapterBytes[128];
    public fixed byte WrittenBytes[4096];

    public string[] Written
    {
        get
        {
            fixed (byte* p = WrittenBytes)
            {
                int n = 0;
                while (n < 4096 && p[n] != 0) n++;
                return Encoding.UTF8.GetString(p, n).Split('\n', StringSplitOptions.RemoveEmptyEntries);
            }
        }
    }

    public string GpuAdapter
    {
        get
        {
            fixed (byte* p = GpuAdapterBytes)
            {
                int n = 0;
                while (n < 128 && p[n] != 0) n++;
                return Encoding.UTF8.GetString(p, n);
            }
        }
    }

    public string Debug
    {
        get
        {
            fixed (byte* p = DebugBytes)
            {
                int n = 0;
                while (n < 160 && p[n] != 0) n++;
                return Encoding.UTF8.GetString(p, n);
            }
        }
    }
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct CoreAudioInfo
{
    public const int CodecMax = 32;

    public fixed byte CodecBytes[CodecMax];
    public int SampleRate;
    public int Channels;
    public int Bits;           // lossless の量子化 bit 数。lossy は 0
    public int FloatingPoint;
    public int Lossless;
    public int BitrateKbps;
    public long Frames;

    public string Codec
    {
        get
        {
            fixed (byte* p = CodecBytes)
            {
                int n = 0;
                while (n < CodecMax && p[n] != 0) n++;
                return Encoding.UTF8.GetString(p, n);
            }
        }
    }
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct CoreGpuStatus
{
    public int Available;
    public fixed byte AdapterBytes[128];
    public fixed byte ReasonBytes[256];

    public string Adapter { get { fixed (byte* p = AdapterBytes) return Utf8(p, 128); } }
    public string Reason { get { fixed (byte* p = ReasonBytes) return Utf8(p, 256); } }

    private static string Utf8(byte* p, int max)
    {
        int n = 0;
        while (n < max && p[n] != 0) n++;
        return Encoding.UTF8.GetString(p, n);
    }
}

/// GPU の状態。最初の問い合わせで core が GPU を初期化する (数百 ms かかることがある) ので、結果を保持して使い回す。
internal readonly record struct GpuInfo(bool Available, string Adapter, string Reason);

[UnmanagedFunctionPointer(CallingConvention.StdCall)]
internal delegate int ProgressCallback(float fraction, IntPtr user);

internal static class Core
{
    private const string Dll = "utagoe_core.dll";

    [DllImport(Dll, EntryPoint = "utagoe_clear_step_cache")]
    public static extern void ClearStepCache();

    [DllImport(Dll, EntryPoint = "utagoe_default_settings")]
    public static extern void DefaultSettings(out CoreSettings s);

    [DllImport(Dll, EntryPoint = "utagoe_extract_file")]
    private static extern int ExtractFileRaw(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string originalPath,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string instrumentalPath,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string outputPath,
        in CoreSettings settings,
        ProgressCallback? progress,
        IntPtr progressUser,
        out CoreResult result,
        byte[] errorBuf,
        int errorLen);

    [DllImport(Dll, EntryPoint = "utagoe_probe_audio")]
    private static extern int ProbeAudioRaw(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string path,
        out CoreAudioInfo info,
        byte[] errorBuf,
        int errorLen);

    [DllImport(Dll, EntryPoint = "utagoe_format_from_path")]
    private static extern int FormatFromPathRaw([MarshalAs(UnmanagedType.LPUTF8Str)] string path, int fallback);

    [DllImport(Dll, EntryPoint = "utagoe_aligned_pair_paths")]
    private static extern void AlignedPairPathsRaw([MarshalAs(UnmanagedType.LPUTF8Str)] string output,
                                                   byte[] mainBuf, byte[] instBuf, int len);

    [DllImport(Dll, EntryPoint = "utagoe_format_extension")]
    private static extern IntPtr FormatExtensionRaw(int format);

    [DllImport(Dll, EntryPoint = "utagoe_transcode_file")]
    private static extern int TranscodeFileRaw(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string inputPath,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string outputPath,
        int format, int depth, int bitrateKbps,
        byte[] errorBuf, int errorLen);

    [DllImport(Dll, EntryPoint = "utagoe_load_ini")]
    public static extern int LoadIni([MarshalAs(UnmanagedType.LPUTF8Str)] string path, ref CoreSettings s);

    [DllImport(Dll, EntryPoint = "utagoe_save_ini")]
    public static extern int SaveIni([MarshalAs(UnmanagedType.LPUTF8Str)] string path, in CoreSettings s);

    [DllImport(Dll, EntryPoint = "utagoe_gpu_status")]
    private static extern void GpuStatusRaw(out CoreGpuStatus status);

    private static readonly Lazy<GpuInfo> s_gpu = new(() =>
    {
        try
        {
            GpuStatusRaw(out var st);
            return new GpuInfo(st.Available != 0, st.Adapter, st.Reason);
        }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException)
        {
            return new GpuInfo(false, "", ex.Message);
        }
    }, LazyThreadSafetyMode.ExecutionAndPublication);

    /// blocking。初回は UI thread 以外から呼ぶのが望ましい。
    public static GpuInfo Gpu => s_gpu.Value;

    [DllImport(Dll, EntryPoint = "utagoe_check_spans")]
    private static extern int CheckSpansRaw([MarshalAs(UnmanagedType.LPUTF8Str)] string text, byte[] errorBuf, int errorLen);

    public static bool CheckSpans(string text, out string error)
    {
        var buf = new byte[4096];
        int rc = CheckSpansRaw(text, buf, buf.Length);
        error = rc == 0 ? "" : CString(buf);
        return rc == 0;
    }

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    public delegate void LogCallback(int kind, int id, IntPtr text, IntPtr user);

    [DllImport(Dll, EntryPoint = "utagoe_set_log")]
    public static extern void SetLog(LogCallback? fn, IntPtr user);

    [DllImport(Dll, EntryPoint = "utagoe_set_log_dir")]
    public static extern void SetLogDir([MarshalAs(UnmanagedType.LPUTF8Str)] string dir);

    [DllImport(Dll, EntryPoint = "utagoe_build_info")]
    private static extern IntPtr BuildInfoRaw();

    public static string BuildInfo
    {
        get
        {
            try { return Marshal.PtrToStringUTF8(BuildInfoRaw()) ?? ""; }
            catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException) { return ""; }
        }
    }

    [DllImport(Dll, EntryPoint = "utagoe_version")]
    private static extern IntPtr VersionRaw();

    public static string Version => Marshal.PtrToStringUTF8(VersionRaw()) ?? "";

    public static bool IsAvailable
    {
        get
        {
            try { _ = VersionRaw(); return true; }
            catch (DllNotFoundException) { return false; }
            catch (BadImageFormatException) { return false; }
        }
    }

    public static bool TryProbeAudio(string path, out CoreAudioInfo info, out string error)
    {
        var buf = new byte[4096];
        int rc = ProbeAudioRaw(path, out info, buf, buf.Length);
        error = rc == 0 ? "" : CString(buf);
        return rc == 0;
    }

    /// 拡張子から出力形式を推定する。.m4a は fallback が ALAC なら ALAC、それ以外は AAC。
    public static OutputFormat FormatFromPath(string path, OutputFormat fallback) =>
        (OutputFormat)FormatFromPathRaw(path, (int)fallback);

    /// 揃えた組を出力するときに書く 2 つの path (core と同じ規則)。
    public static (string Main, string Inst) AlignedPairPaths(string output)
    {
        var m = new byte[4096];
        var i = new byte[4096];
        AlignedPairPathsRaw(output, m, i, m.Length);
        return (CString(m), CString(i));
    }

    public static string FormatExtension(OutputFormat format) =>
        Marshal.PtrToStringUTF8(FormatExtensionRaw((int)format)) ?? ".wav";

    /// 形式変換。blocking なので長いファイルは worker thread から呼ぶ。
    public static bool TranscodeFile(string input, string output, OutputFormat format, OutputDepth depth,
                                     int bitrateKbps, out string error)
    {
        var buf = new byte[4096];
        int rc = TranscodeFileRaw(input, output, (int)format, (int)depth, bitrateKbps, buf, buf.Length);
        error = rc == 0 ? "" : CString(buf);
        return rc == 0;
    }

    /// full pipeline は blocking。worker thread から呼ぶ。progress が false を返すと cancel。
    public static int ExtractFile(string original, string instrumental, string output,
                                  CoreSettings settings, Func<float, bool>? progress,
                                  out CoreResult result, out string error)
    {
        // native call 中に delegate が GC されないよう、call 完了まで参照を保持する。
        ProgressCallback? cb = progress == null ? null : (f, _) => progress(f) ? 1 : 0;
        var buf = new byte[4096];
        int rc = ExtractFileRaw(original, instrumental, output, settings, cb, IntPtr.Zero,
                                out result, buf, buf.Length);
        GC.KeepAlive(cb);
        error = rc == 0 ? "" : CString(buf);
        return rc;
    }

    private static string CString(byte[] buf)
    {
        int n = Array.IndexOf(buf, (byte)0);
        return Encoding.UTF8.GetString(buf, 0, n < 0 ? buf.Length : n);
    }
}
