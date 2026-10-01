// Utagoe のボーカル抽出コア向け公開 C++ API。
// 既知のインスト音源を原曲から差し引く方式。ブラインド除去ではなく 2 入力が必須で、位置合わせとレベル合わせの精度が結果を左右する。
// Utagoe v3 (Utagoe-v3-full_en.exe / C++Builder 2007) を解析して再構成したもの。

#ifndef UTAGOE_H
#define UTAGOE_H

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace utagoe {

// 出力形式。拡張子と 1 対 1 に対応しないもの (AAC と ALAC はどちらも .m4a) があるため明示的に持つ。
enum class OutputFormat { Wav = 0, Aiff = 1, Flac = 2, Alac = 3, Mp3 = 4, Aac = 5, Vorbis = 6, Opus = 7, Wma = 8 };

// 出力の量子化。Auto は入力の最大 bit depth に合わせ、両入力が 16-bit PCM なら 16-bit になる。
enum class OutputDepth { Auto = 0, Int16 = 1, Int24 = 2, Float32 = 3 };

// 設定は元実装の [V30_Option] に合わせる。トラックバー値は INI と往復できるよう 0..20 の生値を保持する。

enum class ProcMode  { Normal = 0, LRDifference = 1, Mono = 2 };
enum class MergeMode { ByFrequency = 0, ByWaveform = 1 };
enum class IntroMode { Automatic = 0, Normal = 1, Detailed = 2, None = 3 };
enum class LevelAdpt { AutoAveraged = 0, AutoAdaptive = 1, Manual = 2, None = 3 };
enum class AdptMode  { Automatic = 0, Manual = 1 };
enum class KrkPhase  { Automatic = 0, Positive = 1, Inverted = 2 };
enum class SoundQty  { Quality = 0, Extraction = 1 };

// GPU の使い方。Exact は CPU と完全に同じ結果になる処理 (位置探索) だけを GPU に任せる。
// Fastest は level 推定、By Frequency / Centralization の FFT、フィルタも GPU で行う (丸めの違いで結果がわずかに変わる)。
enum class GpuMode   { Exact = 0, Fastest = 1 };

// By Waveform の処理モデル。V3 は元実装の時間領域の減算。それ以外は参照キャンセルの 5 エンジンと合議 (v3 にない拡張)。
enum class WaveModel { V3 = 0, Robust = 1, Kalman = 2, Hammerstein = 3, Nmf = 4, Spatial = 5, Ensemble = 6,
                       Rational = 7, Surface = 8, Trend = 9, Ctf = 10, LowRank = 11, EnsembleLarge = 12, Auto = 13 };
// 代替モデルの位置合わせ。V3Block は元実装の解析と block ごとの drift 探索、Gcc は相互相関による offset + clock drift。
enum class WaveAlign { V3Block = 0, Gcc = 1, Dense = 2 };
// 出力するもの (v3 にない拡張)。Vocal は抽出した声。AlignedPair は原曲と、その時間軸に合わせたインストの 2 ファイル。
enum class OutputKind { Vocal = 0, AlignedPair = 1, CenterSides = 2 };

struct Settings {
    ProcMode  procMode  = ProcMode::Normal;
    MergeMode mergeMode = MergeMode::ByFrequency;
    IntroMode introMode = IntroMode::Automatic;
    LevelAdpt levelAdpt = LevelAdpt::AutoAveraged;
    AdptMode  adptMode  = AdptMode::Automatic;
    KrkPhase  krkPhase  = KrkPhase::Automatic;
    SoundQty  soundQty  = SoundQty::Quality;

    bool oversample    = false;
    int  oversampleMul = 32;
    int  blockSizeMs   = 100;
    int  adptRange     = 3;

    bool centralize    = false;
    int  centralizePos = 6;

    bool lowPass       = false;
    int  lowPassPos    = 10;
    bool highPass      = false;
    int  highPassPos   = 10;

    int  extractLevel  = 6;
    int  instLevel     = 10;

    bool searchInstFile   = true;
    bool autoNameOutput   = true;
    std::string outputSuffix = "_vo";

    // ここから下は v3 にない拡張設定。INI では [Output] section に保存する。
    OutputFormat outputFormat  = OutputFormat::Wav;
    OutputDepth  outputDepth   = OutputDepth::Auto;
    int          outputBitrate = 320;   // lossy 形式の kbps

    // GPU。INI では [GPU] section に保存する。gpuNoticeHidden は「GPU が使えない」通知を出さない設定 (UI が使う)。
    bool    useGpu          = true;
    GpuMode gpuMode         = GpuMode::Exact;
    bool    gpuNoticeHidden = false;

    // By Waveform の代替モデル。INI では [WaveModel] section に保存する。
    // fitSpans は声の無い区間の一覧 (例 "0-10, 2:00-2:08")。空なら曲全体で較正する。
    WaveModel   waveModel = WaveModel::V3;
    WaveAlign   waveAlign = WaveAlign::V3Block;
    std::string fitSpans;
    bool        kickDuck = false;

    // 出力するもの。INI では [Output] section の Kind。AlignedPair の位置合わせは waveAlign に従う (処理方式には依らない)。
    OutputKind  outputKind = OutputKind::Vocal;
    int         centerMethod = 1;

    // 出力先の folder (UI が使う。core の処理には関係しない)。INI では [Output] section の Folder。空なら UI の既定。
    std::string outputFolder;

    // UI の見た目 (core の処理には関係しない)。INI では [UI] section。appIcon は app icon の色違いの名前 (空なら standard)。
    std::string appIcon;
    std::string uiLanguage;

    // 以下の変換は SettingsToParams (0x409c4c) と同じ。x87 の拡張精度で計算して float に格納するので long double で評価する。

    float extractLevelValue() const {
        return extractLevel < 10 ? static_cast<float>(extractLevel * 0.1L + 0.6L)
                                 : static_cast<float>((extractLevel - 9) * 0.3L + 1.5L);
    }

    float instLevelValue() const { return static_cast<float>(instLevel * 0.03L + 0.7L); }

    float centralizeValue() const { return centralizePos * 0.25f + 0.5f; }

    float lowPassHz() const { return static_cast<float>(lowPassPos * 800 + 2000); }

    // 高域カット値の丸めは x87 既定の round-to-nearest-even と同じ。
    int highPassHz() const {
        const float p = static_cast<float>(highPassPos + 1);
        return static_cast<int>(std::nearbyint(p * 1.5f * p + 49.0f));
    }

    // 元実装の INI を読み書きする。キー欠落時は失敗せず既定値に戻す。
    bool load(const std::string& path);
    bool save(const std::string& path) const;
};

// 音声はインターリーブの float。full scale は ±1.0 で、16-bit の s は s / 32768 として正確に表せる。
struct AudioBuffer {
    std::vector<float> samples;
    int sampleRate = 44100;
    int channels   = 2;

    std::size_t frames() const {
        return channels ? samples.size() / static_cast<std::size_t>(channels) : 0;
    }
};

// 入力ファイルの形式情報。表示と出力 bit depth の自動決定に使う。
struct AudioInfo {
    std::string codec;          // "WAV", "FLAC", "MP3", "AAC" など
    int     sampleRate = 0;
    int     channels   = 0;
    int     bits       = 0;     // lossless の量子化 bit 数。lossy は 0
    bool    floatingPoint = false;
    bool    lossless   = true;
    int     bitrateKbps = 0;    // lossy の平均 bitrate。不明なら 0
    int64_t frames     = 0;
};

// path は UTF-8。対応形式: WAV/RF64/W64/AIFF, FLAC, MP3, Ogg Vorbis, Opus。
// Windows では AAC/M4A, ALAC, WMA など Media Foundation が扱える形式も読める。
bool probeAudio(const std::string& path, AudioInfo& info, std::string& error);
bool decodeAudio(const std::string& path, AudioBuffer& out, AudioInfo* info, std::string& error);

struct EncodeOptions {
    OutputFormat format  = OutputFormat::Wav;
    OutputDepth  depth   = OutputDepth::Int16;  // Auto はここに来る前に解決しておく
    int          bitrate = 320;                  // lossy 形式の kbps
};

bool encodeAudio(const std::string& path, const AudioBuffer& in, const EncodeOptions& opt, std::string& error);

// 拡張子から形式を推定する。判別できなければ fallback。.m4a は fallback が Alac なら Alac、それ以外は Aac。
OutputFormat formatFromExtension(const std::string& path, OutputFormat fallback);
const char* extensionOf(OutputFormat f);
bool isLossless(OutputFormat f);

// Auto を具体的な bit depth に解決する。両入力が 16-bit PCM なら Int16、float を含めば Float32、それ以外は Int24。
OutputDepth resolveDepth(OutputDepth requested, OutputFormat format,
                         const AudioInfo& original, const AudioInfo& instrumental);

// sample rate / channel 数をそろえる。rate 変換は Kaiser 窓 sinc。3ch 以上は stereo へ downmix する。
AudioBuffer conformAudio(const AudioBuffer& in, int targetRate, int targetChannels,
                         bool (*progress)(float, void*) = nullptr, void* user = nullptr);

// 位置合わせの結果。元実装の解析 (0x40fdc4) と本処理 (0x410cfc) が決めた値。
struct Alignment {
    int    offset    = 0;       // インストの frame = 原曲の frame + offset
    bool   inverted  = false;
    float  gain      = 1.0f;    // インストに掛けた level。adaptive なら block ごとに推定
    bool   adaptive  = false;
    int    driftBase = 0;       // block (半 block 進むごと) の drift 予測
    int    driftRange = 0;
    double residual  = 0.0;     // 解析時の残差比 voc / org。解析しなかったら 0
    std::string model;          // By Waveform の代替モデルを使ったとき、その診断 (位置合わせ、較正)

    std::string debugString() const;
};

// 進捗 callback は 0.0..1.0。false を返すとキャンセル。
using ProgressFn = bool (*)(float fraction, void* user);

struct ExtractResult {
    AudioBuffer vocal;          // AlignedPair のときは原曲 (stereo 以下にしたもの)
    AudioBuffer alignedInst;    // AlignedPair のときだけ。原曲の時間軸と極性に合わせたインスト (音量は変えない)
    Alignment   alignment;
    int         instrumentalRateIn = 0;      // 変換前のインスト sample rate。原曲と同じなら変換なし
    int         instrumentalChannelsIn = 0;
    bool        cancelled = false;
    std::string error;
    bool        gpuUsed = false;
    std::string gpuAdapter;

    explicit operator bool() const { return error.empty() && !cancelled; }
};

// 全体処理は形式合わせのあと、元実装の ProcessMain (0x410cfc) と同じ block 処理を行う。
// インストは原曲の sample rate / channel 数へ自動で合わせる。
// quantize16 が true のとき、元実装と同じ位置で 16-bit 整数へ丸める。By Waveform は 16-bit 入出力で v3 と同じ数値になる。
// By Frequency と Centralization は FFT の演算順が違うため完全一致ではない。
// 元実装に合わせて、By Frequency / Centralization / LPF / HPF の処理遅延は補償しない。
ExtractResult extract(const AudioBuffer& original,
                      const AudioBuffer& instrumental,
                      const Settings& settings,
                      bool quantize16,
                      ProgressFn progress = nullptr,
                      void* progressUser = nullptr);

// fitSpans の文字列を秒の区間に直す。書式は "開始-終了" を , か ; で区切ったもの。時刻は秒 (小数可) か m:ss / h:mm:ss。
// 読めなければ false と error を返す。
bool parseTimeSpans(const std::string& text, std::vector<std::pair<double, double>>& spans, std::string& error);

// AlignedPair の出力 path。出力欄の path の拡張子の前に _main / _inst を付ける。
void alignedPairPaths(const std::string& output, std::string& mainPath, std::string& instPath);
void outputPaths(const std::string& output, OutputKind kind, std::string& first, std::string& second);
ExtractResult extractCenterSides(const AudioBuffer& original, const Settings& settings, ProgressFn progress = nullptr, void* progressUser = nullptr);

// 2 つの path が同じファイルを指すか。元実装は path 文字列の一致で判定するが、ここでは実体で比べる。
bool sameFile(const std::string& a, const std::string& b);

// 原曲とインストに同じファイルが指定されたときの処理 (元実装の 0x411818)。Centralization / LPF / HPF だけを掛ける。
ExtractResult extractFiltersOnly(const AudioBuffer& original,
                                 const Settings& settings,
                                 bool quantize16,
                                 ProgressFn progress = nullptr,
                                 void* progressUser = nullptr);

}

#endif
