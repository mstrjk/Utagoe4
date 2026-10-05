// 「By Frequency」方式の抽出エンジン。初期化 0x40ca90、処理 0x40ce14 (mode 0) / 0x40d280 (mode 1)、位相差 0x40d22c を写した。
// FFT は split-radix の元実装と演算順が違うので bit 単位では一致しない (fft.cpp 参照)。
//
// 元実装は 1 sample 入れるごとに履歴を進め、hop sample たまるたびに 1 frame 処理する。
// 各 frame の FFT は入力だけで決まるので、まとめて並列に計算し、overlap-add だけを frame 順に行う。
// 足し込みの順番と丸めが同じなので、結果は 1 sample ずつ処理した場合と一致する。

#include "freq_engine.h"
#include "mathconst.h"
#include "parallel.h"
#include "gpu.h"
#include "x87.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace utagoe {
namespace {

constexpr int kBatch = 64;   // 一度に計算する frame 数。項の保存領域の大きさを決める。

// 位相差を [0, PI] に wrap する。
double phaseDiff(double a, double b) {
    double d = std::fabs(a - b);
    if (d > kPi) d = 2.0 * kPi - d;
    return d;
}

}

void FreqEngine::init(int blockSize, int overlap, int mode, float kvol, bool quantize16) {
    n_ = blockSize;
    overlap_ = overlap > 0 ? overlap : 1;
    hop_ = n_ / overlap_;
    mode_ = mode;
    kvol_ = kvol;
    kclamp_ = std::min(kvol, 1.5f);
    quantize_ = quantize16;

    tables_.resize(static_cast<std::size_t>(n_));
    const std::size_t n = static_cast<std::size_t>(n_);

    // 位相差の許容量。低域ほど厳しく、0.15 rad を下限にして kvol 倍する。
    threshold_.assign(n / 2, 0.0f);
    for (int k = 0; k < n_ / 2; ++k) {
        const long double per = static_cast<long double>(n_ / 2) / (static_cast<long double>(k) + 1.0f);
        float t = static_cast<float>(kPi / per);
        if (!(t >= 0.15)) t = 0.15f;
        threshold_[static_cast<std::size_t>(k)] = kvol_ * t;
    }

    // Hann 窓 0.5 - 0.5 cos(2 PI i / n)。
    window_.assign(n, 0.0f);
    for (int i = 0; i < n_; ++i) {
        const double a = static_cast<long double>(i) * (2.0 * kPi) / n_;
        window_[static_cast<std::size_t>(i)] = static_cast<float>(std::cos(a) * -0.5f + 0.5f);
    }

    // 元実装は入力位置 n - hop、出力位置 0 から始める。履歴は 0。
    inCursor_ = n_ - hop_;
    hist_[0].assign(static_cast<std::size_t>(inCursor_), 0.0f);
    hist_[1].assign(static_cast<std::size_t>(inCursor_), 0.0f);
    acc_.assign(n, 0.0f);
}

void FreqEngine::frame(const float* winA, const float* winB, double level, long double* terms, Scratch& s) const {
    const std::size_t n = static_cast<std::size_t>(n_);
    s.a.resize(2 * n);
    s.b.resize(2 * n);
    float* A = s.a.data();
    float* B = s.b.data();

    for (std::size_t i = 0; i < n; ++i) {
        A[2 * i] = winA[i] * window_[i];
        A[2 * i + 1] = 0.0f;
        B[2 * i] = winB[i] * window_[i];
        B[2 * i + 1] = 0.0f;
    }
    fft_forward(A, n, tables_);
    fft_forward(B, n, tables_);

    const float gate = cap();
    for (std::size_t k = 0; k < n / 2; ++k) {
        float& ar = A[2 * k];
        float& ai = A[2 * k + 1];
        const float br = B[2 * k], bi = B[2 * k + 1];

        const double magA = std::sqrt(static_cast<double>(static_cast<long double>(ar) * ar + static_cast<long double>(ai) * ai));
        const double magB = std::sqrt(static_cast<double>(static_cast<long double>(br) * br + static_cast<long double>(bi) * bi));

        double dPhase = 0.0;
        if (mode_ == 0) {
            const double phA = (ar == 0.0f && ai == 0.0f) ? 0.0 : std::atan2(static_cast<double>(ai), static_cast<double>(ar));
            const double phB = (br == 0.0f && bi == 0.0f) ? 0.0 : std::atan2(static_cast<double>(bi), static_cast<double>(br));
            dPhase = phaseDiff(phB, phA);
        }

        // インストの spectrum を level 倍して引く。
        ar = static_cast<float>(ar - static_cast<long double>(br) * level);
        ai = static_cast<float>(ai - static_cast<long double>(bi) * level);

        // インスト側が原曲より強い bin は伴奏とみなして落とす。mode 0 は位相も近いときだけ。
        if (magB * gate > magA && (mode_ != 0 || dPhase < threshold_[k])) {
            ar = 0.0f;
            ai = 0.0f;
        }
    }

    // 負の周波数を 0 にして inverse すると、実部が元の半分になる。make-up gain 5.3333 に含まれている。
    std::fill(A + n, A + 2 * n, 0.0f);
    fft_inverse(A, n, tables_);

    // 先頭 hop 個は出力済みの位置に当たるので使われない。
    for (std::size_t i = static_cast<std::size_t>(hop_); i < n; ++i)
        terms[i] = static_cast<long double>(5.333330154418945f) * A[2 * i] * window_[i] / overlap_;
}

