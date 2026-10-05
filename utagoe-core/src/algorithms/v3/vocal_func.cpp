// Extraction Centralization のエンジン。
// TVocalFunc の主要処理を Utagoe v3 から直接対応付けたもの。
// 定数はバイナリから読み出し、各使用箇所に address を残して検算できるようにしている。
// 左右を bin ごとに比較し、位相が近い bin (中央に定位する成分) を残し、違う bin を抑える。単純な M/S ではなく phase-coherence gate。

#include "vocal_func.h"
#include "mathconst.h"
#include "x87.h"
#include "parallel.h"
#include "gpu.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace utagoe {
namespace {

constexpr float kPiF = static_cast<float>(kPi);

// 位相差は [0, PI] に wrap する。
inline double phaseDiff(double a, double b) {
    double d = std::fabs(b - a);
    if (d > kPiF) d = 2.0 * kPiF - d;
    return d;
}

}

void VocalFunc::init(int blockSize, int overlap, float extractLevel, bool quantize16) {
    quantize16_ = quantize16;
    n_       = blockSize;
    overlap_ = overlap > 0 ? overlap : 1;
    hop_     = n_ / overlap_;
    kvol_    = extractLevel;

    kvolClamped_ = std::min(kvol_, 1.0f);

    // slope = 0.4 - 0.1*kvol。0 未満にはしない。
    slope_ = 0.4f - 0.1f * kvol_;
    if (slope_ < 0.0f) slope_ = 0.0f;

    log2n_ = 0;
    for (int s = 1; s < n_; s += s) ++log2n_;

    tables_.resize(static_cast<std::size_t>(n_));

    // 履歴は未処理の入力 (最初は n - hop 個の 0)、acc は overlap-add の途中結果。
    histL_.assign(static_cast<std::size_t>(n_ - hop_), 0.0f);
    histR_.assign(static_cast<std::size_t>(n_ - hop_), 0.0f);
    accL_.assign(static_cast<std::size_t>(n_), 0.0f);
    accR_.assign(static_cast<std::size_t>(n_), 0.0f);

    // 周波数ごとの phase threshold table を作る。高域ほど許容幅が広く、kvol で全体の厳しさを調整する。
    threshold_.assign(static_cast<std::size_t>(n_ / 2), 0.0f);
    for (int i = 0; i < n_ / 2; ++i) {
        const float t   = static_cast<float>(n_ / 2) / (static_cast<float>(i) + 1.0f);
        const float thr = kPiF / t;
        threshold_[static_cast<std::size_t>(i)] =
            (thr * 0.5f + 1.0f) / kvol_;
    }

    // Hann window は win[i] = cos(2*PI*i/n) * -0.5 + 0.5。
    window_.assign(static_cast<std::size_t>(n_), 0.0f);
    for (int i = 0; i < n_; ++i) {
        const double a = 2.0 * static_cast<double>(kPiF) * i / n_;
        window_[static_cast<std::size_t>(i)] =
            static_cast<float>(std::cos(a)) * -0.5f + 0.5f;
    }

    inCursor_  = n_ - hop_;
    ready_     = true;
}

