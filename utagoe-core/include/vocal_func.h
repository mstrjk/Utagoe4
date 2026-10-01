// Extraction Centralization のエンジン (元実装の TVocalFunc, 0x402a80 / 0x402d44)。
// ストリーミングの overlap-add STFT。抽出結果の L/R を入れ、左右で共通する (中央に定位する) 成分を残す。
// 元実装は W_Centralize (0x40f748) から 8192 / 8 / Centralization 値で呼び、L/R をその場で書き換える。

#ifndef UTAGOE_VOCAL_FUNC_H
#define UTAGOE_VOCAL_FUNC_H

#include "fft.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace utagoe {

class VocalFunc {
public:
    VocalFunc() = default;

    // blockSize は FFT 長。2 のべき乗が必要。
    // overlap は重なり係数。hop = blockSize / overlap。
    // quantize16 が true なら出力を元実装に合わせて整数値へ丸める。
    void init(int blockSize, int overlap, float extractLevel, bool quantize16 = true);

    // inL / inR は抽出結果の左右。値は int16 相当の scale (full scale = 32768)。out と in は同じ配列でもよい。
    // 出力は blockSize - hop sample 遅れる。accumulator が空から始まるため、先頭は fade-in になる。
    // 結果は 1 sample ずつ流した場合と同じ。frame の FFT は並列に計算する。
    void process(float* outL, float* outR,
                 const float* inL, const float* inR,
                 int count);

    // 速度優先のとき frame の処理を GPU で行う。GPU が使えなければ CPU に戻る。
    void setGpu(bool on) { gpu_ = on; }

    int blockSize() const { return n_; }
    int hop()       const { return hop_; }
    bool ready()    const { return ready_; }

private:
    struct Scratch {
        std::vector<float> a, b;
    };
    // 1 frame 分の処理。overlap-add に足す項を k = hop..n-1 について返す。
    void frame(const float* winL, const float* winR, float* termL, float* termR, Scratch& s) const;
    static float saturate(float v);

    int   n_        = 0;
    int   log2n_    = 0;
    int   hop_      = 0;
    int   overlap_  = 1;
    float kvol_     = 1.0f;
    float kvolClamped_ = 1.0f;
    float slope_    = 0.0f;

    int   inCursor_  = 0;
    bool  ready_     = false;
    bool  quantize16_ = true;
    bool  gpu_ = false;

    FftTables tables_;

    std::vector<float> window_;
    std::vector<float> threshold_;
    std::vector<float> histL_, histR_;
    std::vector<float> accL_, accR_;
    std::vector<float> lineL_, lineR_, tlL_, tlR_, termL_, termR_;
};

}

#endif
