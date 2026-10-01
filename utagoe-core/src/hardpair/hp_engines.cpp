#include "hp.h"
#include "../parallel.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace utagoe {
namespace hp {
namespace {

constexpr double kPi = 3.14159265358979323846;

struct Grid {
    int F = 0, T = 0;
    std::vector<double> v;
    Grid() = default;
    Grid(int f, int t, double fill = 0.0) : F(f), T(t), v(static_cast<std::size_t>(f) * t, fill) {}
    double& at(int f, int t) { return v[static_cast<std::size_t>(f) * T + t]; }
    double at(int f, int t) const { return v[static_cast<std::size_t>(f) * T + t]; }
};

struct CGrid {
    int F = 0, T = 0;
    std::vector<cd> v;
    CGrid() = default;
    CGrid(int f, int t, cd fill = cd(0, 0)) : F(f), T(t), v(static_cast<std::size_t>(f) * t, fill) {}
    cd& at(int f, int t) { return v[static_cast<std::size_t>(f) * T + t]; }
    cd at(int f, int t) const { return v[static_cast<std::size_t>(f) * T + t]; }
};

CGrid sideOf(const Spec& z) {
    CGrid s(z.F, z.T);
    for (int f = 0; f < z.F; ++f)
        for (int t = 0; t < z.T; ++t) s.at(f, t) = (cd(z.at(f, t, 0)) - cd(z.at(f, t, 1))) * 0.5;
    return s;
}

void smooth(Grid& g, double sf, double st) {
    if (sf > 0) rc::gaussian(g.v.data(), g.F, g.T, 1, 0, sf);
    if (st > 0) rc::gaussian(g.v.data(), g.F, g.T, 1, 1, st);
}

void smooth(CGrid& g, double sf, double st) {
    if (sf > 0) rc::gaussianComplex(g.v.data(), g.F, g.T, 1, 0, sf);
    if (st > 0) rc::gaussianComplex(g.v.data(), g.F, g.T, 1, 1, st);
}

Grid donut(const Grid& a, int wide, int gap) {
    Grid out(a.F, a.T);
    parallelFor(a.F, 16, [&](long long b, long long e) {
        std::vector<double> row(static_cast<std::size_t>(a.T));
        for (long long f = b; f < e; ++f) {
            for (int t = 0; t < a.T; ++t) row[static_cast<std::size_t>(t)] = a.at(static_cast<int>(f), t);
            const std::vector<double> big = boxSum(row, 2 * wide + 1, false), small = boxSum(row, 2 * gap + 1, false);
            for (int t = 0; t < a.T; ++t) out.at(static_cast<int>(f), t) = big[static_cast<std::size_t>(t)] - small[static_cast<std::size_t>(t)];
        }
    });
    return out;
}

CGrid donut(const CGrid& a, int wide, int gap) {
    Grid re(a.F, a.T), im(a.F, a.T);
    for (std::size_t i = 0; i < a.v.size(); ++i) { re.v[i] = a.v[i].real(); im.v[i] = a.v[i].imag(); }
    const Grid r = donut(re, wide, gap), m = donut(im, wide, gap);
    CGrid out(a.F, a.T);
    for (std::size_t i = 0; i < a.v.size(); ++i) out.v[i] = cd(r.v[i], m.v[i]);
    return out;
}

cd clipTransfer(cd h, double gmin, double gmax, double pmax) {
    return std::polar(std::clamp(std::abs(h), gmin, gmax), std::clamp(std::arg(h), -pmax, pmax));
}

std::vector<double> lowBand(int nFft, double sr) {
    std::vector<double> w(static_cast<std::size_t>(nFft / 2 + 1));
    for (std::size_t k = 0; k < w.size(); ++k) w[k] = std::clamp((static_cast<double>(k) * sr / nFft - 60.0) / 100.0, 0.0, 1.0);
    return w;
}

std::vector<double> rowMean(const Grid& g) {
    std::vector<double> m(static_cast<std::size_t>(g.F), 0.0);
    for (int f = 0; f < g.F; ++f) {
        double s = 0;
        for (int t = 0; t < g.T; ++t) s += g.at(f, t);
        m[static_cast<std::size_t>(f)] = s / g.T;
    }
    return m;
}

double meanOf(const Grid& g) {
    double s = 0;
    for (double v : g.v) s += v;
    return s / static_cast<double>(g.v.size());
}

Spec scaleBy(const Spec& X, const CGrid& h) {
    Spec out = X;
    for (int f = 0; f < X.F; ++f)
        for (int t = 0; t < X.T; ++t)
            for (int c = 0; c < X.C; ++c) out.at(f, t, c) = cf(cd(X.at(f, t, c)) * h.at(f, t));
    return out;
}

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

Spec phaseSurface(const Spec& Y, const Spec& X, double sr, int nFft, int hop) {
    const CGrid ys = sideOf(Y), xs = sideOf(X);
    const int F = xs.F, T = xs.T;
    Grid power(F, T);
    for (std::size_t i = 0; i < power.v.size(); ++i) power.v[i] = std::norm(xs.v[i]);
    const int wide = std::max(4, static_cast<int>(2.0 * sr / hop));
    const int gap = std::max(2, static_cast<int>(0.12 * sr / hop));
    const double floorv = std::max(meanOf(power) * 1e-5, 1e-12);
    CGrid h(F, T, cd(1, 0));
    for (int it = 0; it < 3; ++it) {
        Grid errPow(F, T), wpow(F, T);
        CGrid wcross(F, T);
        std::vector<double> errAbs(power.v.size());
        for (std::size_t i = 0; i < power.v.size(); ++i) {
            const cd err = ys.v[i] - h.v[i] * xs.v[i];
            errAbs[i] = std::abs(err);
            errPow.v[i] = errAbs[i] * errAbs[i];
        }
        smooth(errPow, 2.0, std::max(2.0, 0.25 * sr / hop));
        for (std::size_t i = 0; i < power.v.size(); ++i) {
            const double scale = std::sqrt(errPow.v[i] + floorv);
            const double w = std::min(1.0, 1.5 * scale / (errAbs[i] + floorv));
            wpow.v[i] = w * power.v[i];
            wcross.v[i] = w * (ys.v[i] - xs.v[i]) * std::conj(xs.v[i]);
        }
        Grid p = donut(wpow, wide, gap);
        smooth(p, 1.2, 0);
        CGrid cross = donut(wcross, wide, gap);
        smooth(cross, 1.2, 0);
        const std::vector<double> pm = rowMean(p);
        for (int f = 0; f < F; ++f) {
            const double prior = 0.025 * std::max(pm[static_cast<std::size_t>(f)], floorv);
            for (int t = 0; t < T; ++t) {
                const cd dh = cross.at(f, t) / (p.at(f, t) + prior + floorv);
                h.at(f, t) = clipTransfer(1.0 + dh, 0.72, 1.35, 0.50);
            }
        }
    }
    Grid py(F, T);
    CGrid yx(F, T);
    for (std::size_t i = 0; i < power.v.size(); ++i) {
        py.v[i] = std::norm(ys.v[i]);
        yx.v[i] = ys.v[i] * std::conj(xs.v[i]);
    }
    Grid powerY = donut(py, wide, gap);
    smooth(powerY, 1.2, 0);
    Grid powerX = donut(power, wide, gap);
    smooth(powerX, 1.2, 0);
    CGrid cross = donut(yx, wide, gap);
    smooth(cross, 1.2, 0);
    const std::vector<double> pxm = rowMean(powerX);
    const std::vector<double> lb = lowBand(nFft, sr);
    for (int f = 0; f < F; ++f)
        for (int t = 0; t < T; ++t) {
            const double coh = std::clamp(std::norm(cross.at(f, t)) / (powerX.at(f, t) * powerY.at(f, t) + floorv), 0.0, 1.0);
            double rel = std::clamp((coh - 0.35) / 0.5, 0.0, 1.0);
            rel *= powerX.at(f, t) / (powerX.at(f, t) + 0.02 * pxm[static_cast<std::size_t>(f)] + floorv);
            rel *= lb[static_cast<std::size_t>(f)];
            h.at(f, t) = 1.0 + rel * (h.at(f, t) - 1.0);
        }
    return scaleBy(X, h);
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

namespace {

void robustLowrank(std::vector<cd>& D, const std::vector<double>& W, int m, int n, std::vector<cd>& L) {
    const int rank = 6;
    const double tau = 0.45, lam = 0.06, step = 0.45;
    L.assign(D.size(), cd(0, 0));
    std::vector<cd> S(D.size(), cd(0, 0)), M(D.size()), Ln(D.size()), Sn(D.size());
    std::vector<double> weight(W.size());
    for (std::size_t i = 0; i < W.size(); ++i) weight[i] = W[i] * W[i];
    for (int it = 0; it < 24; ++it) {
        std::vector<cd> grad(D.size());
        for (std::size_t i = 0; i < D.size(); ++i) {
            grad[i] = weight[i] * (L[i] + S[i] - D[i]);
            M[i] = L[i] - step * grad[i];
        }
        const bool tall = m >= n;
        const int k = tall ? n : m;
        std::vector<cd> G(static_cast<std::size_t>(k) * k, cd(0, 0));
        if (tall) {
            for (int a = 0; a < n; ++a)
                for (int b = a; b < n; ++b) {
                    cd acc(0, 0);
                    for (int r = 0; r < m; ++r) acc += std::conj(M[static_cast<std::size_t>(r) * n + a]) * M[static_cast<std::size_t>(r) * n + b];
                    G[static_cast<std::size_t>(a) * k + b] = acc;
                    G[static_cast<std::size_t>(b) * k + a] = std::conj(acc);
                }
        } else {
            for (int a = 0; a < m; ++a)
                for (int b = a; b < m; ++b) {
                    cd acc(0, 0);
                    for (int c = 0; c < n; ++c) acc += M[static_cast<std::size_t>(a) * n + c] * std::conj(M[static_cast<std::size_t>(b) * n + c]);
                    G[static_cast<std::size_t>(a) * k + b] = acc;
                    G[static_cast<std::size_t>(b) * k + a] = std::conj(acc);
                }
        }
        std::vector<double> values;
        std::vector<cd> vecs;
        const int keep = std::min(rank, k);
        hermitianTopEigen(G, k, keep, values, vecs);
        std::fill(Ln.begin(), Ln.end(), cd(0, 0));
        for (int s = 0; s < keep; ++s) {
            const double sigma = std::sqrt(std::max(values[static_cast<std::size_t>(s)], 0.0));
            const double shrunk = std::max(sigma - step * tau, 0.0);
            if (shrunk <= 0 || sigma <= 0) continue;
            const double ratio = shrunk / sigma;
            if (tall) {
                std::vector<cd> u(static_cast<std::size_t>(m), cd(0, 0));
                for (int r = 0; r < m; ++r) {
                    cd acc(0, 0);
                    for (int c = 0; c < n; ++c) acc += M[static_cast<std::size_t>(r) * n + c] * vecs[static_cast<std::size_t>(c) * keep + s];
                    u[static_cast<std::size_t>(r)] = acc;
                }
                for (int r = 0; r < m; ++r)
                    for (int c = 0; c < n; ++c)
                        Ln[static_cast<std::size_t>(r) * n + c] += ratio * u[static_cast<std::size_t>(r)] * std::conj(vecs[static_cast<std::size_t>(c) * keep + s]);
            } else {
                std::vector<cd> v(static_cast<std::size_t>(n), cd(0, 0));
                for (int c = 0; c < n; ++c) {
                    cd acc(0, 0);
                    for (int r = 0; r < m; ++r) acc += std::conj(vecs[static_cast<std::size_t>(r) * keep + s]) * M[static_cast<std::size_t>(r) * n + c];
                    v[static_cast<std::size_t>(c)] = acc;
                }
                for (int r = 0; r < m; ++r)
                    for (int c = 0; c < n; ++c)
                        Ln[static_cast<std::size_t>(r) * n + c] += ratio * vecs[static_cast<std::size_t>(r) * keep + s] * v[static_cast<std::size_t>(c)];
            }
        }
        double dL = 0, dS = 0, nL = 0, nS = 0;
        for (std::size_t i = 0; i < D.size(); ++i) {
            const cd z = S[i] - step * grad[i];
            const double a = std::abs(z);
            Sn[i] = z * std::max(0.0, 1.0 - step * lam / (a + 1e-15));
            dL += std::norm(Ln[i] - L[i]);
            dS += std::norm(Sn[i] - S[i]);
            nL += std::norm(Ln[i]);
            nS += std::norm(Sn[i]);
        }
        L.swap(Ln);
        S.swap(Sn);
        if (std::sqrt(dL) + std::sqrt(dS) < 1e-5 * (1 + std::sqrt(nL) + std::sqrt(nS))) break;
    }
}

}

Spec lowrankField(const Spec& Y, const Spec& X, double sr, int nFft, int hop) {
    const CGrid ys = sideOf(Y), xs = sideOf(X);
    const int F = xs.F, T = xs.T;
    const double sf = 2.0, st = std::max(2.0, 0.08 * sr / hop);
    Grid p(F, T), q(F, T);
    CGrid c(F, T);
    for (std::size_t i = 0; i < p.v.size(); ++i) {
        p.v[i] = std::norm(xs.v[i]);
        q.v[i] = std::norm(ys.v[i]);
        c.v[i] = ys.v[i] * std::conj(xs.v[i]);
    }
    smooth(p, sf, st);
    smooth(q, sf, st);
    smooth(c, sf, st);
    const double floorv = std::max(meanOf(p) * 1e-5, 1e-15);
    const std::vector<double> pm = rowMean(p);
    const std::vector<double> lb = lowBand(nFft, sr);
    CGrid obs(F, T);
    Grid W(F, T);
    for (int f = 0; f < F; ++f)
        for (int t = 0; t < T; ++t) {
            const double pv = p.at(f, t);
            const double coh = std::clamp(std::norm(c.at(f, t)) / (pv * q.at(f, t) + floorv * floorv), 0.0, 1.0);
            cd o = (c.at(f, t) - pv) / (pv + 0.03 * pm[static_cast<std::size_t>(f)] + floorv);
            obs.at(f, t) = clipTransfer(1.0 + o, 0.65, 1.4, 0.55) - 1.0;
            W.at(f, t) = std::clamp((coh - 0.3) / 0.6, 0.0, 1.0) * pv / (pv + 0.05 * pm[static_cast<std::size_t>(f)] + floorv) * lb[static_cast<std::size_t>(f)];
        }
    const int fw = 4, tw = std::max(2, static_cast<int>(0.16 * sr / hop));
    std::vector<int> fi, ti;
    for (int f = 0; f < F; f += fw) fi.push_back(f);
    for (int t = 0; t < T; t += tw) ti.push_back(t);
    const int m = static_cast<int>(fi.size()), n = static_cast<int>(ti.size());
    std::vector<cd> D(static_cast<std::size_t>(m) * n);
    std::vector<double> Wc(static_cast<std::size_t>(m) * n);
    for (int a = 0; a < m; ++a)
        for (int b = 0; b < n; ++b) {
            D[static_cast<std::size_t>(a) * n + b] = obs.at(fi[static_cast<std::size_t>(a)], ti[static_cast<std::size_t>(b)]);
            Wc[static_cast<std::size_t>(a) * n + b] = W.at(fi[static_cast<std::size_t>(a)], ti[static_cast<std::size_t>(b)]);
        }
    std::vector<cd> L;
    robustLowrank(D, Wc, m, n, L);
    std::vector<double> tix(ti.begin(), ti.end()), fix(fi.begin(), fi.end());
    CGrid a(m, T);
    for (int j = 0; j < m; ++j) {
        std::vector<double> re(static_cast<std::size_t>(n)), im(static_cast<std::size_t>(n));
        for (int b = 0; b < n; ++b) { re[static_cast<std::size_t>(b)] = L[static_cast<std::size_t>(j) * n + b].real(); im[static_cast<std::size_t>(b)] = L[static_cast<std::size_t>(j) * n + b].imag(); }
        for (int t = 0; t < T; ++t) a.at(j, t) = cd(interp(t, tix, re), interp(t, tix, im));
    }
    CGrid full(F, T);
    for (int t = 0; t < T; ++t) {
        std::vector<double> re(static_cast<std::size_t>(m)), im(static_cast<std::size_t>(m));
        for (int j = 0; j < m; ++j) { re[static_cast<std::size_t>(j)] = a.at(j, t).real(); im[static_cast<std::size_t>(j)] = a.at(j, t).imag(); }
        for (int f = 0; f < F; ++f) full.at(f, t) = cd(interp(f, fix, re), interp(f, fix, im));
    }
    smooth(full, 1.0, std::max(1.0, 0.04 * sr / hop));
    CGrid h(F, T);
    for (std::size_t i = 0; i < h.v.size(); ++i) h.v[i] = clipTransfer(1.0 + full.v[i] * W.v[i], 0.72, 1.35, 0.5);
    return scaleBy(X, h);
}

namespace {

std::vector<double> trendFilter(const std::vector<double>& obs, const std::vector<double>& w) {
    const long long n = static_cast<long long>(obs.size());
    if (n < 4) return obs;
    const double lam = 0.04, ridge = 0.01, rho = 2.0;
    std::vector<std::array<double, 3>> band(static_cast<std::size_t>(n), {0.0, 0.0, 0.0});
    for (long long i = 0; i < n; ++i) band[static_cast<std::size_t>(i)][0] = w[static_cast<std::size_t>(i)] + ridge;
    for (long long r = 0; r + 2 < n; ++r) {
        const double d[3] = {1, -2, 1};
        for (int a = 0; a < 3; ++a)
            for (int b = a; b < 3; ++b) band[static_cast<std::size_t>(r + a)][static_cast<std::size_t>(b - a)] += rho * d[a] * d[b];
    }
    std::vector<std::array<double, 3>> Lc(static_cast<std::size_t>(n), {0.0, 0.0, 0.0});
    for (long long i = 0; i < n; ++i) {
        for (int k = 2; k >= 1; --k) {
            const long long j = i - k;
            if (j < 0) continue;
            double s = band[static_cast<std::size_t>(j)][static_cast<std::size_t>(k)];
            for (int m = 1; m <= 2; ++m) {
                const long long p = j - m;
                if (p < 0 || i - p > 2) continue;
                s -= Lc[static_cast<std::size_t>(i)][static_cast<std::size_t>(i - p)] * Lc[static_cast<std::size_t>(j)][static_cast<std::size_t>(j - p)];
            }
            Lc[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)] = s / Lc[static_cast<std::size_t>(j)][0];
        }
        double s = band[static_cast<std::size_t>(i)][0];
        for (int k = 1; k <= 2; ++k)
            if (i - k >= 0) s -= Lc[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)] * Lc[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)];
        Lc[static_cast<std::size_t>(i)][0] = std::sqrt(std::max(s, 1e-300));
    }
    auto solve = [&](std::vector<double> b) {
        for (long long i = 0; i < n; ++i) {
            double s = b[static_cast<std::size_t>(i)];
            for (int k = 1; k <= 2; ++k)
                if (i - k >= 0) s -= Lc[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)] * b[static_cast<std::size_t>(i - k)];
            b[static_cast<std::size_t>(i)] = s / Lc[static_cast<std::size_t>(i)][0];
        }
        for (long long i = n - 1; i >= 0; --i) {
            double s = b[static_cast<std::size_t>(i)];
            for (int k = 1; k <= 2; ++k)
                if (i + k < n) s -= Lc[static_cast<std::size_t>(i + k)][static_cast<std::size_t>(k)] * b[static_cast<std::size_t>(i + k)];
            b[static_cast<std::size_t>(i)] = s / Lc[static_cast<std::size_t>(i)][0];
        }
        return b;
    };
    const long long nz = n - 2;
    auto applyD = [&](const std::vector<double>& x) {
        std::vector<double> d(static_cast<std::size_t>(nz));
        for (long long r = 0; r < nz; ++r) d[static_cast<std::size_t>(r)] = x[static_cast<std::size_t>(r)] - 2 * x[static_cast<std::size_t>(r + 1)] + x[static_cast<std::size_t>(r + 2)];
        return d;
    };
    std::vector<double> x = obs, z = applyD(x), u(static_cast<std::size_t>(nz), 0.0);
    for (int it = 0; it < 80; ++it) {
        std::vector<double> rhs(static_cast<std::size_t>(n));
        for (long long i = 0; i < n; ++i) rhs[static_cast<std::size_t>(i)] = w[static_cast<std::size_t>(i)] * obs[static_cast<std::size_t>(i)];
        for (long long r = 0; r < nz; ++r) {
            const double v = rho * (z[static_cast<std::size_t>(r)] - u[static_cast<std::size_t>(r)]);
            rhs[static_cast<std::size_t>(r)] += v;
            rhs[static_cast<std::size_t>(r + 1)] -= 2 * v;
            rhs[static_cast<std::size_t>(r + 2)] += v;
        }
        x = solve(rhs);
        const std::vector<double> dx = applyD(x);
        double pr = 0, du = 0;
        for (long long r = 0; r < nz; ++r) {
            const double old = z[static_cast<std::size_t>(r)];
            const double v = dx[static_cast<std::size_t>(r)] + u[static_cast<std::size_t>(r)];
            const double t = lam / rho;
            z[static_cast<std::size_t>(r)] = v * std::max(0.0, 1.0 - t / (std::abs(v) + 1e-15));
            u[static_cast<std::size_t>(r)] += dx[static_cast<std::size_t>(r)] - z[static_cast<std::size_t>(r)];
            pr += (dx[static_cast<std::size_t>(r)] - z[static_cast<std::size_t>(r)]) * (dx[static_cast<std::size_t>(r)] - z[static_cast<std::size_t>(r)]);
            du += (z[static_cast<std::size_t>(r)] - old) * (z[static_cast<std::size_t>(r)] - old);
        }
        const double primal = std::sqrt(pr) / std::sqrt(static_cast<double>(nz));
        const double dual = rho * std::sqrt(du) / std::sqrt(static_cast<double>(nz));
        if (primal < 1e-6 && dual < 1e-6) break;
    }
    return x;
}

