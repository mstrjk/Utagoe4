// 元実装の処理本体 (0x40c400..0x412500) を写した内部 API。
// 元実装は WAV を mmio stream で読みながら block 単位で処理する。ここでは decode 済みの buffer を同じ stream 操作で読む。
// 値はすべて int16 scale。quantize が true のときは元実装と同じ位置で整数へ丸め、false のときは丸めずに float のまま流す。

#ifndef UTAGOE_ENGINE_H
#define UTAGOE_ENGINE_H

#include "utagoe.h"
#include "fir.h"
#include "freq_engine.h"
#include "vocal_func.h"
#include "x87.h"

#include <vector>

namespace utagoe {
namespace v3 {

// SettingsToParams (0x409c4c) が作る実行時の値。
struct Params {
    int    procMode   = 0;     // 0 = 通常、1 = L/R Difference、2 = Mono。
    int    mergeMode  = 0;     // 方式は 0 = By Frequency、1 = By Waveform。
    bool   oversample = false;
    int    ovsMul     = 1;
    int    blockMs    = 100;
    double blockSec   = 0.1;
    int    adptMode   = 0;
    int    driftRange = 0;     // 計算式は round(blockMs * 0.01 * AdptNum)。
    bool   centralize = false;
    float  cntr       = 2.0f;
    int    introMode  = 0;
    int    levelAdpt  = 0;
    bool   lowPass    = false;
    bool   highPass   = false;
    int    lowPassHz  = 0;
    int    highPassHz = 0;
    float  kvol       = 1.0f;
    float  klvl       = 1.0f;
    int    krkPhase   = 0;
    int    soundQty   = 0;
};

// ProcessEntry (0x40fbb4) と同じく、mono 入力では L/R Difference と Centralization を無効にする。
Params toParams(const Settings& s, int channels);

// mmio stream 相当。seek / tell は frame 単位。範囲外は 0 として読む (元実装は範囲外で header や前回の残りを読む)。
struct Source {
    const float* data = nullptr;
    long long frames = 0;
    int channels = 2;
    long long pos = 0;

    void seek(long long f) { pos = f; }
    long long tell() const { return pos; }
    // dst には count frame を書く。戻り値は実際に読めた frame 数 (元実装の mmioRead の戻り値に相当)。
    int read(float* dst, int count);
};

struct Sink {
    std::vector<float> data;
    int channels = 2;
    void write(const float* src, int frames);
};

// 解析結果。元実装では global の ctx (0x4cd934) に置かれる。
struct Context {
    int channels = 2;
    int rate = 44100;
    bool quantize = true;
    Params p;
    Source orig, inst;
    Source analysisInst;
    bool hasAnalysisInst = false;
    Sink out;

    // ScoreOffsets (0x410570) の出力
    double vol = 1.0, ofs = 0.0, voc = 0.0, org = 0.0, bnsn = 0.0;
    // IntroAnalysis (0x40fdc4) の出力
    int  baseOfs = 0;          // ctx+0x40 block ごとの drift 予測
    int  range = 0;            // ctx+0x44 drift 探索幅
    int  offset = 0;           // ctx+0x48 初期オフセット
    bool phase = false;        // ctx+0x58 true なら極性反転
    bool analysed = false;

    // processMain が実際に使った値 (結果表示用)
    int    usedOffset = 0;
    bool   usedPhase = false;
    double usedLevel = 1.0;
    bool   adaptiveLevel = false;
    int    usedBase = 0;
    int    usedRange = 0;

    // GPU。gpuSearch は位置探索 (CPU と同じ結果)、gpuFast は速度優先の処理にも使う。
    // gpuMinWork より小さい探索は転送の待ち時間の方が長いので CPU で行う。
    // 代替モデル用。true なら本処理は減算せず、原曲と位置合わせ済みのインストを記録する (int16 scale, interleaved)。
    bool collect = false;
    std::vector<float> colMix, colRef;
    bool tapRaw = false;
    std::vector<float> rawTap;
    std::vector<char> colValid;

    bool gpuSearch = false;
    bool gpuFast = false;
    long long gpuMinWork = 300000;

