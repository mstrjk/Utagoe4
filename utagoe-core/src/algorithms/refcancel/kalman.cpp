#include "rc_internal.h"
#include "parallel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>

namespace utagoe {
namespace rc {

Spec kalmanBackground(const Spec& mixture, const Spec& baseline, const Config& cfg, const std::vector<char>& updateMask) {
    const int F = mixture.F, T = mixture.T, C = mixture.C;
    const std::size_t FC = static_cast<std::size_t>(F) * C;
    std::vector<cd> h(FC, cd(1, 0)), cross(FC, cd(0, 0));
    std::vector<double> cov(FC, 0.03), noise(FC, 0.0), basePow(FC, 0.0), errPow(FC, 0.0), predPow(FC, 0.0);
    for (int f = 0; f < F; ++f)
        for (int c = 0; c < C; ++c) {
            double n = 0.0, p = 0.0;
            for (int t = 0; t < T; ++t) {
                n += absSq(cd(mixture.at(f, t, c)) - cd(baseline.at(f, t, c)));
                p += absSq(cd(baseline.at(f, t, c)));
            }
            noise[static_cast<std::size_t>(f) * C + c] = n / T + 1e-12;
            basePow[static_cast<std::size_t>(f) * C + c] = p / T;
        }
    Spec out;
    out.resize(F, T, C);
    std::vector<double> evidence(FC), meas(FC);
    std::vector<cd> hPrior(FC), step(FC), err(FC);
    std::vector<double> pPrior(FC);
    for (int n = 0; n < T; ++n) {
        for (std::size_t i = 0; i < FC; ++i) {
            const int f = static_cast<int>(i / C), c = static_cast<int>(i % C);
            const cd bv(baseline.at(f, n, c)), yv(mixture.at(f, n, c));
            // 全体の推定へ少し引き戻し、長期的な漂流を防ぐ。
            hPrior[i] = 1.0 + 0.9998 * (h[i] - 1.0);
            pPrior[i] = std::min(cov[i] + cfg.kalmanProcessNoise, 0.3);
            const cd pred = hPrior[i] * bv;
            out.at(f, n, c) = cf(pred);
            err[i] = yv - pred;
            const double inst = absSq(err[i]);
            cross[i] = 0.94 * cross[i] + 0.06 * err[i] * std::conj(bv);
            errPow[i] = 0.94 * errPow[i] + 0.06 * inst;
            predPow[i] = 0.94 * predPow[i] + 0.06 * absSq(bv);
            evidence[i] = std::clamp(absSq(cross[i]) / std::max(errPow[i] * predPow[i], 1e-20), 0.0, 1.0);
            noise[i] = 0.96 * noise[i] + 0.04 * inst;
            meas[i] = std::max(0.002 * basePow[i] + 1e-14, std::max(noise[i], inst * 0.25));
        }
        // 周波数方向に平滑化した証拠で、説明できない成分が多いときは更新を弱める。
        gaussian(evidence.data(), F, C, 1, 0, 2.0);
        for (std::size_t i = 0; i < FC; ++i) {
            const int f = static_cast<int>(i / C), c = static_cast<int>(i % C);
            const cd bv(baseline.at(f, n, c));
            meas[i] /= std::max(0.08 + 0.92 * evidence[i], 0.02);
            const double den = pPrior[i] * absSq(bv) + meas[i];
            const cd gain = pPrior[i] * std::conj(bv) / std::max(den, 1e-20);
            cd s = gain * err[i];
            s *= std::min(1.0, cfg.kalmanMaxStep / std::max(std::abs(s), 1e-20));
            step[i] = s;
        }
        if (updateMask.empty() || updateMask[static_cast<std::size_t>(n)]) {
            for (std::size_t i = 0; i < FC; ++i) {
                cd v = hPrior[i] + step[i];
                const cd corr = v - 1.0;
                v = 1.0 + corr * std::min(1.0, cfg.kalmanMaxCorrection / std::max(std::abs(corr), 1e-20));
                h[i] = v;
            }
            gaussianComplex(h.data(), F, C, 1, 0, 0.6);
            for (std::size_t i = 0; i < FC; ++i) {
                const int f = static_cast<int>(i / C), c = static_cast<int>(i % C);
                const cd bv(baseline.at(f, n, c));
                const cd eff = std::abs(err[i]) > 1e-15 ? step[i] / err[i] : cd(0, 0);
                const double cv = absSq(1.0 - eff * bv) * pPrior[i] + absSq(eff) * meas[i];
                cov[i] = std::clamp(cv, 1e-8, 0.3);
            }
        } else {
            h = hPrior;
            cov = pPrior;
        }
    }
    return out;
}
}
}
