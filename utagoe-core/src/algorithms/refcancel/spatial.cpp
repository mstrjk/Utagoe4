#include "rc_internal.h"
#include "parallel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>

namespace utagoe {
namespace rc {

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
}
}