    ProgressFn progress = nullptr;
    void* progressUser = nullptr;
    float progressFrom = 0.0f, progressTo = 1.0f;
    bool cancelled = false;

    // Application->ProcessMessages とキャンセル確認の代わり。fraction はこの段階内の進捗。
    bool poll(double fraction);
};

// 元実装の作業 object (0x1c0 byte, ctor 0x40ddec, 初期化 0x40e150)。
class Worker {
public:
    // blockSec 秒を 1 block とし、N = round(rate * blockSec)、インスト側は M = round(N * 1.5)。
    void init(int channels, int rate, double blockSec, int ovs, bool quantize);
    void setPhase(bool inverted) { phaseSign_ = inverted ? -1.0 : 1.0; }
    void setGpu(const Context& c) {
        gpuSearch_ = c.gpuSearch;
        gpuFast_ = c.gpuFast;
        gpuMinWork_ = c.gpuMinWork;
    }

    int n() const { return n_; }
    int m() const { return m_; }
    int ovs() const { return ovs_; }

    // 読み込み用の raw buffer (interleaved)。
    float* rawOrig() { return rawOrig_.data(); }
    float* rawInst() { return rawInst_.data(); }

    void reset();                  // 元実装の対応箇所: 0x40e59c
    void prepare();                // 元実装の対応箇所: 0x40e668
    void toLRDiff();               // 元実装の対応箇所: 0x40e820
    void toMono();                 // 元実装の対応箇所: 0x40e87c
    void upsample();               // 元実装の対応箇所: 0x40e8d8
    void upsampleMix();            // 元実装の対応箇所: 0x40e980

    // 探索。戻り値はインスト buffer 内の位置。
    int searchCh(int base, int range, int step);                    // 元実装の対応箇所: 0x40ecf0
    int searchMix(int base, int range, int step, int count);        // 元実装の対応箇所: 0x40ed2c / 0x40ed5c
    int searchOvsCh(int base, int range, int step);                 // 元実装の対応箇所: 0x40efac
    int searchOvsMix(int base, int range, int step);                // 元実装の対応箇所: 0x40efe0
    int searchMixAbs(int base, int range, int step);                // 元実装の対応箇所: 0x40e9f4

    double levelFitCh(int pos, double step, double maxDelta, double center);      // 元実装の対応箇所: 0x40f1b8
    double levelFitMix(int pos, double step, double maxDelta, double center);     // 元実装の対応箇所: 0x40f1f8
    double levelFitOvsCh(int pos, double step, double maxDelta, double center);   // 元実装の対応箇所: 0x40f32c
    double levelFitOvsMix(int pos, double step, double maxDelta, double center);  // 元実装の対応箇所: 0x40f36c

    void sub(int pos, double g);                                    // 元実装の対応箇所: 0x40f3a0
    void subMono(int pos, double g);                                // 元実装の対応箇所: 0x40f410
    void subOvs(int pos, double g);                                 // 元実装の対応箇所: 0x40f498
    void subOvsMono(int pos, double g);                             // 元実装の対応箇所: 0x40f514
    // By Frequency の mono 処理 (0x40f638) の前半。左 channel を左右の平均で置き換える (整数除算は 0 方向へ切り捨て)。
    void monoAverage(int start, int count, int pos);

    // 処理済みの block を Pipeline へ渡すための直接アクセス。
    const double* orig(int c) const { return o_[c].data(); }
    const double* inst(int c) const { return i_[c].data(); }
    const double* out(int c) const { return out_[c].data(); }
    const double* instOvs(int c) const { return io_[c].data(); }

