// 「By Frequency」方式の抽出エンジン。元実装の ThVocalFFT (0x40c8b8) が持つ処理 0x40ca90 / 0x40ce14 / 0x40d280 に相当する。
// channel ごとに 1 つ持つストリーミングの overlap-add STFT。原曲からインストの spectrum を level 倍して引き、
// インストの方が強く位相も近い bin は 0 にする。

#ifndef UTAGOE_FREQ_ENGINE_H
#define UTAGOE_FREQ_ENGINE_H

#include "fft.h"

#include <vector>

namespace utagoe {

class FreqEngine {
public:
    // mode 0 (SoundQty = Quality) は位相差も見る。mode 1 (Extraction) は振幅だけで判定する。
    // kvol は Extraction Level の値。元実装は 8192 / 8 で呼ぶ。
    void init(int blockSize, int overlap, int mode, float kvol, bool quantize16);

    // 元実装で 1 回の呼び出し (= 1 block 分) を 1 区間として、区間ごとの level を sample ごとに渡す。
    // 結果は元実装のように 1 sample ずつ流した場合と同じ。frame の FFT は並列に計算する。
    // orig と inst は int16 scale。出力は blockSize - hop sample 遅れる (元実装も補償しない)。
    void process(const float* orig, const float* inst, const double* level, double* out, int count);

    // 速度優先のとき frame の処理を GPU で行う。GPU が使えなければ CPU に戻る。
    void setGpu(bool on) { gpu_ = on; }

    int blockSize() const { return n_; }
    int hop() const { return hop_; }
    int mode() const { return mode_; }
    float kvol() const { return kvol_; }
    float cap() const { return mode_ == 0 ? kclamp_ : kvol_; }
    const std::vector<float>& window() const { return window_; }
    const std::vector<float>& threshold() const { return threshold_; }

private:
    struct Scratch {
        std::vector<float> a, b;
    };
    // 1 frame 分の処理。結果は overlap-add に足す項 (拡張精度) を k = hop..n-1 について返す。
    void frame(const float* winA, const float* winB, double level, long double* terms, Scratch& s) const;

    int n_ = 0, overlap_ = 1, hop_ = 0, mode_ = 0;
    float kvol_ = 1.0f, kclamp_ = 1.0f;
    bool quantize_ = true;
    bool gpu_ = false;
    int inCursor_ = 0;

    FftTables tables_;
    std::vector<float> window_, threshold_;
    std::vector<float> hist_[2];
    std::vector<float> acc_;
    std::vector<float> histA_, histB_, timeline_;
    std::vector<long double> terms_;
    std::vector<float> gpuTerms_;
    std::vector<double> gpuLevels_;
};

}

#endif
