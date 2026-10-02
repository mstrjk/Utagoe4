#include "hp_grid.h"
#include "parallel.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace utagoe {
namespace hp {
namespace {

std::vector<cd> solveComplex(std::vector<cd> a, std::vector<cd> b, int n) {
    for (int c = 0; c < n; ++c) {
        int piv = c;
        for (int r = c + 1; r < n; ++r)
            if (std::abs(a[static_cast<std::size_t>(r) * n + c]) > std::abs(a[static_cast<std::size_t>(piv) * n + c])) piv = r;
        if (piv != c) {
            for (int k = 0; k < n; ++k) std::swap(a[static_cast<std::size_t>(c) * n + k], a[static_cast<std::size_t>(piv) * n + k]);
            std::swap(b[static_cast<std::size_t>(c)], b[static_cast<std::size_t>(piv)]);
        }
        const cd d = a[static_cast<std::size_t>(c) * n + c];
        if (d == cd(0, 0)) continue;
        for (int r = c + 1; r < n; ++r) {
            const cd f = a[static_cast<std::size_t>(r) * n + c] / d;
            for (int k = c; k < n; ++k) a[static_cast<std::size_t>(r) * n + k] -= f * a[static_cast<std::size_t>(c) * n + k];
            b[static_cast<std::size_t>(r)] -= f * b[static_cast<std::size_t>(c)];
        }
    }
    std::vector<cd> x(static_cast<std::size_t>(n));
    for (int r = n - 1; r >= 0; --r) {
        cd acc = b[static_cast<std::size_t>(r)];
        for (int k = r + 1; k < n; ++k) acc -= a[static_cast<std::size_t>(r) * n + k] * x[static_cast<std::size_t>(k)];
        const cd d = a[static_cast<std::size_t>(r) * n + r];
        x[static_cast<std::size_t>(r)] = d != cd(0, 0) ? acc / d : cd(0, 0);
    }
    return x;
}

constexpr int kLags[5] = {-2, -1, 0, 1, 2};

cd lagged(const CGrid& x, int f, int t, int lag) {
    const int s = t - lag;
    return (s >= 0 && s < x.T) ? x.at(f, s) : cd(0, 0);
}

std::vector<std::array<cd, 5>> ctfFit(const CGrid& xs, const CGrid& target, const std::vector<double>& mask,
                                      const std::vector<double>& basePower) {
    const int F = xs.F, T = xs.T;
    std::vector<std::array<cd, 5>> coeff(static_cast<std::size_t>(F));
    const double penalty[5] = {6, 3, 1, 3, 6};
    parallelFor(F, 8, [&](long long b, long long e) {
        std::vector<double> w(static_cast<std::size_t>(T));
        std::vector<cd> err(static_cast<std::size_t>(T));
        for (long long fl = b; fl < e; ++fl) {
            const int f = static_cast<int>(fl);
            for (int t = 0; t < T; ++t) w[static_cast<std::size_t>(t)] = mask[static_cast<std::size_t>(t)];
            std::array<cd, 5> c{};
            for (int it = 0; it < 3; ++it) {
                std::vector<cd> A(25, cd(0, 0)), rhs(5, cd(0, 0));
                for (int t = 0; t < T; ++t) {
                    const double wt = w[static_cast<std::size_t>(t)];
                    if (wt == 0.0) continue;
                    cd d[5];
                    for (int p = 0; p < 5; ++p) d[p] = lagged(xs, f, t, kLags[p]);
                    const cd y = target.at(f, t);
                    for (int p = 0; p < 5; ++p) {
                        const cd dc = std::conj(d[p]) * wt;
                        rhs[static_cast<std::size_t>(p)] += dc * y;
                        for (int q = 0; q < 5; ++q) A[static_cast<std::size_t>(p) * 5 + q] += dc * d[q];
                    }
                }
                double tr = 0;
                for (int p = 0; p < 5; ++p) tr += A[static_cast<std::size_t>(p) * 5 + p].real();
                const double scale = std::max(tr / 5, 1e-12);
                for (int p = 0; p < 5; ++p) A[static_cast<std::size_t>(p) * 5 + p] += scale * 0.025 * penalty[p] + 1e-12;
                const std::vector<cd> sol = solveComplex(A, rhs, 5);
                for (int p = 0; p < 5; ++p) c[static_cast<std::size_t>(p)] = sol[static_cast<std::size_t>(p)];
                double ms = 0;
                for (int t = 0; t < T; ++t) {
                    cd pr(0, 0);
                    for (int p = 0; p < 5; ++p) pr += lagged(xs, f, t, kLags[p]) * c[static_cast<std::size_t>(p)];
                    err[static_cast<std::size_t>(t)] = target.at(f, t) - pr;
                    ms += std::norm(err[static_cast<std::size_t>(t)]);
                }
                const double sigma = std::sqrt(ms / T + 0.005 * basePower[static_cast<std::size_t>(f)] + 1e-12);
                for (int t = 0; t < T; ++t)
                    w[static_cast<std::size_t>(t)] = mask[static_cast<std::size_t>(t)] * std::min(1.0, 1.5 * sigma / (std::abs(err[static_cast<std::size_t>(t)]) + 1e-12));
            }
            coeff[static_cast<std::size_t>(f)] = c;
        }
    });
    return coeff;
}

}

Spec ctf(const Spec& Y, const Spec& X, double sr, int nFft, int hop) {
    const CGrid ys = sideOf(Y), xs = sideOf(X);
    const int F = xs.F, T = xs.T;
    CGrid target(F, T);
    for (std::size_t i = 0; i < target.v.size(); ++i) target.v[i] = ys.v[i] - xs.v[i];
    std::vector<double> power(static_cast<std::size_t>(F), 0.0);
    for (int f = 0; f < F; ++f) {
        double s = 0;
        for (int t = 0; t < T; ++t) s += std::norm(xs.at(f, t));
        power[static_cast<std::size_t>(f)] = s / T;
    }
    const double span = 1.5;
    std::vector<double> train(static_cast<std::size_t>(T)), test(static_cast<std::size_t>(T));
    double ntrain = 0, ntest = 0;
    for (int t = 0; t < T; ++t) {
        const double time = static_cast<double>(t) * hop / sr;
        const double ph = std::fmod(time, span);
        const bool fold = static_cast<long long>(std::floor(time / span)) % 2 == 0;
        const bool guard = ph > 0.15 && ph < span - 0.15;
        train[static_cast<std::size_t>(t)] = fold && guard;
        test[static_cast<std::size_t>(t)] = !fold && guard;
        ntrain += train[static_cast<std::size_t>(t)];
        ntest += test[static_cast<std::size_t>(t)];
    }
    if (ntrain < 20 || ntest < 20) return X;
    const auto hcTrain = ctfFit(xs, target, train, power);
    std::vector<double> e0(static_cast<std::size_t>(F), 0.0), e1(static_cast<std::size_t>(F), 0.0);
    for (int f = 0; f < F; ++f)
        for (int t = 0; t < T; ++t) {
            if (test[static_cast<std::size_t>(t)] == 0.0) continue;
            cd pr(0, 0);
            for (int p = 0; p < 5; ++p) pr += lagged(xs, f, t, kLags[p]) * hcTrain[static_cast<std::size_t>(f)][static_cast<std::size_t>(p)];
            e0[static_cast<std::size_t>(f)] += std::norm(target.at(f, t));
            e1[static_cast<std::size_t>(f)] += std::norm(target.at(f, t) - pr);
        }
    gaussianReflect(e0, 2.0);
    gaussianReflect(e1, 2.0);
    const std::vector<double> lb = lowBand(nFft, sr);
    std::vector<double> accept(static_cast<std::size_t>(F));
    for (int f = 0; f < F; ++f)
        accept[static_cast<std::size_t>(f)] = std::clamp((e0[static_cast<std::size_t>(f)] - e1[static_cast<std::size_t>(f)]) /
                                                         std::max(e0[static_cast<std::size_t>(f)], 1e-15) / 0.10, 0.0, 1.0) * lb[static_cast<std::size_t>(f)];
    const auto hc = ctfFit(xs, target, std::vector<double>(static_cast<std::size_t>(T), 1.0), power);
    Grid energy(F, T);
    for (int f = 0; f < F; ++f)
        for (int t = 0; t < T; ++t) {
            double s = 0;
            for (int c = 0; c < X.C; ++c) s += std::norm(cd(X.at(f, t, c)));
            energy.at(f, t) = s;
        }
    smooth(energy, 0, 2.0);
    Spec out = X;
    parallelFor(F, 16, [&](long long b, long long e) {
        for (long long fl = b; fl < e; ++fl) {
            const int f = static_cast<int>(fl);
            for (int t = 0; t < T; ++t) {
                cd corr[2] = {cd(0, 0), cd(0, 0)};
                for (int c = 0; c < X.C && c < 2; ++c)
                    for (int p = 0; p < 5; ++p) {
                        const int s = t - kLags[p];
                        if (s >= 0 && s < T) corr[c] += cd(X.at(f, s, c)) * hc[static_cast<std::size_t>(f)][static_cast<std::size_t>(p)];
                    }
                const double limit = 0.45 * std::sqrt(energy.at(f, t) + 1e-15);
                const double norm = std::sqrt(std::norm(corr[0]) + std::norm(corr[1]));
                const double k = std::min(1.0, limit / (norm + 1e-15)) * accept[static_cast<std::size_t>(f)];
                for (int c = 0; c < X.C && c < 2; ++c) out.at(f, t, c) = cf(cd(X.at(f, t, c)) + corr[c] * k);
            }
        }
    });
    return out;
}
}
}