    double sumAbsOut(int from, int to) const;
    double sumAbsOrig(int from, int to) const;
    // DetailOffset 用の直接アクセス。
    std::vector<double>& mixOrig() { return om_; }
    std::vector<double>& mixInst() { return im_; }
    void setN(int n) { n_ = n; }

private:
    // quantize のときは拡張精度の値から直接整数へ丸める (double を経由すると二重丸めになる)。
    double q(long double v) const { return static_cast<double>(quantize_ ? rint87(v) : v); }
    int searchCore(const double* const* o, const double* const* in, int nch, int count,
                   int base, int range, int step, int stride, bool mix);
    double levelCore(const double* const* o, const double* const* in, int nch, int pos,
                     double step, double maxDelta, double center, bool mix);
    double levelOvsCore(const double* const* o, const double* const* in, int nch, int pos,
                        double step, double maxDelta, double center, bool mix);
    // GPU に今の block を置く。mix なら mix 系列、そうでなければ channel ごとの系列。count は原曲側の長さ。
    bool gpuBlock(bool mix, int count);
    bool gpuLevel(bool mix, int pos, int stride, double& g);
    // 整数化の倍率。16-bit 経路は 1、それ以外は 1/256 sample 単位 (値はこの格子に乗っている)。
    double scale() const { return quantize_ ? 1.0 : 256.0; }
    void subCore(const double* i0, const double* i1, int pos, int stride, double g);
    void subMonoCore(const double* i0, const double* i1, int pos, int stride, double g);

    int ch_ = 2, rate_ = 44100, ovs_ = 1, n_ = 0, m_ = 0;
    bool quantize_ = true;
    double phaseSign_ = 1.0;

    std::vector<float> rawOrig_, rawInst_;
    std::vector<double> o_[2], i_[2], io_[2], out_[2];
    std::vector<double> om_, im_, imo_;
    std::vector<int> cand_;
    std::vector<double> gains_, scores_;

    bool gpuSearch_ = false, gpuFast_ = false;
    long long gpuMinWork_ = 1000000;
    int gpuKind_ = 0;        // 0 = なし、1 = channel 系列、2 = mix 系列。
    int gpuCount_ = 0;
    std::vector<int32_t> gpuInt_[4];
    std::vector<int32_t> gpuCand_;
    std::vector<uint64_t> gpuScores_;
    std::vector<float> gpuGains_;
};

// 書き出し側の流れ: By Frequency エンジン -> Centralization (0x40f748) -> LPF (0x40f78c) -> HPF (0x40f7fc) -> soft clip (0x40e728)。
// 元実装はこれを block ごとに、書き出す範囲 (block の中央半分など) だけに掛けている。どれも状態を持つ stream 処理で、
// 入力はその範囲を順につないだ列そのものなので、まとめて処理しても結果は同じ。ここではある程度ためてから一括で流す。
class Pipeline {
public:
    void init(Context& c, bool freqEngine);

    // By Waveform / 後処理だけの経路: 減算済みの値を渡す。
    void pushOutput(const double* l, const double* r, int count);
    // By Frequency: エンジンの入力 (原曲 / インスト) と、その block の level を渡す。mono なら左だけを使う。
    void pushEngine(const double* const* orig, const double* const* inst, int count, double level);
    // ためた分を処理して Context の出力へ書く。最後に必ず呼ぶ。
    void flush();

private:
    void maybeFlush();

    Context* c_ = nullptr;
    int ch_ = 2;
    bool quantize_ = true;
    bool freq_ = false, mono_ = false;
    bool hasCntr_ = false, hasLpf_ = false, hasHpf_ = false;
    FreqEngine fe_[2];
    VocalFunc vf_;
    FirFilter lpf_[2], hpf_[2];

    std::vector<float> ea_[2], eb_[2];
    std::vector<double> level_;
    std::vector<double> pre_[2];
    std::vector<double> tmp_;
    std::vector<float> fl_, fr_, raw_;
};

// 解析 (0x40fdc4 系)。
int  scoreOffsets(Context& c, int ofs, int base, bool final, bool phase, int secs, int range);
int  simpleOffset(Context& c, bool phase);                // 元実装の対応箇所: 0x411a7c
int  detailOffset(Context& c, bool mono, bool phase);     // 元実装の対応箇所: 0x411cd4
int  decideInitialOffset(Context& c);                     // 元実装の対応箇所: 0x4102c4
int  introAnalysis(Context& c);                           // 元実装の対応箇所: 0x40fdc4

// 本処理。戻り値は元実装に合わせて 0 成功、1 メモリ確保失敗、2 書き込み失敗。
int processMain(Context& c);                              // 元実装の対応箇所: 0x410cfc
int processAlt(Context& c);                               // 元実装の対応箇所: 0x411818

}
}

#endif
