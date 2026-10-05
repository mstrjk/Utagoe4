#include "rc_internal.h"
#include "mathconst.h"
#include "parallel.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace utagoe {
namespace rc {

namespace {


double erbRate(double hz) { return 21.4 * std::log10(1.0 + 0.00437 * hz); }

struct Bands {
    int count = 0;
    std::vector<int> owner;                 // [F]
    std::vector<std::vector<std::pair<int, double>>> weights;   // [band] -> (bin, weight)
    std::vector<double> binNorm;            // [F]
};

Bands makeBands(int F, int nFft, int sr, int wanted) {
    Bands b;
    const double top = erbRate(sr / 2.0);
    b.count = std::max(4, wanted);
    std::vector<double> centre(static_cast<std::size_t>(b.count) + 2);
    for (int i = 0; i < b.count + 2; ++i) centre[static_cast<std::size_t>(i)] = top * i / (b.count + 1);
    b.weights.assign(static_cast<std::size_t>(b.count), {});
    b.owner.assign(static_cast<std::size_t>(F), 0);
    b.binNorm.assign(static_cast<std::size_t>(F), 0.0);
    std::vector<double> best(static_cast<std::size_t>(F), -1.0);
    for (int f = 0; f < F; ++f) {
        const double e = erbRate(static_cast<double>(f) * sr / nFft);
        for (int k = 0; k < b.count; ++k) {
            const double lo = centre[static_cast<std::size_t>(k)], mid = centre[static_cast<std::size_t>(k) + 1], hi = centre[static_cast<std::size_t>(k) + 2];
            double w = 0.0;
            if (e >= lo && e <= mid) w = (e - lo) / std::max(mid - lo, 1e-9);
            else if (e > mid && e <= hi) w = (hi - e) / std::max(hi - mid, 1e-9);
            if (k == 0 && e < mid) w = 1.0;
            if (k == b.count - 1 && e > mid) w = 1.0;
            if (w <= 0.0) continue;
            b.weights[static_cast<std::size_t>(k)].emplace_back(f, w);
            b.binNorm[static_cast<std::size_t>(f)] += w;
            if (w > best[static_cast<std::size_t>(f)]) {
                best[static_cast<std::size_t>(f)] = w;
                b.owner[static_cast<std::size_t>(f)] = k;
            }
        }
    }
    return b;
}

double expint1(double x) {
    if (x <= 0.0) return 50.0;
    if (x < 1.0) {
        const double euler = 0.5772156649015329;
        double sum = 0.0, term = 1.0;
        for (int n = 1; n < 30; ++n) {
            term *= -x / n;
            sum += term / n;
        }
        return -euler - std::log(x) - sum;
    }
    const double num = x * x + 2.334733 * x + 0.250621, den = x * x + 3.330657 * x + 1.681534;
    return std::exp(-x) / x * (num / den);
}

struct Canceller {
    int taps = 0, ahead = 0;
    std::vector<cd> trusted, trial;
};

}

Spec adaptiveBackground(const Spec& mixture, const Spec& baseline, const Config& cfg, int sr) {
    const int F = mixture.F, T = mixture.T, C = mixture.C;
    const int ahead = std::max(0, cfg.adaptiveAhead), past = std::max(0, cfg.adaptivePast);
    const int L = ahead + past + 1;
    const Bands bands = makeBands(F, cfg.nFft, sr, cfg.adaptiveBands);
    const int B = bands.count;

    std::vector<std::vector<int>> members(static_cast<std::size_t>(B));
    for (int f = 0; f < F; ++f) members[static_cast<std::size_t>(bands.owner[static_cast<std::size_t>(f)])].push_back(f);

    Spec predicted;
    predicted.resize(F, T, C);
    std::vector<double> leakTrack(static_cast<std::size_t>(B) * T, 0.0);

    parallelFor(B, 1, [&](long long b0, long long b1) {
        for (long long band = b0; band < b1; ++band) {
            const auto& bins = members[static_cast<std::size_t>(band)];
            if (bins.empty()) continue;
            const std::size_t nb = bins.size();
            std::vector<cd> wT(nb * C * L, cd(0, 0)), wA(nb * C * L, cd(0, 0));
            for (std::size_t i = 0; i < nb * C; ++i) wT[i * L + ahead] = wA[i * L + ahead] = cd(1, 0);
            std::vector<double> norm(nb * C, 0.0);
            double mE = 0, mY = 0, mEY = 0, mYY = 0, leak = cfg.adaptiveInitialLeak;
            double sT = 0, sA = 0;
            int better = 0;
            const double aStat = cfg.adaptiveStatSmoothing;
            std::vector<cd> u(static_cast<std::size_t>(L));
            for (int pass = 0; pass < std::max(1, cfg.adaptivePasses); ++pass) {
                const bool last = pass == std::max(1, cfg.adaptivePasses) - 1;
                for (int t = 0; t < T; ++t) {
                    double eT = 0, eA = 0, yPow = 0, ePow = 0;
                    for (std::size_t j = 0; j < nb; ++j) {
                        const int f = bins[j];
                        for (int c = 0; c < C; ++c) {
                            const std::size_t s = j * C + c;
                            double pu = 0.0;
                            for (int k = 0; k < L; ++k) {
                                const int tt = t + ahead - k;
                                u[static_cast<std::size_t>(k)] = (tt >= 0 && tt < T) ? cd(baseline.at(f, tt, c)) : cd(0, 0);
                                pu += absSq(u[static_cast<std::size_t>(k)]);
                            }
                            norm[s] = norm[s] == 0.0 ? pu : 0.7 * norm[s] + 0.3 * pu;
                            cd pT(0, 0), pA(0, 0);
                            for (int k = 0; k < L; ++k) {
                                pT += wT[s * L + k] * u[static_cast<std::size_t>(k)];
                                pA += wA[s * L + k] * u[static_cast<std::size_t>(k)];
                            }
                            const cd y(mixture.at(f, t, c));
                            const cd errT = y - pT, errA = y - pA;
                            if (last) predicted.at(f, t, c) = cf(static_cast<float>(pT.real()), static_cast<float>(pT.imag()));
                            eT += absSq(errT);
                            eA += absSq(errA);
                            yPow += absSq(pT);
                            ePow += absSq(errT);
                            const double predictable = leak * absSq(pT);
                            const double mu = cfg.adaptiveMaxStep * std::min(1.0, predictable / (absSq(errA) + 1e-12));
                            if (mu > 1e-6 && pu > 1e-14) {
                                const double g = mu / (norm[s] + cfg.adaptiveRegularization * norm[s] + 1e-14);
                                for (int k = 0; k < L; ++k) wA[s * L + k] += g * errA * std::conj(u[static_cast<std::size_t>(k)]);
                            }
                        }
                    }
                    mE = aStat * mE + (1 - aStat) * ePow;
                    mY = aStat * mY + (1 - aStat) * yPow;
                    mEY = aStat * mEY + (1 - aStat) * ePow * yPow;
                    mYY = aStat * mYY + (1 - aStat) * yPow * yPow;
                    const double var = mYY - mY * mY;
                    if (var > 1e-24) leak = std::clamp((mEY - mE * mY) / var, cfg.adaptiveMinLeak, 1.0);
                    if (last) leakTrack[static_cast<std::size_t>(band) * T + t] = leak;

                    sT = 0.9 * sT + 0.1 * eT;
                    sA = 0.9 * sA + 0.1 * eA;
                    if (sA < sT * cfg.adaptivePromoteRatio) {
                        if (++better >= cfg.adaptivePromoteFrames) {
                            wT = wA;
                            sT = sA;
                            better = 0;
                        }
                    } else {
                        better = 0;
                        if (sA > sT * cfg.adaptiveResetRatio) {
                            wA = wT;
                            sA = sT;
                        }
                    }
                }
            }
        }
    });

    Spec residual;
    residual.resize(F, T, C);
    for (std::size_t i = 0; i < residual.v.size(); ++i) residual.v[i] = mixture.v[i] - predicted.v[i];

    // 残った instrumental だけを、声がありそうな所では控えめに抑える (帯域ごとの決定指向 SNR と存在確率)。
    std::vector<double> pE(static_cast<std::size_t>(B) * T, 0.0), pL(static_cast<std::size_t>(B) * T, 0.0);
    std::vector<double> leakBin(static_cast<std::size_t>(F) * T, 0.0);
    parallelFor(T, 16, [&](long long t0, long long t1) {
        for (long long t = t0; t < t1; ++t)
            for (int f = 0; f < F; ++f) {
                double y = 0.0;
                for (int c = 0; c < C; ++c) y += absSq(predicted.at(f, static_cast<int>(t), c));
                leakBin[static_cast<std::size_t>(f) * T + t] = leakTrack[static_cast<std::size_t>(bands.owner[static_cast<std::size_t>(f)]) * T + t] * y;
            }
    });
    parallelFor(F, 32, [&](long long f0, long long f1) {
        for (long long f = f0; f < f1; ++f) {
            double held = 0.0;
            for (int t = 0; t < T; ++t) {
                double& v = leakBin[static_cast<std::size_t>(f) * T + t];
                held = std::max(v, held * cfg.adaptiveLeakRelease);
                v = held;
            }
        }
    });
    parallelFor(T, 16, [&](long long t0, long long t1) {
        for (long long t = t0; t < t1; ++t)
            for (int b = 0; b < B; ++b) {
                double e = 0.0, l = 0.0;
                for (const auto& [f, w] : bands.weights[static_cast<std::size_t>(b)]) {
                    double r = 0.0;
                    for (int c = 0; c < C; ++c) r += absSq(residual.at(f, static_cast<int>(t), c));
                    e += w * r;
                    l += w * leakBin[static_cast<std::size_t>(f) * T + t];
                }
                pE[static_cast<std::size_t>(b) * T + t] = e;
                pL[static_cast<std::size_t>(b) * T + t] = l;
            }
    });

    const double floorGain = std::pow(10.0, cfg.adaptiveFloorDb / 20.0);
    const double xiMin = std::pow(10.0, -25.0 / 10.0);
    const double absence = std::clamp(cfg.adaptiveVocalAbsence, 0.01, 0.99);
    std::vector<double> gain(static_cast<std::size_t>(B) * T, 1.0);
    parallelFor(B, 1, [&](long long b0, long long b1) {
        for (long long b = b0; b < b1; ++b) {
            double prevClean = 0.0, prevLeak = 1e-20;
            for (int t = 0; t < T; ++t) {
                const std::size_t i = static_cast<std::size_t>(b) * T + t;
                const double lambda = pL[i] + 1e-20;
                const double gamma = std::min(pE[i] / lambda, 1e4);
                const double xi = std::max(xiMin, cfg.adaptiveDecision * prevClean / std::max(prevLeak, 1e-20) +
                                                    (1 - cfg.adaptiveDecision) * std::max(gamma - 1.0, 0.0));
                const double v = xi * gamma / (1.0 + xi);
                const double lsa = std::min(1.0, xi / (1.0 + xi) * std::exp(0.5 * expint1(std::max(v, 1e-8))));
                const double presence = 1.0 / (1.0 + absence / (1.0 - absence) * (1.0 + xi) * std::exp(-std::min(v, 50.0)));
                const double g = std::pow(lsa, presence) * std::pow(floorGain, 1.0 - presence);
                gain[i] = std::clamp(g, floorGain, 1.0);
                prevClean = gain[i] * gain[i] * pE[i];
                prevLeak = lambda;
            }
        }
    });

    Spec background;
    background.resize(F, T, C);
    parallelFor(T, 16, [&](long long t0, long long t1) {
        for (long long t = t0; t < t1; ++t) {
            std::vector<double> g(static_cast<std::size_t>(F), 0.0);
            for (int b = 0; b < B; ++b)
                for (const auto& [f, w] : bands.weights[static_cast<std::size_t>(b)])
                    g[static_cast<std::size_t>(f)] += w * gain[static_cast<std::size_t>(b) * T + t];
            for (int f = 0; f < F; ++f) {
                const double gf = g[static_cast<std::size_t>(f)] / std::max(bands.binNorm[static_cast<std::size_t>(f)], 1e-12);
                for (int c = 0; c < C; ++c) {
                    const cf y = mixture.at(f, static_cast<int>(t), c);
                    const cf v = residual.at(f, static_cast<int>(t), c) * static_cast<float>(gf);
                    background.at(f, static_cast<int>(t), c) = y - v;
                }
            }
        }
    });
    return background;
}

}
}
