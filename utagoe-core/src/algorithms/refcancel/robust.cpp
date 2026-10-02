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
}
}
