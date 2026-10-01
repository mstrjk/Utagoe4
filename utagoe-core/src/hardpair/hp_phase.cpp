#include "hp.h"
#include "../parallel.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace utagoe {
namespace hp {
namespace {

constexpr double kPi = 3.14159265358979323846;

cd response(double f, double sr, double fc, int order, double delay) {
    const cd z = std::polar(1.0, -2.0 * kPi * f / sr);
    const double t = std::tan(kPi * fc / sr);
    const double a = (1 - t) / (1 + t);
    cd r = (a - z) / (1.0 - a * z);
    cd out(1, 0);
    for (int i = 0; i < order; ++i) out *= r;
    return out * std::polar(1.0, -2.0 * kPi * f * delay / sr);
}

}

PhaseSpectra phaseSpectra(const Audio& mix, const Audio& ref, double sr, int nFft) {
    const std::vector<double> ys = side(mix), xs = side(ref);
    if (static_cast<long long>(ys.size()) < nFft) nFft = 1 << static_cast<int>(std::floor(std::log2(static_cast<double>(ys.size()))));
    const std::vector<double> win = hann(nFft);
    const int half = nFft / 2 + 1;
    PhaseSpectra s;
    s.h.assign(static_cast<std::size_t>(half), cd(0, 0));
    s.xx.assign(static_cast<std::size_t>(half), 0.0);
    std::vector<double> yy(static_cast<std::size_t>(half), 0.0);
    std::vector<long long> starts;
    for (long long a = 0; a + nFft <= static_cast<long long>(ys.size()); a += nFft / 2) starts.push_back(a);
    std::vector<std::vector<cd>> xyPart(starts.size());
    std::vector<std::vector<double>> xxPart(starts.size()), yyPart(starts.size());
    parallelFor(static_cast<long long>(starts.size()), 1, [&](long long b, long long e) {
        for (long long i = b; i < e; ++i) {
            const long long a = starts[static_cast<std::size_t>(i)];
            std::vector<cd> Y(static_cast<std::size_t>(nFft)), X(static_cast<std::size_t>(nFft));
            for (int j = 0; j < nFft; ++j) {
                Y[static_cast<std::size_t>(j)] = cd(ys[static_cast<std::size_t>(a + j)] * win[static_cast<std::size_t>(j)], 0);
                X[static_cast<std::size_t>(j)] = cd(xs[static_cast<std::size_t>(a + j)] * win[static_cast<std::size_t>(j)], 0);
            }
            rc::fftDouble(Y, false);
            rc::fftDouble(X, false);
            auto& xy = xyPart[static_cast<std::size_t>(i)];
            auto& xx = xxPart[static_cast<std::size_t>(i)];
            auto& yv = yyPart[static_cast<std::size_t>(i)];
            xy.resize(static_cast<std::size_t>(half));
            xx.resize(static_cast<std::size_t>(half));
            yv.resize(static_cast<std::size_t>(half));
            for (int k = 0; k < half; ++k) {
                xy[static_cast<std::size_t>(k)] = Y[static_cast<std::size_t>(k)] * std::conj(X[static_cast<std::size_t>(k)]);
                xx[static_cast<std::size_t>(k)] = std::norm(X[static_cast<std::size_t>(k)]);
                yv[static_cast<std::size_t>(k)] = std::norm(Y[static_cast<std::size_t>(k)]);
            }
        }
    });
    for (std::size_t i = 0; i < starts.size(); ++i)
        for (int k = 0; k < half; ++k) {
            s.h[static_cast<std::size_t>(k)] += xyPart[i][static_cast<std::size_t>(k)];
            s.xx[static_cast<std::size_t>(k)] += xxPart[i][static_cast<std::size_t>(k)];
            yy[static_cast<std::size_t>(k)] += yyPart[i][static_cast<std::size_t>(k)];
        }
    const double maxXX = *std::max_element(s.xx.begin(), s.xx.end());
    const double reg = std::max(maxXX * 1e-10, 1e-20);
    s.f.resize(static_cast<std::size_t>(half));
    s.coh.resize(static_cast<std::size_t>(half));
    for (int k = 0; k < half; ++k) {
        const cd xy = s.h[static_cast<std::size_t>(k)];
        s.coh[static_cast<std::size_t>(k)] = std::norm(xy) / (s.xx[static_cast<std::size_t>(k)] * yy[static_cast<std::size_t>(k)] + 1e-25);
        s.h[static_cast<std::size_t>(k)] = xy / (s.xx[static_cast<std::size_t>(k)] + reg);
        s.f[static_cast<std::size_t>(k)] = static_cast<double>(k) * sr / nFft;
    }
    return s;
}

PhaseModel fitPhase(const Audio& mix, const Audio& ref, double sr) {
    const PhaseSpectra s = phaseSpectra(mix, ref, sr, 32768);
    const double pmax = *std::max_element(s.xx.begin(), s.xx.end());
    std::vector<double> fs, wt;
    std::vector<cd> target;
    for (std::size_t k = 0; k < s.f.size(); ++k) {
        const double f = s.f[k];
        if (f >= 100 && f <= std::min(6000.0, sr * 0.35) && s.coh[k] > 0.70 && s.xx[k] > pmax * 1e-6) {
            fs.push_back(f);
            target.push_back(s.h[k] / std::max(std::abs(s.h[k]), 1e-12));
            wt.push_back(std::sqrt(std::clamp(s.coh[k], 0.0, 1.0)) * std::sqrt(300.0 / std::max(f, 150.0)));
        }
    }
    if (fs.size() < 12) throw Error("no sufficiently coherent stereo side bands for the rational phase fit");
    const int order = 2;
    const double lo[2] = {0.1, -4.0}, hi[2] = {std::min(250.0, sr * 0.03), 4.0};
    const double C = 0.04;
    auto residuals = [&](const std::array<double, 2>& v, std::vector<double>& r) {
        r.resize(fs.size() * 2);
        for (std::size_t i = 0; i < fs.size(); ++i) {
            const cd e = wt[i] * (response(fs[i], sr, v[0], order, v[1]) - target[i]);
            r[i] = e.real();
            r[i + fs.size()] = e.imag();
        }
    };
    auto cost = [&](const std::vector<double>& r) {
        double c = 0;
        for (double x : r) c += 2.0 * (std::sqrt(1.0 + (x / C) * (x / C)) - 1.0);
        return 0.5 * C * C * c;
    };
    std::array<double, 2> x{25.0, 0.0};
    std::vector<double> r, rTry;
    residuals(x, r);
    double fx = cost(r);
    double lambda = 1e-3;
    for (int it = 0; it < 200; ++it) {
        std::array<std::vector<double>, 2> J;
        for (int p = 0; p < 2; ++p) {
            const double h = 1e-7 * std::max(1.0, std::abs(x[static_cast<std::size_t>(p)]));
            std::array<double, 2> xp = x, xm = x;
            xp[static_cast<std::size_t>(p)] = std::min(hi[p], x[static_cast<std::size_t>(p)] + h);
            xm[static_cast<std::size_t>(p)] = std::max(lo[p], x[static_cast<std::size_t>(p)] - h);
            std::vector<double> rp, rm;
            residuals(xp, rp);
            residuals(xm, rm);
            const double dx = xp[static_cast<std::size_t>(p)] - xm[static_cast<std::size_t>(p)];
            J[static_cast<std::size_t>(p)].resize(r.size());
            for (std::size_t i = 0; i < r.size(); ++i) J[static_cast<std::size_t>(p)][i] = (rp[i] - rm[i]) / dx;
        }
        double A[2][2] = {{0, 0}, {0, 0}}, g[2] = {0, 0};
        for (std::size_t i = 0; i < r.size(); ++i) {
            const double w = 1.0 / std::sqrt(1.0 + (r[i] / C) * (r[i] / C));
            for (int a = 0; a < 2; ++a) {
                g[a] += w * J[static_cast<std::size_t>(a)][i] * r[i];
                for (int b = 0; b < 2; ++b) A[a][b] += w * J[static_cast<std::size_t>(a)][i] * J[static_cast<std::size_t>(b)][i];
            }
        }
        bool improved = false;
        for (int tries = 0; tries < 30; ++tries) {
            const double a00 = A[0][0] * (1 + lambda), a11 = A[1][1] * (1 + lambda), a01 = A[0][1];
            const double det = a00 * a11 - a01 * a01;
            if (det == 0) { lambda *= 10; continue; }
            const double d0 = -(a11 * g[0] - a01 * g[1]) / det, d1 = -(a00 * g[1] - a01 * g[0]) / det;
            std::array<double, 2> xn{std::clamp(x[0] + d0, lo[0], hi[0]), std::clamp(x[1] + d1, lo[1], hi[1])};
            residuals(xn, rTry);
            const double fn = cost(rTry);
            if (fn < fx) {
                const double change = (fx - fn) / std::max(fx, 1e-300);
                const double step = std::abs(xn[0] - x[0]) + std::abs(xn[1] - x[1]);
                x = xn;
                r.swap(rTry);
                fx = fn;
                lambda = std::max(lambda / 10, 1e-12);
                improved = true;
                if (change < 1e-15 || step < 1e-12) it = 200;
                break;
            }
            lambda *= 10;
        }
        if (!improved) break;
    }
    PhaseModel m;
    m.order = order;
    m.breakHz = x[0];
    m.delay = x[1];
    double rmsAcc = 0;
    for (std::size_t i = 0; i < fs.size(); ++i) rmsAcc += std::norm(response(fs[i], sr, x[0], order, x[1]) - target[i]);
    m.fitRms = std::sqrt(rmsAcc / fs.size());
    std::vector<double> ratios;
    for (std::size_t k = 0; k < s.f.size(); ++k) {
        const double f = s.f[k];
        if (f > 150 && f < std::min(8000.0, sr * 0.4) && s.coh[k] > 0.85 && s.xx[k] > pmax * 1e-7)
            ratios.push_back((s.h[k] / response(f, sr, x[0], order, x[1])).real());
    }
    m.gain = ratios.empty() ? 1.0 : std::clamp(medianOf(ratios), 0.25, 4.0);
    return m;
}

Audio applyPhase(const Audio& ref, double sr, const PhaseModel& model) {
    const double t = std::tan(kPi * model.breakHz / sr);
    const double a = (1 - t) / (1 + t);
    const std::size_t n = ref.frames();
    const int C = ref.channels;
    std::vector<double> h(129);
    double hs = 0;
    const double i0 = rc::besselI0(10.0);
    for (int i = 0; i < 129; ++i) {
        const double u = (i - 64) - model.delay;
        const double sinc = u == 0 ? 1.0 : std::sin(kPi * u) / (kPi * u);
        const double r = (i - 64) / 64.0;
        h[static_cast<std::size_t>(i)] = sinc * rc::besselI0(10.0 * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0;
        hs += h[static_cast<std::size_t>(i)];
    }
    for (double& v : h) v /= hs;
    Audio out;
    out.channels = C;
    out.v.assign(ref.v.size(), 0.0f);
    std::vector<std::vector<double>> chans(static_cast<std::size_t>(C));
    parallelFor(C, 1, [&](long long b, long long e) {
        for (long long c = b; c < e; ++c) {
            std::vector<double> z(n);
            for (std::size_t i = 0; i < n; ++i) z[i] = ref.at(i, static_cast<int>(c));
            for (int o = 0; o < model.order; ++o) {
                double xPrev = 0, yPrev = 0;
                for (std::size_t i = 0; i < n; ++i) {
                    const double x = z[i];
                    const double y = a * x - xPrev + a * yPrev;
                    xPrev = x;
                    yPrev = y;
                    z[i] = y;
                }
            }
            chans[static_cast<std::size_t>(c)] = std::move(z);
        }
    });
    for (int c = 0; c < C; ++c) {
        const std::vector<double> y = convolveFull(chans[static_cast<std::size_t>(c)], h, 64, n);
        for (std::size_t i = 0; i < n; ++i) out.at(i, c) = static_cast<float>(y[i] * model.gain);
    }
    return out;
}

}
}
