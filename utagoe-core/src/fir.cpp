// 元実装の CenterFocus に相当する Kaiser 窓 FIR。
// 設計 0x404f94、窓 0x405208、I0 0x405284、次数 0x405344、畳み込み 0x405398 を写した。
// 元実装は中間値をたびたび float に格納するので、その丸めも再現している。

#include "fir.h"
#include "x87.h"
#include "parallel.h"
#include "gpu.h"

#include <cmath>
#include <cstring>

namespace utagoe {
namespace {

// 元実装の PI は float 定数 3.141592 (0x4c1570)。正確な pi ではない。
constexpr float kPiF = 3.141592f;

// Kaiser 窓 w(i) = I0(beta * sqrt(1 - (i/half)^2)) / I0(beta)。
long double kaiserWindow(int half, float beta, float i) {
    const float r = i / static_cast<float>(half);
    const float t = static_cast<float>(1.0f - std::pow(static_cast<double>(r), 2.0));
    const float x = static_cast<float>(std::sqrt(static_cast<double>(t)) * beta);
    return static_cast<long double>(kaiser::besselI0(x)) / kaiser::besselI0(beta);
}

}

namespace kaiser {

float besselI0(float x) {
    const float half = x * 0.5f;
    float sum = 0.0f;
    for (int k = 1; k <= 20; ++k) {
        float fact = 1.0f;
        for (int j = 1; j <= k; ++j) fact = static_cast<float>(j) * fact;
        const long double p = std::pow(static_cast<double>(half), static_cast<double>(k));
        const float term = static_cast<float>(p / fact);
        sum = static_cast<float>(std::pow(static_cast<double>(term), 2.0) + sum);
    }
    return 1.0f + sum;
}

float beta(float attenDb) {
    // x87 は拡張精度で計算して最後に float へ格納するので、long double で評価してから丸める。
    if (attenDb > 50.0f) return static_cast<float>((static_cast<long double>(attenDb) - 8.7f) * 0.1102f);
    if (21.0f > attenDb) return 0.0f;
    const float a = attenDb - 21.0f;
    // 指数 0.4 は float 定数を double に広げた値 (0x3fd99999a0000000)。
    const double p = std::pow(static_cast<double>(a), static_cast<double>(0.4f));
    return static_cast<float>(p * 0.5842f + static_cast<long double>(attenDb - 21.0f) * 0.07886);
}

int order(float transitionWidthRad, float attenDb) {
    float t = static_cast<float>((static_cast<long double>(attenDb) - 8.0f) /
                                 (static_cast<long double>(2.285f) * transitionWidthRad));
    t = static_cast<float>(1.0L + t);
    int n = static_cast<int>(std::nearbyint(t));
    if (n % 2 != 0) ++n;
    if (n > 1000) n = 1000;
    return n;
}

}

void FirFilter::design(int sampleRate, float lowHz, float highHz, float attenDb, Mode mode) {
    const float nyq = static_cast<float>(static_cast<long double>(sampleRate) * 0.5f);
    auto toRad = [&](float hz) { return static_cast<float>(static_cast<long double>(hz) / nyq * kPiF); };

    const float dw = static_cast<float>((static_cast<long double>(highHz) - lowHz) / nyq * kPiF);
    const int n = kaiser::order(dw, attenDb);
    const int half = n / 2;
    coef_.assign(static_cast<std::size_t>(n + 1), 0.0f);

    float w1 = toRad(lowHz);
    float w2 = toRad(highHz);
    if (mode == Mode::HighPass) {
        w1 = kPiF - w1;
        w2 = kPiF - w2;
    }
    const float wc = static_cast<float>((static_cast<long double>(w1) + w2) * 0.5f);
    const float b = kaiser::beta(attenDb);

    std::size_t k = 0;
    for (int i = -half; i <= half; ++i, ++k) {
        if (i != 0) {
            const float arg = static_cast<float>(static_cast<long double>(i) * wc);
            const long double sinc = std::sin(static_cast<double>(arg)) /
                                     (static_cast<long double>(i) * kPiF);
            coef_[k] = static_cast<float>(sinc * kaiserWindow(half, b, static_cast<float>(i)));
        } else {
            coef_[k] = static_cast<float>(static_cast<long double>(wc) / kPiF * kaiserWindow(half, b, 0.0f));
        }
    }

    // ハイパスは偶数番目の係数の符号を反転して作る。中央の係数の偶奇で全体の極性が変わる点も元実装どおり。
    if (mode == Mode::HighPass) {
        for (std::size_t i = 0; i < coef_.size(); i += 2) coef_[i] = -1.0f * coef_[i];
    }

    reset();
}

void FirFilter::reset() {
    delay_.assign(coef_.size(), 0.0);
}

void FirFilter::process(double* out, const double* in, int count, bool quantize) {
    if (coef_.empty() || count <= 0) return;

    const std::size_t taps = coef_.size();
    const std::size_t c = static_cast<std::size_t>(count);

    // [遅延線 | 入力] を作って畳み込み、末尾を遅延線に戻す。
    tmp_.resize(taps + c);
    std::memcpy(tmp_.data(), delay_.data(), taps * sizeof(double));
    std::memcpy(tmp_.data() + taps, in, c * sizeof(double));

    // GPU では float の積和で計算する (速度優先のときだけ)。一度に送る量は driver の時間制限に収まるよう抑える。
    if (gpu_) {
        gpuH_.assign(coef_.begin(), coef_.end());
        constexpr std::size_t kPiece = std::size_t{1} << 18;
        for (std::size_t at = 0; gpu_ && at < c; at += kPiece) {
            const std::size_t len = std::min(kPiece, c - at);
            gpuX_.assign(tmp_.begin() + static_cast<std::ptrdiff_t>(at + 1),
                         tmp_.begin() + static_cast<std::ptrdiff_t>(at + taps + len));
            gpuY_.resize(len);
            if (!gpu::firConvolve(gpuX_.data(), static_cast<int>(len), gpuH_.data(), static_cast<int>(taps), gpuY_.data())) {
                gpu_ = false;
                break;
            }
            for (std::size_t i = 0; i < len; ++i)
                out[at + i] = quantize ? static_cast<double>(rint87(gpuY_[i])) : gpuY_[i];
        }
        if (gpu_) {
            std::memcpy(delay_.data(), tmp_.data() + c, taps * sizeof(double));
            return;
        }
    }

    // 出力 sample ごとに独立なので並列に計算する。1 sample 内の足し込み順は元実装と同じ。
    parallelFor(static_cast<long long>(c), 1024, [&](long long b, long long e) {
        for (long long i = b; i < e; ++i) {
            const double* x = tmp_.data() + taps + i;
            if (quantize) {
                // 元実装の accumulator は float で、1 項ごとに float へ格納される。
                float acc = 0.0f;
                for (std::size_t j = 0; j < taps; ++j)
                    acc = static_cast<float>(static_cast<long double>(x[-static_cast<std::ptrdiff_t>(j)]) * coef_[j] + acc);
                out[i] = static_cast<double>(rint87(acc));
            } else {
                double acc = 0.0;
                for (std::size_t j = 0; j < taps; ++j) acc += x[-static_cast<std::ptrdiff_t>(j)] * coef_[j];
                out[i] = acc;
            }
        }
    });

    std::memcpy(delay_.data(), tmp_.data() + c, taps * sizeof(double));
}

}
