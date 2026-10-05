// Utagoe コア用の複素 FFT 実装。
// 元実装と bit 単位で同じとは限らない。
// 元の FFT は hand-optimized split-radix。演算自体は追えたが twiddle table の index 計算は完全には復元できていない。
// 推測の転記にせず、同じ契約を満たす検証済み radix-2 Cooley-Tukey を使う。
// 契約は in-place / interleaved float32、forward は非正規化、inverse は 1/n 正規化。
// 数値的には正しいが、演算順が違うので元実装との bit-identical な丸めは保証しない。
// 差は用途上かなり小さいが、WAV の byte 比較を検証方法にしないこと。
// bit exact が必要になったら run() を split-radix の忠実な実装へ差し替える。周辺 API はそのままでよい。

#include "fft.h"
#include "mathconst.h"

#include <cmath>

namespace utagoe {
namespace {


}

void FftTables::resize(std::size_t n) {
    if (n == n_) return;
    n_ = n;

    tw_.assign(2 * (n / 2 + 1), 0.0f);
    for (std::size_t k = 0; k <= n / 2; ++k) {
        const double a = -2.0 * kPi * static_cast<double>(k) / static_cast<double>(n);
        tw_[2 * k]     = static_cast<float>(std::cos(a));
        tw_[2 * k + 1] = static_cast<float>(std::sin(a));
    }

    // scratch サイズは元実装との対応説明のため残している。この実装では使わない。
    const int m = static_cast<int>(std::sqrt(static_cast<double>(n) / 2.0) + 2.0);
    stride_.assign(static_cast<std::size_t>(m > 0 ? m : 1), 0);
}

namespace {

void bit_reverse(float* a, std::size_t n) {
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(a[2 * i],     a[2 * j]);
            std::swap(a[2 * i + 1], a[2 * j + 1]);
        }
    }
}

void run(float* a, std::size_t n, const FftTables& tables, bool inverse) {
    if (n < 2) return;

    bit_reverse(a, n);

    const float* tw = tables.twiddles();
    const float  sgn = inverse ? -1.0f : 1.0f;

    for (std::size_t len = 2; len <= n; len <<= 1) {
        const std::size_t half = len >> 1;
        const std::size_t step = n / len;
        for (std::size_t base = 0; base < n; base += len) {
            for (std::size_t k = 0; k < half; ++k) {
                const std::size_t ti = k * step;
                const float wr = tw[2 * ti];
                const float wi = sgn * tw[2 * ti + 1];

                const std::size_t p = 2 * (base + k);
                const std::size_t q = 2 * (base + k + half);

                const float xr = a[q] * wr - a[q + 1] * wi;
                const float xi = a[q] * wi + a[q + 1] * wr;

                a[q]     = a[p]     - xr;
                a[q + 1] = a[p + 1] - xi;
                a[p]     = a[p]     + xr;
                a[p + 1] = a[p + 1] + xi;
            }
        }
    }
}

}

void fft_forward(float* data, std::size_t n, const FftTables& tables) {
    run(data, n, tables, false);
}

void fft_inverse(float* data, std::size_t n, const FftTables& tables) {
    run(data, n, tables, true);

    // 1/n 正規化は全 2n float にまとめて適用する。元実装も要素ごとの除算ではなく逆数を掛ける。
    const float inv = 1.0f / static_cast<float>(n);
    for (std::size_t i = 0; i < 2 * n; ++i) data[i] *= inv;
}

}
