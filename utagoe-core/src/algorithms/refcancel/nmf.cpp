#include "rc_internal.h"
#include "parallel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>

namespace utagoe {
namespace rc {

Spec nmfBackground(const Spec& mixture, const Spec& powerReference, const Config& cfg) {
    const int F = mixture.F, T = mixture.T, C = mixture.C;
    const std::size_t FT = static_cast<std::size_t>(F) * T;
    std::vector<double> power(FT), anchor(FT);
    for (int f = 0; f < F; ++f)
        for (int t = 0; t < T; ++t) {
            double a = 0, b = 0;
            for (int c = 0; c < C; ++c) {
                a += absSq(mixture.at(f, t, c));
                b += absSq(powerReference.at(f, t, c));
            }
            power[static_cast<std::size_t>(f) * T + t] = static_cast<float>(a / C);
            anchor[static_cast<std::size_t>(f) * T + t] = static_cast<float>(b / C);
        }
    double meanPow = 0.0;
    for (double v : power) meanPow += v;
    const double scale = std::max(meanPow / static_cast<double>(FT), 1e-15);
    const int R = std::min({cfg.nmfRank, F, T});
    std::vector<double> V(FT), A(FT), resid(FT);
    for (std::size_t i = 0; i < FT; ++i) {
        V[i] = std::max(power[i] / scale, 1e-8);
        A[i] = std::max(anchor[i] / scale, 0.0);
        resid[i] = std::max(power[i] - anchor[i], 0.0) / scale;
    }
    // 初期値は残差から。乱数は Python 版と系列が違うが、ごく小さい揺らぎにしか使わない。
    std::mt19937_64 rng(cfg.seed);
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    std::vector<double> W(static_cast<std::size_t>(F) * R), H(static_cast<std::size_t>(R) * T);
    for (int k = 0; k < R; ++k) {
        const int sel = R > 1 ? static_cast<int>(static_cast<double>(T - 1) * k / (R - 1)) : 0;
        for (int f = 0; f < F; ++f) W[static_cast<std::size_t>(f) * R + k] = std::max(resid[static_cast<std::size_t>(f) * T + sel], 1e-4) + uni(rng) * 1e-3;
    }
    for (int k = 0; k < R; ++k) {
        double m = 0;
        for (int f = 0; f < F; ++f) m += W[static_cast<std::size_t>(f) * R + k];
        m = std::max(m / F, 1e-12);
        for (int f = 0; f < F; ++f) W[static_cast<std::size_t>(f) * R + k] /= m;
    }
    for (int t = 0; t < T; ++t) {
        double m = 0;
        for (int f = 0; f < F; ++f) m += resid[static_cast<std::size_t>(f) * T + t];
        const double init = std::max(m / F, 1e-3) / R;
        for (int k = 0; k < R; ++k) H[static_cast<std::size_t>(k) * T + t] = init * (0.9 + 0.2 * uni(rng));
    }
    std::vector<double> P(FT), Q(FT), Pinv(FT);
    auto model = [&]() {
        parallelFor(F, 16, [&](long long b, long long e) {
            for (long long f = b; f < e; ++f)
                for (int t = 0; t < T; ++t) {
                    double s = A[static_cast<std::size_t>(f) * T + t];
                    for (int k = 0; k < R; ++k) s += W[static_cast<std::size_t>(f) * R + k] * H[static_cast<std::size_t>(k) * T + t];
                    const double p = std::max(s, 1e-12);
                    Q[static_cast<std::size_t>(f) * T + t] = V[static_cast<std::size_t>(f) * T + t] / (p * p);
                    Pinv[static_cast<std::size_t>(f) * T + t] = 1.0 / p;
                }
        });
    };
    for (int it = 0; it < cfg.nmfIterations; ++it) {
        // H 更新: H *= sqrt((W^T Q) / (W^T Pinv))
        model();
        parallelFor(T, 16, [&](long long b, long long e) {
            for (long long t = b; t < e; ++t)
                for (int k = 0; k < R; ++k) {
                    double num = 0, den = 0;
                    for (int f = 0; f < F; ++f) {
                        const double w = W[static_cast<std::size_t>(f) * R + k];
                        num += w * Q[static_cast<std::size_t>(f) * T + t];
                        den += w * Pinv[static_cast<std::size_t>(f) * T + t];
                    }
                    H[static_cast<std::size_t>(k) * T + t] *= std::sqrt(std::max(num, 1e-20) / std::max(den, 1e-20));
                }
        });
        // W 更新: W *= sqrt((Q H^T) / (Pinv H^T))
        model();
        parallelFor(F, 16, [&](long long b, long long e) {
            for (long long f = b; f < e; ++f)
                for (int k = 0; k < R; ++k) {
                    double num = 0, den = 0;
                    for (int t = 0; t < T; ++t) {
                        const double h = H[static_cast<std::size_t>(k) * T + t];
                        num += Q[static_cast<std::size_t>(f) * T + t] * h;
                        den += Pinv[static_cast<std::size_t>(f) * T + t] * h;
                    }
                    W[static_cast<std::size_t>(f) * R + k] *= std::sqrt(std::max(num, 1e-20) / std::max(den, 1e-20));
                }
        });
        for (int k = 0; k < R; ++k) {
            double m = 0;
            for (int f = 0; f < F; ++f) m += W[static_cast<std::size_t>(f) * R + k];
            m = std::max(m / F, 1e-20);
            for (int f = 0; f < F; ++f) W[static_cast<std::size_t>(f) * R + k] /= m;
            for (int t = 0; t < T; ++t) H[static_cast<std::size_t>(k) * T + t] *= m;
        }
    }
    // 残差 (声など) の分散 / (それ + 参照の分散) を mask にする。
    std::vector<float> mask(FT);
    for (int f = 0; f < F; ++f)
        for (int t = 0; t < T; ++t) {
            double s = 0;
            for (int k = 0; k < R; ++k) s += W[static_cast<std::size_t>(f) * R + k] * H[static_cast<std::size_t>(k) * T + t];
            const float target = static_cast<float>(s * scale);
            const float an = static_cast<float>(anchor[static_cast<std::size_t>(f) * T + t]);
            mask[static_cast<std::size_t>(f) * T + t] = target / std::max(target + an, 1e-20f);
        }
    gaussian(mask.data(), F, T, 1, 0, 0.5);
    gaussian(mask.data(), F, T, 1, 1, 0.7);
    const float floor = static_cast<float>(std::pow(10.0, cfg.maskFloorDb / 20.0));
    Spec out;
    out.resize(F, T, C);
    for (int f = 0; f < F; ++f)
        for (int t = 0; t < T; ++t) {
            const float m = std::clamp(mask[static_cast<std::size_t>(f) * T + t], floor, 1.0f);
            for (int c = 0; c < C; ++c) out.at(f, t, c) = mixture.at(f, t, c) - mixture.at(f, t, c) * m;
        }
    return out;
}
}
}