void FreqEngine::process(const float* orig, const float* inst, const double* level, double* out, int count) {
    if (count <= 0) return;
    const long long n = n_, hop = hop_, ic = inCursor_;
    const long long total = ic + count;

    // 履歴 + 今回の入力を 1 本につなげる。index 0 が元実装の hist[0]、acc[0] にも対応する。
    histA_.resize(static_cast<std::size_t>(total));
    histB_.resize(static_cast<std::size_t>(total));
    std::copy(hist_[0].begin(), hist_[0].end(), histA_.begin());
    std::copy(hist_[1].begin(), hist_[1].end(), histB_.begin());
    std::copy(orig, orig + count, histA_.begin() + ic);
    std::copy(inst, inst + count, histB_.begin() + ic);

    // frame j は index j*hop から n sample を使い、入力 index j*hop + n - 1 を入れた直後に処理される。
    const long long frames = total >= n ? (total - n) / hop + 1 : 0;
    const long long tlLen = std::max(total, frames > 0 ? (frames - 1) * hop + n : 0) + n;
    timeline_.assign(static_cast<std::size_t>(tlLen), 0.0f);
    std::copy(acc_.begin(), acc_.end(), timeline_.begin());

    const float cap = this->cap();
    // level は frame を処理した呼び出しの値を上限 cap で抑えたもの。
    auto levelOf = [&](long long j) {
        double lv = level[j * hop + n - 1 - ic];
        if (!(cap >= lv)) lv = cap;
        return lv;
    };
    const long long batch = gpu_ ? 256 : kBatch;
    terms_.resize(static_cast<std::size_t>(batch) * static_cast<std::size_t>(n));
    for (long long j0 = 0; j0 < frames; j0 += batch) {
        const long long jn = std::min<long long>(batch, frames - j0);
        if (gpu_) {
            gpuLevels_.resize(static_cast<std::size_t>(jn));
            for (long long jj = 0; jj < jn; ++jj) gpuLevels_[static_cast<std::size_t>(jj)] = levelOf(j0 + jj);
            gpuTerms_.resize(static_cast<std::size_t>(jn * n));
            if (gpu::freqFrames(histA_.data() + j0 * hop, histB_.data() + j0 * hop, static_cast<int>(jn), n_, hop_,
                                overlap_, mode_, cap, window_.data(), threshold_.data(), gpuLevels_.data(),
                                gpuTerms_.data()))
                std::copy(gpuTerms_.begin(), gpuTerms_.end(), terms_.begin());
            else
                gpu_ = false;
        }
        if (!gpu_) parallelFor(jn, 1, [&](long long b, long long e) {
            Scratch s;
            for (long long jj = b; jj < e; ++jj) {
                const long long j = j0 + jj;
                frame(histA_.data() + j * hop, histB_.data() + j * hop, levelOf(j),
                      terms_.data() + jj * n, s);
            }
        });
        // overlap-add は位置ごとに frame 順で足す。位置ごとは独立なので並列にできる。
        const long long hBegin = j0 * hop + hop, hEnd = (j0 + jn - 1) * hop + n;
        parallelFor(hEnd - hBegin, 4096, [&](long long b, long long e) {
            for (long long h = hBegin + b; h < hBegin + e; ++h) {
                long long jFirst = (h - n) / hop + 1;
                if (h - n < 0) jFirst = 0;
                jFirst = std::max(jFirst, j0);
                const long long jLast = std::min((h - hop) / hop, j0 + jn - 1);
                float v = timeline_[static_cast<std::size_t>(h)];
                for (long long j = jFirst; j <= jLast; ++j)
                    v = static_cast<float>(terms_[static_cast<std::size_t>((j - j0) * n + (h - j * hop))] + v);
                timeline_[static_cast<std::size_t>(h)] = v;
            }
        });
    }

    // 入力 index p の出力は、その時点の出力位置 p - (n - hop) の値。
    for (int t = 0; t < count; ++t) {
        const float v = timeline_[static_cast<std::size_t>(ic + t - (n - hop))];
        out[t] = quantize_ ? static_cast<double>(rint87(v)) : v;
    }

    const long long shift = frames * hop;
    inCursor_ = static_cast<int>(total - shift);
    hist_[0].assign(histA_.begin() + shift, histA_.end());
    hist_[1].assign(histB_.begin() + shift, histB_.end());
    for (long long x = 0; x < n; ++x)
        acc_[static_cast<std::size_t>(x)] = timeline_[static_cast<std::size_t>(shift + x)];
}

}
