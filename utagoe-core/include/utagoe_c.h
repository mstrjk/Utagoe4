// C# の P/Invoke 用に絞ったフラットな C ABI。
// 境界を跨ぐのは固定レイアウト構造体とスカラーだけ。DSP と WAV/INI 処理は DLL 側へ寄せる。
// 文字列はすべて UTF-8。パスには Unicode 文字を含められる。

#ifndef UTAGOE_C_H
#define UTAGOE_C_H

#include <stdint.h>

#if defined(_WIN32)
#  if defined(UTAGOE_BUILD_DLL)
#    define UTAGOE_API __declspec(dllexport)
#  else
#    define UTAGOE_API __declspec(dllimport)
#  endif
#  define UTAGOE_CALL __stdcall
#else
#  define UTAGOE_API
#  define UTAGOE_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define UTAGOE_SUFFIX_MAX 64

// [V30_Option] の全キーを保持し、トラックバー項目は INI と同じ 0..20 の生値。
// C# 側も Sequential layout で同じ順序にすること。
#define UTAGOE_SPANS_MAX 256
#define UTAGOE_PATH_MAX 1024
#define UTAGOE_NAME_MAX 32

typedef struct UtagoeSettings {
    int32_t procMode;
    int32_t mergeMode;
    int32_t introMode;
    int32_t levelAdpt;
    int32_t adptMode;
    int32_t krkPhase;
    int32_t soundQty;

    int32_t oversample;
    int32_t oversampleMul;
    int32_t blockSizeMs;
    int32_t adptRange;

    int32_t centralize;
    int32_t centralizePos;

    int32_t lowPass;
    int32_t lowPassPos;
    int32_t highPass;
    int32_t highPassPos;

    int32_t extractLevel;
    int32_t instLevel;

    int32_t searchInstFile;
    int32_t autoNameOutput;
    char    outputSuffix[UTAGOE_SUFFIX_MAX];

    // ここから下は v3 にない出力設定。値は utagoe::OutputFormat / OutputDepth と同じ。
    int32_t outputFormat;    // 出力形式は 0=WAV、1=AIFF、2=FLAC、3=ALAC、4=MP3、5=AAC、6=Ogg Vorbis、7=Opus、8=WMA。
    int32_t outputDepth;     // bit depth は 0=Auto、1=16-bit、2=24-bit、3=32-bit float。
    int32_t outputBitrate;   // lossy 形式の kbps

    // GPU。gpuMode は 0 = CPU と同じ結果 (Exact)、1 = 速度優先 (Fastest)。
    int32_t useGpu;
    int32_t gpuMode;
    int32_t gpuNoticeHidden;

    // By Waveform の代替モデル。waveModel: 0 v3, 1 Robust, 2 Kalman, 3 Hammerstein, 4 NMF, 5 Spatial, 6 Ensemble。
    // waveAlign: 0 v3 の block 位置合わせ, 1 GCC。fitSpans: 声の無い区間 (例 "0-10, 2:00-2:08")。
    int32_t waveModel;
    int32_t waveAlign;
    char    fitSpans[UTAGOE_SPANS_MAX];

    // 出力するもの。0 抽出した声、1 原曲とその時間軸に合わせたインストの組 (outputPath の拡張子の前に _main / _inst を付けた 2 ファイル)。
    int32_t outputKind;

    // 出力先の folder (UTF-8)。UI が使う。空なら UI の既定 (Music\Utagoe)。
    char    outputFolder[UTAGOE_PATH_MAX];

    // app icon の色違いの名前 (UTF-8、例 "standard")。UI が使う。
    char    appIcon[UTAGOE_NAME_MAX];

    int32_t centerMethod;

    char    uiLanguage[UTAGOE_NAME_MAX];

    int32_t repeatGuide;
    int32_t repeatBreadth;

    int32_t overwriteOutput;

    int32_t matchBandwidth;

    int32_t upmixMethod;
    int32_t upmixSevenOne;
    int32_t upmixLfe;

    int32_t normalizeOutput;

    int32_t matchLowEnd;
    int32_t removeSubsonic;
    int32_t freqModel;
    int32_t saveMask;
} UtagoeSettings;

// debug には元実装のデバッグ表示と同じ形式の行を入れる。
typedef struct UtagoeResult {
    int32_t offset;
    int32_t inverted;
    float   gain;
    double  residual;
    char    debug[160];

    int32_t outputFormat;          // 実際に書いた形式 (拡張子から決まる)
    int32_t outputDepth;           // Auto を解決した後の bit depth
    int32_t outputRate;
    int32_t outputChannels;
    int32_t instrumentalRateIn;    // 変換前のインスト形式。outputRate と違えば rate 変換した
    int32_t instrumentalChannelsIn;
    int32_t gpuUsed;
    char    gpuAdapter[128];
    char    written[4096];
} UtagoeResult;

// GPU の利用可否。available が 0 なら reason に理由が入る (英語、UTF-8)。
typedef struct UtagoeGpuStatus {
    int32_t available;
    char    adapter[128];
    char    reason[256];
} UtagoeGpuStatus;

#define UTAGOE_CODEC_MAX 32

