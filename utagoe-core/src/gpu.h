// GPU (Direct3D 11 compute) による高速化。使えない環境ではすべて CPU で処理する。
// 各関数は GPU で処理できたら true を返し、false なら呼び出し側が CPU で同じ処理をする。
// 途中で device が失われた場合 (driver の更新や TDR) も false を返し、以後は CPU に切り替わる。
// 呼び出しは内部で直列化する。

#ifndef UTAGOE_GPU_H
#define UTAGOE_GPU_H

#include <complex>
#include <cstdint>
#include <string>
#include <vector>

namespace utagoe {
namespace gpu {

// 利用可否。最初の呼び出しで device を作り、結果を覚えておく。
struct Status {
    bool available = false;
    std::string adapter;      // 使う GPU の名前 (UTF-8)
    std::string reason;       // 使えない理由 (available が false のとき)
};
Status status();

// block ごとに setBlock で原曲 (count 個) とインスト (instLen 個) を整数で渡してから呼ぶ。
// インストは ovs 倍の仮想格子上の線形補間 (最近接偶数丸め) として参照する。ovs は 2 のべき乗、1 なら補間なし。
bool setBlock(const int32_t* const* orig, int channels, int count, const int32_t* const* inst, int instLen);

// 候補位置 pos ごとに sum_c sum_i |d_i| + |d_i - d_{i-1}| (d = 原曲 - インスト[pos + stride*i])。
// 整数演算なので CPU と完全に一致する。
bool searchScores(int ovs, int stride, const int32_t* candidates, int count, std::vector<uint64_t>& scores);

// 以下は「速度優先」のときだけ使う (浮動小数点の丸めが CPU と異なる)

// 候補 g ごとに sum_c sum_i |o - in[pos + stride*i] * g| (値は整数を scale で割ったもの。quantize なら各項を整数に丸める)。
bool levelScores(int ovs, int stride, int pos, float scale, bool quantize,
                 const float* gains, int count, std::vector<double>& scores);

// By Frequency の frame をまとめて処理する。frame f は lineA/lineB の f*hop から n 個を使う。
// terms[f*n + k] に overlap-add で足す項を返す。levels は frame ごとの level (上限で抑え済み)。
bool freqFrames(const float* lineA, const float* lineB, int frames, int n, int hop, int overlap,
                int mode, float gate, const float* window, const float* threshold,
                const double* levels, float* terms);

// Centralization の frame をまとめて処理する。
bool centralizeFrames(const float* lineL, const float* lineR, int frames, int n, int hop, int overlap,
                      float kvol, float slope, const float* window, const float* threshold,
                      float* termsL, float* termsR);

// FIR 畳み込み。out[i] = sum_j x[taps-1+i-j] * h[j] (x は遅延線 taps-1 個 + 入力 count 個)。
bool firConvolve(const float* x, int count, const float* h, int taps, float* out);

void allow(bool on);
bool fftBatch(std::complex<double>* const* arrays, int count, int n, bool inverse, const std::complex<double>* table);
bool allowed();

struct AnchorSetup {
    int len = 0, n = 0, m = 0, half = 0, lags = 0, lagStart = 0, bigN = 0;
    const std::complex<double>* chirp = nullptr;
    const std::complex<double>* kernel = nullptr;
    const std::complex<double>* twForward = nullptr;
    const std::complex<double>* twInverse = nullptr;
    const std::complex<double>* lagTwiddle = nullptr;
};
bool anchorScan(const AnchorSetup& s, const std::vector<const double*>& ys, const std::vector<const double*>& xs,
                std::vector<double>& acc, std::vector<double>& c0);

}
}

#endif
