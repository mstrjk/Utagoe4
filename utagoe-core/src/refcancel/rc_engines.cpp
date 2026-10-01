// 5 つのエンジンと合議 (Python 版 engines/*.py, ensemble.py)。
// 数式と定数は Python 版のまま。計算は周波数 bin ごとなどの独立な単位で並列化している。

#include "rc.h"
#include "../parallel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

namespace utagoe {
namespace rc {
namespace {

// 小さい複素連立方程式 A X = B を部分 pivot 付き Gauss 消去で解く。A は n x n、B は n x m (どちらも行優先)。
bool solveComplex(std::vector<cd>& A, std::vector<cd>& B, int n, int m) {
    for (int col = 0; col < n; ++col) {
        int piv = col;
        double best = std::abs(A[static_cast<std::size_t>(col * n + col)]);
        for (int r = col + 1; r < n; ++r) {
            const double v = std::abs(A[static_cast<std::size_t>(r * n + col)]);
            if (v > best) { best = v; piv = r; }
        }
        if (best == 0.0) return false;
        if (piv != col) {
            for (int k = 0; k < n; ++k) std::swap(A[static_cast<std::size_t>(col * n + k)], A[static_cast<std::size_t>(piv * n + k)]);
            for (int k = 0; k < m; ++k) std::swap(B[static_cast<std::size_t>(col * m + k)], B[static_cast<std::size_t>(piv * m + k)]);
        }
        const cd d = A[static_cast<std::size_t>(col * n + col)];
        for (int r = col + 1; r < n; ++r) {
            const cd f = A[static_cast<std::size_t>(r * n + col)] / d;
            if (f == cd(0, 0)) continue;
            for (int k = col; k < n; ++k) A[static_cast<std::size_t>(r * n + k)] -= f * A[static_cast<std::size_t>(col * n + k)];
            for (int k = 0; k < m; ++k) B[static_cast<std::size_t>(r * m + k)] -= f * B[static_cast<std::size_t>(col * m + k)];
        }
    }
    for (int r = n - 1; r >= 0; --r) {
        for (int k = 0; k < m; ++k) {
            cd s = B[static_cast<std::size_t>(r * m + k)];
            for (int c = r + 1; c < n; ++c) s -= A[static_cast<std::size_t>(r * n + c)] * B[static_cast<std::size_t>(c * m + k)];
            B[static_cast<std::size_t>(r * m + k)] = s / A[static_cast<std::size_t>(r * n + r)];
        }
    }
    return true;
}

double absSq(const cd& z) { return z.real() * z.real() + z.imag() * z.imag(); }
float absSq(const cf& z) { return z.real() * z.real() + z.imag() * z.imag(); }

}


Transfer fitTransfer(const Spec& x, const Spec& y, const Config& cfg, const Transfer* prior,
                     const std::vector<double>* penalties) {
    const int F = x.F, T = x.T, D = x.C, O = y.C;
    Transfer h;
    h.resize(F, D, O);
    bool finite = true;
    parallelFor(F, 4, [&](long long b, long long e) {
        std::vector<cd> X(static_cast<std::size_t>(T) * D), Y(static_cast<std::size_t>(T) * O), H0(static_cast<std::size_t>(D) * O);
        std::vector<cd> H(static_cast<std::size_t>(D) * O), gram, rhs;
        std::vector<double> diag(static_cast<std::size_t>(D)), w(static_cast<std::size_t>(T)), mag(static_cast<std::size_t>(T));
        for (long long fi = b; fi < e; ++fi) {
            const int f = static_cast<int>(fi);
            for (int t = 0; t < T; ++t) {
                for (int d = 0; d < D; ++d) X[static_cast<std::size_t>(t * D + d)] = cd(x.at(f, t, d));
                for (int o = 0; o < O; ++o) Y[static_cast<std::size_t>(t * O + o)] = cd(y.at(f, t, o));
            }
            for (int d = 0; d < D; ++d)
                for (int o = 0; o < O; ++o)
                    H0[static_cast<std::size_t>(d * O + o)] = prior ? cd(prior->at(f, d, o)) : cd(d == o ? 1.0 : 0.0, 0.0);
            // 正則化は特徴量のエネルギーに比例させる。
            double band = 0.0;
            std::vector<double> energy(static_cast<std::size_t>(D), 0.0);
            for (int d = 0; d < D; ++d) {
                for (int t = 0; t < T; ++t) energy[static_cast<std::size_t>(d)] += absSq(X[static_cast<std::size_t>(t * D + d)]);
                energy[static_cast<std::size_t>(d)] /= T;
                band += energy[static_cast<std::size_t>(d)];
            }
            band /= D;
            for (int d = 0; d < D; ++d) {
                const double pen = penalties ? (*penalties)[static_cast<std::size_t>(d)] : 1.0;
                diag[static_cast<std::size_t>(d)] = cfg.ridge * T * std::max(energy[static_cast<std::size_t>(d)], band * 1e-3 + 1e-18) * pen + 1e-16;
            }
            double yPower = 0.0;
            for (const cd& v : Y) yPower += absSq(v);
            yPower /= static_cast<double>(T) * O;
            std::fill(w.begin(), w.end(), 1.0);
            H = H0;
            for (int it = 0; it < cfg.robustIterations; ++it) {
                gram.assign(static_cast<std::size_t>(D) * D, cd(0, 0));
                rhs.assign(static_cast<std::size_t>(D) * O, cd(0, 0));
                for (int t = 0; t < T; ++t) {
                    const double wt = w[static_cast<std::size_t>(t)];
                    for (int d = 0; d < D; ++d) {
                        const cd xc = std::conj(X[static_cast<std::size_t>(t * D + d)]) * wt;
                        for (int j = 0; j < D; ++j) gram[static_cast<std::size_t>(d * D + j)] += xc * X[static_cast<std::size_t>(t * D + j)];
                        for (int o = 0; o < O; ++o) rhs[static_cast<std::size_t>(d * O + o)] += xc * Y[static_cast<std::size_t>(t * O + o)];
                    }
                }
                for (int d = 0; d < D; ++d) {
                    gram[static_cast<std::size_t>(d * D + d)] += diag[static_cast<std::size_t>(d)];
                    for (int o = 0; o < O; ++o) rhs[static_cast<std::size_t>(d * O + o)] += diag[static_cast<std::size_t>(d)] * H0[static_cast<std::size_t>(d * O + o)];
                }
                if (!solveComplex(gram, rhs, D, O)) { finite = false; break; }
                H = rhs;
                // Huber の重みは出力 channel をまとめた誤差で決める。
                for (int t = 0; t < T; ++t) {
                    double s = 0.0;
                    for (int o = 0; o < O; ++o) {
                        cd pred(0, 0);
                        for (int d = 0; d < D; ++d) pred += X[static_cast<std::size_t>(t * D + d)] * H[static_cast<std::size_t>(d * O + o)];
                        s += absSq(Y[static_cast<std::size_t>(t * O + o)] - pred);
                    }
                    mag[static_cast<std::size_t>(t)] = std::sqrt(s / O);
                }
                double scale = median(mag) * 1.4826;
                scale = std::max(scale, std::sqrt(yPower) * 1e-5 + 1e-12);
                for (int t = 0; t < T; ++t) {
                    double wt = std::min(1.0, cfg.huberDelta * scale / std::max(mag[static_cast<std::size_t>(t)], 1e-15));
                    w[static_cast<std::size_t>(t)] = std::max(wt, 0.015);
                }
            }
            for (int d = 0; d < D; ++d)
                for (int o = 0; o < O; ++o) h.at(f, d, o) = cf(H[static_cast<std::size_t>(d * O + o)]);
        }
    });
    if (cfg.smoothBins > 0) gaussianComplex(h.v.data(), F, D, O, 0, cfg.smoothBins);
    for (const cf& v : h.v)
        if (!std::isfinite(v.real()) || !std::isfinite(v.imag())) finite = false;
    if (!finite) throw std::runtime_error("the transfer estimate is not finite");
    return h;
}


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


namespace {

// 2 x 2 までの Hermitian 行列の固有分解。vals 昇順、vecs は列ベクトル (行優先 [行][列])。
void eigh(const cd* m, int n, double* vals, cd* vecs) {
    if (n == 1) {
        vals[0] = m[0].real();
        vecs[0] = cd(1, 0);
        return;
    }
    const double a = m[0].real(), d = m[3].real();
    const cd b = m[1];
    const double tr = 0.5 * (a + d), diff = 0.5 * (a - d);
    const double r = std::sqrt(diff * diff + absSq(b));
    vals[0] = tr - r;
    vals[1] = tr + r;
    if (std::abs(b) < 1e-300) {
        // 対角。値の小さい方を先に。
        if (a <= d) { vecs[0] = 1; vecs[1] = 0; vecs[2] = 0; vecs[3] = 1; }
        else { vecs[0] = 0; vecs[1] = 1; vecs[2] = 1; vecs[3] = 0; }
        return;
    }
    for (int k = 0; k < 2; ++k) {
        // 固有ベクトルは (A - lambda I) v = 0 から v = (b, lambda - a) で取る。
        cd v0 = b, v1 = cd(vals[k] - a, 0);
        double nn = std::sqrt(absSq(v0) + absSq(v1));
        if (nn < 1e-300) { v0 = cd(vals[k] - d, 0); v1 = std::conj(b); nn = std::sqrt(absSq(v0) + absSq(v1)); }
        vecs[0 * 2 + k] = v0 / nn;
        vecs[1 * 2 + k] = v1 / nn;
    }
}

}

Spec spatialBackground(const Spec& mixture, const Spec& powerReference, const Config& cfg) {
    const int F = mixture.F, T = mixture.T, C = mixture.C;
    const std::size_t FT = static_cast<std::size_t>(F) * T;
    const int CC = C * C;
    auto covariance = [&](const Spec& z) {
        std::vector<cf> cov(FT * CC);
        for (int f = 0; f < F; ++f)
            for (int t = 0; t < T; ++t)
                for (int i = 0; i < C; ++i)
                    for (int j = 0; j < C; ++j)
                        cov[(static_cast<std::size_t>(f) * T + t) * CC + i * C + j] = z.at(f, t, i) * std::conj(z.at(f, t, j));
        gaussianComplex(cov.data(), F, T, CC, 0, 0.8);
        gaussianComplex(cov.data(), F, T, CC, 1, std::max(cfg.covarianceFrames / 6.0, 0.5));
        for (std::size_t p = 0; p < FT; ++p)
            for (int i = 0; i < C; ++i)
                for (int j = i; j < C; ++j) {
                    cf& a = cov[p * CC + i * C + j];
                    cf& b = cov[p * CC + j * C + i];
                    const cf m = 0.5f * (a + std::conj(b));
                    a = m;
                    b = std::conj(m);
                }
        return cov;
    };
    const std::vector<cf> cy = covariance(mixture), ca = covariance(powerReference);
    const double floorGain = std::pow(10.0, cfg.maskFloorDb / 20.0);
    Spec out;
    out.resize(F, T, C);
    parallelFor(static_cast<long long>(FT), 1024, [&](long long b, long long e) {
        for (long long p = b; p < e; ++p) {
            const int f = static_cast<int>(p / T), t = static_cast<int>(p % T);
            cd Y[4], Aa[4];
            for (int i = 0; i < CC; ++i) {
                Y[i] = cd(cy[static_cast<std::size_t>(p) * CC + i]);
                Aa[i] = cd(ca[static_cast<std::size_t>(p) * CC + i]);
            }
            double pw = 0;
            for (int i = 0; i < C; ++i) pw += Y[i * C + i].real();
            pw /= C;
            const double fl = std::max(pw * cfg.covarianceLoading, 1e-16);
            for (int i = 0; i < C; ++i) Y[i * C + i] += fl;
            double ev[2];
            cd U[4];
            eigh(Y, C, ev, U);
            for (int i = 0; i < C; ++i) ev[i] = std::max(ev[i], fl);
            // R^{1/2} と R^{-1/2} を使う。
            cd root[4] = {}, inv[4] = {};
            for (int i = 0; i < C; ++i)
                for (int j = 0; j < C; ++j)
                    for (int k = 0; k < C; ++k) {
                        root[i * C + j] += U[i * C + k] * std::sqrt(ev[k]) * std::conj(U[j * C + k]);
                        inv[i * C + j] += U[i * C + k] * (1.0 / std::sqrt(ev[k])) * std::conj(U[j * C + k]);
                    }
            auto mul = [&](const cd* a, const cd* bb, cd* r) {
                for (int i = 0; i < C; ++i)
                    for (int j = 0; j < C; ++j) {
                        cd s(0, 0);
                        for (int k = 0; k < C; ++k) s += a[i * C + k] * bb[k * C + j];
                        r[i * C + j] = s;
                    }
            };
            cd tmp[4], wa[4];
            mul(inv, Aa, tmp);
            mul(tmp, inv, wa);
            for (int i = 0; i < C; ++i)
                for (int j = i; j < C; ++j) {
                    const cd m = 0.5 * (wa[i * C + j] + std::conj(wa[j * C + i]));
                    wa[i * C + j] = m;
                    wa[j * C + i] = std::conj(m);
                }
            double lam[2];
            cd Qv[4];
            eigh(wa, C, lam, Qv);
            cd mid[4] = {};
            for (int i = 0; i < C; ++i)
                for (int j = 0; j < C; ++j)
                    for (int k = 0; k < C; ++k) {
                        const double g = std::clamp(1.0 - lam[k], floorGain, 1.0);
                        mid[i * C + j] += Qv[i * C + k] * g * std::conj(Qv[j * C + k]);
                    }
            cd filt[4], t2[4];
            mul(root, mid, t2);
            mul(t2, inv, filt);
            for (int i = 0; i < C; ++i) {
                cd s(0, 0);
                for (int j = 0; j < C; ++j) s += filt[i * C + j] * cd(mixture.at(f, t, j));
                out.at(f, t, i) = mixture.at(f, t, i) - cf(s);
            }
        }
    });
    return out;
}


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