void VocalFunc::frame(const float* winL, const float* winR, float* termL, float* termR, Scratch& s) const {
    const std::size_t n = static_cast<std::size_t>(n_);
    s.a.resize(2 * n);
    s.b.resize(2 * n);
    std::vector<float>& specA = s.a;
    std::vector<float>& specB = s.b;

    // 入力へ window を掛け、実数値を複素配列の偶数 slot に詰める。
    for (std::size_t i = 0; i < n; ++i) {
        specA[2 * i]     = winL[i] * window_[i];
        specA[2 * i + 1] = 0.0f;
        specB[2 * i]     = winR[i] * window_[i];
        specB[2 * i + 1] = 0.0f;
    }

    fft_forward(specA.data(), n, tables_);
    fft_forward(specB.data(), n, tables_);

    // 下半分の spectrum だけを bin 単位で比較する。
    for (std::size_t i = 0; i < n / 2; ++i) {
        const float ar = specA[2 * i], ai = specA[2 * i + 1];
        const float br = specB[2 * i], bi = specB[2 * i + 1];

        // magnitude は hypot ではなく sqrt(re^2 + im^2)。元実装に合わせる。
        const double magA = std::sqrt(static_cast<double>(ar) * ar +
                                      static_cast<double>(ai) * ai);
        const double magB = std::sqrt(static_cast<double>(br) * br +
                                      static_cast<double>(bi) * bi);

        // 元実装は atan2(re, im) の順で呼ぶ。通常と逆だが位相差しか使わないため結果には影響しない。ここもそのまま再現する。
        const double phA = (ar == 0.0f && ai == 0.0f)
                               ? 0.0 : std::atan2(static_cast<double>(ar),
                                                  static_cast<double>(ai));
        const double phB = (br == 0.0f && bi == 0.0f)
                               ? 0.0 : std::atan2(static_cast<double>(br),
                                                  static_cast<double>(bi));

        // 両方とも無音なら両 spectrum の該当 bin を 0 にする。
        if (magA <= 0.0 && magB <= 0.0) {
            specA[2 * i] = specA[2 * i + 1] = 0.0f;
            specB[2 * i] = specB[2 * i + 1] = 0.0f;
            continue;
        }

        // 片方だけ 0 の bin は level match の比が無限大になり、元実装では 0 * inf = NaN が block 全体へ広がる
        // (16-bit 出力では -32768 が続く full-scale のノイズになる)。ここは意図的に直している。
        // インスト側が 0 ならその成分はボーカルだけなので残し、原曲側が 0 なら残すものがないので 0 にする。
        if (magB <= 0.0) {
            specB[2 * i] = specB[2 * i + 1] = 0.0f;
            continue;
        }
        if (magA <= 0.0) {
            specA[2 * i] = specA[2 * i + 1] = 0.0f;
            specB[2 * i] = specB[2 * i + 1] = 0.0f;
            continue;
        }

        // 小さい側の spectrum を大きい側へ寄せて level match する。slope で補正を緩める。
        const double d = std::fabs(magA - magB);
        if (magA > magB) {
            const double scale = (d * slope_ + magA) / magB;
            specB[2 * i]     = static_cast<float>(specB[2 * i] * scale);
            specB[2 * i + 1] = static_cast<float>(specB[2 * i + 1] * scale);
        } else {
            const double scale = (d * slope_ + magB) / magA;
            specA[2 * i]     = static_cast<float>(specA[2 * i] * scale);
            specA[2 * i + 1] = static_cast<float>(specA[2 * i + 1] * scale);
        }

        // 位相差に応じて gate を掛ける。
        const double dPhase = phaseDiff(phA, phB);
        const double thr    = threshold_[i];
        if (dPhase > thr) {
            // 抑圧は raised-cosine で滑らかにし、PI で最大になる。
            const double x = (dPhase - thr) * 2.0f * kvol_;
            const double gain = (x < kPiF)
                                    ? (std::cos(x) * 0.5 + 0.5)
                                    : 0.0;
            specA[2 * i]     = static_cast<float>(specA[2 * i] * gain);
            specA[2 * i + 1] = static_cast<float>(specA[2 * i + 1] * gain);
            specB[2 * i]     = static_cast<float>(specB[2 * i] * gain);
            specB[2 * i + 1] = static_cast<float>(specB[2 * i + 1] * gain);
        }
    }

    // Hermitian mirror 側は使わないので上半分を 0 にする。
    std::fill(specA.begin() + static_cast<std::ptrdiff_t>(n), specA.end(), 0.0f);
    std::fill(specB.begin() + static_cast<std::ptrdiff_t>(n), specB.end(), 0.0f);

    fft_inverse(specA.data(), n, tables_);
    fft_inverse(specB.data(), n, tables_);

    // overlap-add で出力 accumulator へ足し込む。make-up gain は Hann^2 用で、overlap 除算まで含めて正規化する。
    const float kMakeup = 5.333330154418945f;
    // 先頭 hop 個は出力済みの位置に当たるので使われない。
    for (std::size_t i = static_cast<std::size_t>(hop_); i < n; ++i) {
        termL[i] = kMakeup * specA[2 * i] * window_[i] / static_cast<float>(overlap_);
        termR[i] = kMakeup * specB[2 * i] * window_[i] / static_cast<float>(overlap_);
    }
}

