#include "rc_internal.h"
#include "parallel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>

namespace utagoe {
namespace rc {

void PolynomialBasis::fit(const Audio& ref) {
    const int C = ref.channels;
    const std::size_t n = ref.frames();
    scale.assign(static_cast<std::size_t>(C), 0.0);
    std::vector<std::vector<double>> u(static_cast<std::size_t>(C), std::vector<double>(n));
    for (int c = 0; c < C; ++c) {
        std::vector<double> a(n);
        for (std::size_t i = 0; i < n; ++i) a[i] = std::fabs(static_cast<double>(ref.at(i, c)));
        scale[static_cast<std::size_t>(c)] = std::max(quantile(a, 0.999), 1e-5);
        for (std::size_t i = 0; i < n; ++i) u[static_cast<std::size_t>(c)][i] = ref.at(i, c) / scale[static_cast<std::size_t>(c)];
    }
    // q[k][c][i]: 0 次 (u)、2 次、3 次の直交化した系列。
    std::vector<std::vector<std::vector<double>>> q{u};
    for (int deg = 0; deg < 2; ++deg) {
        const int power = deg + 2;
        coefficients[deg].assign(q.size(), std::vector<double>(static_cast<std::size_t>(C)));
        divisors[deg].assign(static_cast<std::size_t>(C), 1.0);
        std::vector<std::vector<double>> v(static_cast<std::size_t>(C), std::vector<double>(n));
        for (int c = 0; c < C; ++c) {
            for (std::size_t i = 0; i < n; ++i) v[static_cast<std::size_t>(c)][i] = std::pow(std::clamp(u[static_cast<std::size_t>(c)][i], -4.0, 4.0), power);
            // Gram-Schmidt で低次の成分を除く。
            for (std::size_t k = 0; k < q.size(); ++k) {
                double num = 0.0, den = 0.0;
                for (std::size_t i = 0; i < n; ++i) {
                    num += v[static_cast<std::size_t>(c)][i] * q[k][static_cast<std::size_t>(c)][i];
                    den += q[k][static_cast<std::size_t>(c)][i] * q[k][static_cast<std::size_t>(c)][i];
                }
                const double a = num / std::max(den, 1e-20);
                for (std::size_t i = 0; i < n; ++i) v[static_cast<std::size_t>(c)][i] -= q[k][static_cast<std::size_t>(c)][i] * a;
                coefficients[deg][k][static_cast<std::size_t>(c)] = a;
            }
            double ss = 0.0, bs = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                ss += v[static_cast<std::size_t>(c)][i] * v[static_cast<std::size_t>(c)][i];
                bs += u[static_cast<std::size_t>(c)][i] * u[static_cast<std::size_t>(c)][i];
            }
            const double denom = std::sqrt(ss / n), base = std::sqrt(bs / n);
            divisors[deg][static_cast<std::size_t>(c)] = std::max(denom / std::max(base, 1e-8), 1e-4);
            for (std::size_t i = 0; i < n; ++i) v[static_cast<std::size_t>(c)][i] /= divisors[deg][static_cast<std::size_t>(c)];
        }
        q.push_back(std::move(v));
    }
}

Audio PolynomialBasis::transform(const Audio& x) const {
    const int C = x.channels;
    const std::size_t n = x.frames();
    Audio out;
    out.channels = C * 3;
    out.v.resize(n * static_cast<std::size_t>(C) * 3);
    parallelFor(static_cast<long long>(n), 4096, [&](long long b, long long e) {
        for (long long i = b; i < e; ++i) {
            for (int c = 0; c < C; ++c) {
                const double sc = scale[static_cast<std::size_t>(c)];
                const double u = x.at(static_cast<std::size_t>(i), c) / sc;
                double q[3] = {u, 0, 0};
                for (int deg = 0; deg < 2; ++deg) {
                    double v = std::pow(std::clamp(u, -4.0, 4.0), deg + 2);
                    for (int k = 0; k <= deg; ++k) v -= q[k] * coefficients[deg][static_cast<std::size_t>(k)][static_cast<std::size_t>(c)];
                    q[deg + 1] = v / divisors[deg][static_cast<std::size_t>(c)];
                }
                // 1 本目は参照波形そのもの。並びは [元の channel..., 2 次..., 3 次...]。
                out.at(static_cast<std::size_t>(i), c) = x.at(static_cast<std::size_t>(i), c);
                out.at(static_cast<std::size_t>(i), C + c) = static_cast<float>(q[1] * sc);
                out.at(static_cast<std::size_t>(i), 2 * C + c) = static_cast<float>(q[2] * sc);
            }
        }
    });
    return out;
}

Spec hammersteinBackground(const Spec& features, const Transfer& h, const Spec& linear, bool accepted, double maxRelativeRms) {
    if (!accepted) return linear;
    Spec cand = applyTransfer(features, h);
    const int F = linear.F, T = linear.T, C = linear.C;
    std::vector<float> p(static_cast<std::size_t>(F) * T), q(static_cast<std::size_t>(F) * T);
    for (int f = 0; f < F; ++f)
        for (int t = 0; t < T; ++t) {
            float a = 0, b = 0;
            for (int c = 0; c < C; ++c) {
                a += absSq(linear.at(f, t, c));
                b += absSq(cand.at(f, t, c) - linear.at(f, t, c));
            }
            p[static_cast<std::size_t>(f) * T + t] = a / C;
            q[static_cast<std::size_t>(f) * T + t] = b / C;
        }
    gaussian(p.data(), F, T, 1, 0, 2.0);
    gaussian(p.data(), F, T, 1, 1, 5.0);
    gaussian(q.data(), F, T, 1, 0, 2.0);
    gaussian(q.data(), F, T, 1, 1, 5.0);
    Spec out = linear;
    for (int f = 0; f < F; ++f)
        for (int t = 0; t < T; ++t) {
            const std::size_t i = static_cast<std::size_t>(f) * T + t;
            // 非線形分で余計に消す量を、線形の背景に対する比で抑える。
            const float gate = std::min(1.0f, static_cast<float>(maxRelativeRms * std::sqrt((p[i] + 1e-18) / (q[i] + 1e-18))));
            for (int c = 0; c < C; ++c) out.at(f, t, c) = linear.at(f, t, c) + (cand.at(f, t, c) - linear.at(f, t, c)) * gate;
        }
    return out;
}
}
}