// 入力ファイルの形式情報。bits は lossless の量子化 bit 数で、lossy は 0。
typedef struct UtagoeAudioInfo {
    char    codec[UTAGOE_CODEC_MAX];
    int32_t sampleRate;
    int32_t channels;
    int32_t bits;
    int32_t floatingPoint;
    int32_t lossless;
    int32_t bitrateKbps;
    int64_t frames;
} UtagoeAudioInfo;

// 既定値は元実装の初期化処理と Reset が使う値に合わせる。
UTAGOE_API void UTAGOE_CALL utagoe_default_settings(UtagoeSettings* s);

// 進捗 callback は 0..1。0 でキャンセル、それ以外で続行。
typedef int32_t (UTAGOE_CALL *UtagoeProgress)(float fraction, void* user);

// 0 が成功。失敗時は非 0 と errorBuf のメッセージを返す。
// 入力は対応形式なら何でもよく、形式・sample rate・channel 数が違ってもよい。
// 出力形式は outputPath の拡張子で決まる。判別できない拡張子なら settings->outputFormat を使う。
UTAGOE_API int32_t UTAGOE_CALL utagoe_extract_file(
    const char* originalPath,
    const char* instrumentalPath,
    const char* outputPath,
    const UtagoeSettings* settings,
    UtagoeProgress progress,
    void* progressUser,
    UtagoeResult* result,
    char* errorBuf,
    int32_t errorLen);

// outputKind = 1 のときに書く 2 つの path。len は各 buffer の大きさ (byte)。
UTAGOE_API void UTAGOE_CALL utagoe_aligned_pair_paths(const char* outputPath, char* mainBuf, char* instBuf, int32_t len);
UTAGOE_API void UTAGOE_CALL utagoe_output_paths(const char* outputPath, int32_t kind, char* firstBuf, char* secondBuf, int32_t len);

// 形式情報だけを読む。音声全体は展開しない (MP3 は長さを数えるため全体を走査する)。
UTAGOE_API int32_t UTAGOE_CALL utagoe_probe_audio(const char* path, UtagoeAudioInfo* info,
                                                  char* errorBuf, int32_t errorLen);

// 拡張子から出力形式を推定する。.m4a は fallback が ALAC なら ALAC、それ以外は AAC。
UTAGOE_API int32_t UTAGOE_CALL utagoe_format_from_path(const char* path, int32_t fallback);

// 出力形式の標準拡張子 (".flac" など)。
UTAGOE_API const char* UTAGOE_CALL utagoe_format_extension(int32_t format);

// 対応形式のファイルを別形式へ変換する。format が負なら outputPath の拡張子で決める。
// UI の再生で、MCI が直接開けない形式を一時 WAV にするために使う。0 が成功。
UTAGOE_API int32_t UTAGOE_CALL utagoe_transcode_file(const char* inputPath, const char* outputPath,
                                                     int32_t format, int32_t depth, int32_t bitrateKbps,
                                                     char* errorBuf, int32_t errorLen);

// INI 読み込みは現在の設定値を起点にし、欠落キーは既存値を残す。
UTAGOE_API int32_t UTAGOE_CALL utagoe_load_ini(const char* path, UtagoeSettings* s);
UTAGOE_API int32_t UTAGOE_CALL utagoe_save_ini(const char* path, const UtagoeSettings* s);

// 初回は GPU の初期化 (数百 ms かかることがある) を行い、結果を覚えておく。
UTAGOE_API void UTAGOE_CALL utagoe_gpu_status(UtagoeGpuStatus* status);

// 声の無い区間の文字列 (例 "0-10, 2:00-2:08") を検査する。0 なら正しい。読めなければ非 0 と errorBuf の説明を返す。
UTAGOE_API int32_t UTAGOE_CALL utagoe_check_spans(const char* text, char* errorBuf, int32_t errorLen);

// 処理の記録。terminal に流すための行を callback で受け取る。text は UTF-8。
// kind: UTAGOE_LOG_LINE 普通の行, STATUS 同じ id の行を上書きする進捗, STAGE_BEGIN / STAGE_END 段階の開始と終了
// (END の id は所要 ms、失敗なら -1 - ms), WARNING, ERROR (stack trace つき), DETAIL 細かい情報。
// callback はどの thread からも呼ばれる。
#define UTAGOE_LOG_LINE        0
#define UTAGOE_LOG_STATUS      1
#define UTAGOE_LOG_STAGE_BEGIN 2
#define UTAGOE_LOG_STAGE_END   3
#define UTAGOE_LOG_WARNING     4
#define UTAGOE_LOG_ERROR       5
#define UTAGOE_LOG_DETAIL      6
typedef void (UTAGOE_CALL *UtagoeLog)(int32_t kind, int32_t id, const char* text, void* user);
UTAGOE_API void UTAGOE_CALL utagoe_set_log(UtagoeLog fn, void* user);

// crash 記録 (この DLL 群の中のアクセス違反など) を書く folder。UTF-8。
UTAGOE_API void UTAGOE_CALL utagoe_set_log_dir(const char* dir);

// build と同梱ライブラリの版。複数行の UTF-8。
UTAGOE_API const char* UTAGOE_CALL utagoe_build_info(void);

UTAGOE_API const char* UTAGOE_CALL utagoe_version(void);

#ifdef __cplusplus
}
#endif

#endif
