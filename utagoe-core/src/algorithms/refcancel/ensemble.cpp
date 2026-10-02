#include "rc_internal.h"
#include "parallel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>

namespace utagoe {
namespace rc {

Spec consensusBackground(const std::vector<const Spec*>& cands, const Spec& baseline, double strength) {
    if (cands.empty()) return baseline;
    const int F = baseline.F, T = baseline.T, C = baseline.C;
    const std::size_t FT = static_cast<std::size_t>(F) * T;
    const std::size_t K = cands.size();
    Spec center;
    center.resize(F, T, C);
    std::vector<float> spread(FT), scale(FT);
    parallelFor(static_cast<long long>(FT), 2048, [&](long long b, long long e) {
        std::vector<float> dist(K);
        for (long long p = b; p < e; ++p) {
            const int f = static_cast<int>(p / T), t = static_cast<int>(p % T);
            float bs = 0;
            for (int c = 0; c < C; ++c) bs += absSq(baseline.at(f, t, c));
            const float sc = std::sqrt(bs / C) + 1e-8f;
            scale[static_cast<std::size_t>(p)] = sc;
            // Weiszfeld の幾何中央値を複素 channel ベクトル全体で求める。
            cf ctr[2] = {};
            for (int c = 0; c < C; ++c) {
                for (std::size_t k = 0; k < K; ++k) ctr[c] += cands[k]->at(f, t, c);
                ctr[c] /= static_cast<float>(K);
            }
            for (int it = 0; it < 7; ++it) {
                cf num[2] = {};
                float den = 0;
                for (std::size_t k = 0; k < K; ++k) {
                    float d2 = 0;
                    for (int c = 0; c < C; ++c) d2 += absSq(cands[k]->at(f, t, c) - ctr[c]);
                    const float w = 1.0f / std::max(std::sqrt(d2), sc * 1e-4f + 1e-10f);
                    for (int c = 0; c < C; ++c) num[c] += cands[k]->at(f, t, c) * w;
                    den += w;
                }
                for (int c = 0; c < C; ++c) ctr[c] = num[c] / std::max(den, 1e-20f);
            }
            for (int c = 0; c < C; ++c) center.at(f, t, c) = ctr[c];
            for (std::size_t k = 0; k < K; ++k) {
                float d2 = 0;
                for (int c = 0; c < C; ++c) d2 += absSq(cands[k]->at(f, t, c) - ctr[c]);
                dist[k] = std::sqrt(d2 / C);
            }
            std::vector<double> dd(dist.begin(), dist.end());
            spread[static_cast<std::size_t>(p)] = static_cast<float>(median(dd));
        }
    });
    std::vector<float> rel(FT);
    for (std::size_t p = 0; p < FT; ++p) rel[p] = spread[p] / scale[p];
    gaussian(rel.data(), F, T, 1, 0, 1.0);
    gaussian(rel.data(), F, T, 1, 1, 2.0);
    Spec out;
    out.resize(F, T, C);
    for (int f = 0; f < F; ++f)
        for (int t = 0; t < T; ++t) {
            const float r = rel[static_cast<std::size_t>(f) * T + t] / 0.20f;
            const float agree = 1.0f / (1.0f + r * r);
            // 食い違うときは安全な基準 (robust) に戻る。
            for (int c = 0; c < C; ++c)
                out.at(f, t, c) = baseline.at(f, t, c) + static_cast<float>(strength) * agree * (center.at(f, t, c) - baseline.at(f, t, c));
        }
    return out;
}
}
}
