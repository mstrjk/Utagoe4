#include "hp_grid.h"
#include "parallel.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace utagoe {
namespace hp {

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
}
}