std::vector<double> stationaryEq(const Audio& mix, const Audio& base, double sr) {
    const int nFft = 8192;
    PhaseSpectra s = phaseSpectra(mix, base, sr, nFft);
    const int half = static_cast<int>(s.f.size());
    const int n = (half - 1) * 2;
    CGrid hg(half, 1);
    for (int k = 0; k < half; ++k) hg.at(k, 0) = s.h[static_cast<std::size_t>(k)];
    smooth(hg, 1.5, 0);
    std::vector<cd> h(static_cast<std::size_t>(half));
    for (int k = 0; k < half; ++k) {
        const double w = std::clamp((s.coh[static_cast<std::size_t>(k)] - 0.4) / 0.5, 0.0, 1.0) *
                         std::clamp((s.f[static_cast<std::size_t>(k)] - 60) / 100, 0.0, 1.0);
        h[static_cast<std::size_t>(k)] = 1.0 + w * (clipTransfer(hg.at(k, 0), 0.8, 1.25, 0.4) - 1.0);
    }
    h[0] = h[0].real();
    h[static_cast<std::size_t>(half - 1)] = h[static_cast<std::size_t>(half - 1)].real();
    std::vector<cd> full(static_cast<std::size_t>(n));
    for (int k = 0; k < half; ++k) full[static_cast<std::size_t>(k)] = h[static_cast<std::size_t>(k)];
    for (int k = half; k < n; ++k) full[static_cast<std::size_t>(k)] = std::conj(h[static_cast<std::size_t>(n - k)]);
    rc::fftDouble(full, true);
    std::vector<double> imp(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) imp[static_cast<std::size_t>((i + n / 2) % n)] = full[static_cast<std::size_t>(i)].real();
    const double alpha = 0.5;
    const int width = static_cast<int>(std::floor(alpha * (n - 1) / 2.0));
    for (int i = 0; i < n; ++i) {
        double w = 1.0;
        if (i <= width) w = 0.5 * (1 + std::cos(kPi * (-1 + 2.0 * i / alpha / (n - 1))));
        else if (i >= n - width - 1) w = 0.5 * (1 + std::cos(kPi * (-2.0 / alpha + 1 + 2.0 * i / alpha / (n - 1))));
        imp[static_cast<std::size_t>(i)] *= w;
    }
    return imp;
}

}