// 元実装は 1 sample ずつ履歴へ入れ、hop sample ごとに 1 frame 処理する。frame は入力だけで決まるので並列に計算し、
// overlap-add だけを frame 順に行う。足し込みの順番が同じなので結果も同じ。out と in は同じ配列でもよい。
void VocalFunc::process(float* outL, float* outR, const float* inL, const float* inR, int count) {
    if (!ready_ || count <= 0) return;
    const long long n = n_, hop = hop_, ic = inCursor_;
    const long long total = ic + count;

    lineL_.resize(static_cast<std::size_t>(total));
    lineR_.resize(static_cast<std::size_t>(total));
    std::copy(histL_.begin(), histL_.end(), lineL_.begin());
    std::copy(histR_.begin(), histR_.end(), lineR_.begin());
    std::copy(inL, inL + count, lineL_.begin() + ic);
    std::copy(inR, inR + count, lineR_.begin() + ic);

    const long long frames = total >= n ? (total - n) / hop + 1 : 0;
    const long long tlLen = std::max(total, frames > 0 ? (frames - 1) * hop + n : 0) + n;
    tlL_.assign(static_cast<std::size_t>(tlLen), 0.0f);
    tlR_.assign(static_cast<std::size_t>(tlLen), 0.0f);
    std::copy(accL_.begin(), accL_.end(), tlL_.begin());
    std::copy(accR_.begin(), accR_.end(), tlR_.begin());

    const long long kBatch = gpu_ ? 256 : 64;
    termL_.resize(static_cast<std::size_t>(kBatch * n));
    termR_.resize(static_cast<std::size_t>(kBatch * n));
    for (long long j0 = 0; j0 < frames; j0 += kBatch) {
        const long long jn = std::min(kBatch, frames - j0);
        if (gpu_ && !gpu::centralizeFrames(lineL_.data() + j0 * hop, lineR_.data() + j0 * hop, static_cast<int>(jn),
                                           n_, hop_, overlap_, kvol_, slope_, window_.data(), threshold_.data(),
                                           termL_.data(), termR_.data()))
            gpu_ = false;
        if (!gpu_) parallelFor(jn, 1, [&](long long b, long long e) {
            Scratch s;
            for (long long jj = b; jj < e; ++jj) {
                const long long j = j0 + jj;
                frame(lineL_.data() + j * hop, lineR_.data() + j * hop,
                      termL_.data() + jj * n, termR_.data() + jj * n, s);
            }
        });
        const long long hBegin = j0 * hop + hop, hEnd = (j0 + jn - 1) * hop + n;
        parallelFor(hEnd - hBegin, 4096, [&](long long b, long long e) {
            for (long long h = hBegin + b; h < hBegin + e; ++h) {
                long long jFirst = h - n < 0 ? 0 : (h - n) / hop + 1;
                jFirst = std::max(jFirst, j0);
                const long long jLast = std::min((h - hop) / hop, j0 + jn - 1);
                float l = tlL_[static_cast<std::size_t>(h)], r = tlR_[static_cast<std::size_t>(h)];
                for (long long j = jFirst; j <= jLast; ++j) {
                    l += termL_[static_cast<std::size_t>((j - j0) * n + (h - j * hop))];
                    r += termR_[static_cast<std::size_t>((j - j0) * n + (h - j * hop))];
                }
                tlL_[static_cast<std::size_t>(h)] = l;
                tlR_[static_cast<std::size_t>(h)] = r;
            }
        });
    }

    // 16-bit 互換 mode では元実装に合わせて 1 sample ずつ整数に丸める。
    for (int t = 0; t < count; ++t) {
        const std::size_t h = static_cast<std::size_t>(ic + t - (n - hop));
        outL[t] = quantize16_ ? saturate(tlL_[h]) : tlL_[h];
        outR[t] = quantize16_ ? saturate(tlR_[h]) : tlR_[h];
    }

    const long long shift = frames * hop;
    inCursor_ = static_cast<int>(total - shift);
    histL_.assign(lineL_.begin() + shift, lineL_.end());
    histR_.assign(lineR_.begin() + shift, lineR_.end());
    for (long long x = 0; x < n; ++x) {
        accL_[static_cast<std::size_t>(x)] = tlL_[static_cast<std::size_t>(shift + x)];
        accR_[static_cast<std::size_t>(x)] = tlR_[static_cast<std::size_t>(shift + x)];
    }
}

float VocalFunc::saturate(float v) {
    // 元実装は int32 の作業 buffer に丸めて書くだけで、ここでは clamp しない。飽和処理は W_Finish (0x40e728) の soft clip が担う。
    return static_cast<float>(rint87(v));
}

}
