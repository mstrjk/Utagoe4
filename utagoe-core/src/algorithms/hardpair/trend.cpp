#include "hp_grid.h"
#include "parallel.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace utagoe {
namespace hp {

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