Audio trendDemaster(const Audio& mix, const Audio& base, double sr) {
    const std::vector<double> imp = stationaryEq(mix, base, sr);
    const std::size_t n = base.frames();
    Audio ref;
    ref.channels = base.channels;
    ref.v.assign(base.v.size(), 0.0f);
    for (int c = 0; c < base.channels; ++c) {
        std::vector<double> x(n);
        for (std::size_t i = 0; i < n; ++i) x[i] = base.at(i, c);
        const std::vector<double> y = convolveFull(x, imp, imp.size() / 2, n);
        for (std::size_t i = 0; i < n; ++i) ref.at(i, c) = static_cast<float>(y[i]);
    }
    const std::vector<double> xs = side(ref), ys = side(mix);
    const long long hop = std::max<long long>(1, static_cast<long long>(std::nearbyint(0.01 * sr)));
    const int width = std::max(3, static_cast<int>(std::nearbyint(0.05 * sr)));
    std::vector<double> xx(n), yy(n), xy(n), fx(n), fy(n), fxy(n);
    for (std::size_t i = 0; i < n; ++i) { xx[i] = xs[i] * xs[i]; yy[i] = ys[i] * ys[i]; xy[i] = xs[i] * ys[i]; }
    uniformFilterNearest(xx.data(), fx.data(), static_cast<long long>(n), width);
    uniformFilterNearest(yy.data(), fy.data(), static_cast<long long>(n), width);
    uniformFilterNearest(xy.data(), fxy.data(), static_cast<long long>(n), width);
    std::vector<double> px, py, pxy;
    for (std::size_t i = 0; i < n; i += static_cast<std::size_t>(hop)) { px.push_back(fx[i]); py.push_back(fy[i]); pxy.push_back(fxy[i]); }
    double mpx = 0;
    for (double v : px) mpx += v;
    mpx /= static_cast<double>(px.size());
    const double floorv = std::max(mpx * 1e-6, 1e-15);
    std::vector<double> obs(px.size()), w(px.size());
    for (std::size_t i = 0; i < px.size(); ++i) {
        const double corr = std::clamp(pxy[i] / std::sqrt(px[i] * py[i] + floorv * floorv), -1.0, 1.0);
        double o = std::log(std::clamp(pxy[i] / (px[i] + floorv), 0.65, 1.45));
        const double c = std::clamp((corr - 0.65) / 0.3, 0.0, 1.0);
        w[i] = c * c * px[i] / (px[i] + 0.05 * mpx + floorv);
        obs[i] = w[i] > 1e-3 ? o : 0.0;
    }
    const std::vector<double> logg = trendFilter(obs, w);
    std::vector<double> gain(logg.size()), gx(logg.size());
    for (std::size_t i = 0; i < logg.size(); ++i) {
        gain[i] = std::exp(std::clamp(logg[i], std::log(0.7), std::log(1.4)));
        gx[i] = static_cast<double>(i) * hop;
    }
    Audio out = ref;
    parallelFor(static_cast<long long>(n), 65536, [&](long long b, long long e) {
        for (long long i = b; i < e; ++i) {
            const double g = interp(static_cast<double>(i), gx, gain);
            for (int c = 0; c < out.channels; ++c) out.at(static_cast<std::size_t>(i), c) = static_cast<float>(ref.at(static_cast<std::size_t>(i), c) * g);
        }
    });
    return out;
}

}
}
