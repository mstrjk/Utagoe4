#include "hp.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace utagoe {
namespace hp {

namespace {

constexpr double kEps = 1e-12;

std::vector<double> bandpass(const std::vector<double>& x, double sr, double lo, double hi, int order = 3) {
    hi = std::min(hi, 0.46 * sr);
    lo = std::max(lo, 10.0);
    if (hi <= lo * 1.05) return std::vector<double>(x.size(), 0.0);
    return sosfiltfilt(butterBandpass(order, lo, hi, sr), x);
}

std::vector<double> uniform(const std::vector<double>& x, int size) {
    std::vector<double> out(x.size());
    uniformFilterNearest(x.data(), out.data(), static_cast<long long>(x.size()), size);
    return out;
}

std::vector<double> smoothPower(const std::vector<double>& x, double sr, double ms) {
    const int n = std::max(3, static_cast<int>(std::nearbyint(ms * 1e-3 * sr)));
    std::vector<double> sq(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) sq[i] = x[i] * x[i];
    return uniform(sq, n);
}

std::vector<long long> findPeaks(const std::vector<double>& x, long long distance, double minProminence) {
    std::vector<long long> peaks;
    const long long n = static_cast<long long>(x.size());
    const long long iMax = n - 1;
    long long i = 1;
    while (i < iMax) {
        if (x[static_cast<std::size_t>(i - 1)] < x[static_cast<std::size_t>(i)]) {
            long long ahead = i + 1;
            while (ahead < iMax && x[static_cast<std::size_t>(ahead)] == x[static_cast<std::size_t>(i)]) ++ahead;
            if (x[static_cast<std::size_t>(ahead)] < x[static_cast<std::size_t>(i)]) {
                peaks.push_back((i + ahead - 1) / 2);
                i = ahead;
            }
        }
        ++i;
    }
    const std::size_t np = peaks.size();
    std::vector<char> keep(np, 1);
    std::vector<std::size_t> order(np);
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return x[static_cast<std::size_t>(peaks[a])] < x[static_cast<std::size_t>(peaks[b])];
    });
    for (std::size_t r = np; r-- > 0;) {
        const std::size_t j = order[r];
        if (!keep[j]) continue;
        for (long long k = static_cast<long long>(j) - 1; k >= 0 && peaks[j] - peaks[static_cast<std::size_t>(k)] < distance; --k)
            keep[static_cast<std::size_t>(k)] = 0;
        for (std::size_t k = j + 1; k < np && peaks[k] - peaks[j] < distance; ++k) keep[k] = 0;
    }
    std::vector<long long> out;
    for (std::size_t j = 0; j < np; ++j) {
        if (!keep[j]) continue;
        const long long p = peaks[j];
        const double top = x[static_cast<std::size_t>(p)];
        double leftMin = top, rightMin = top;
        for (long long k = p; k >= 0 && x[static_cast<std::size_t>(k)] <= top; --k) leftMin = std::min(leftMin, x[static_cast<std::size_t>(k)]);
        for (long long k = p; k <= iMax && x[static_cast<std::size_t>(k)] <= top; ++k) rightMin = std::min(rightMin, x[static_cast<std::size_t>(k)]);
        if (top - std::max(leftMin, rightMin) >= minProminence) out.push_back(p);
    }
    return out;
}

void detectKicks(const std::vector<double>& mid, double sr, std::vector<long long>& peaks, std::vector<double>& strengths) {
    peaks.clear();
    strengths.clear();
    const std::vector<double> k = bandpass(mid, sr, 35.0, 180.0, 4);
    const std::vector<double> power = smoothPower(k, sr, 10.0);
    const std::vector<double> f = uniform(power, std::max(3, static_cast<int>(0.004 * sr)));
    std::vector<double> novelty(power.size());
    bool any = false;
    for (std::size_t i = 0; i < power.size(); ++i) {
        novelty[i] = std::max(0.0, f[i] - (i == 0 ? power[0] : f[i - 1]));
        any = any || novelty[i] > 0;
    }
    if (!any) return;
    const double med = medianOf(novelty);
    std::vector<double> dev(novelty.size());
    for (std::size_t i = 0; i < novelty.size(); ++i) dev[i] = std::abs(novelty[i] - med);
    const double mad = medianOf(dev) + kEps;
    const double prom = std::max(quantileOf(novelty, 0.80), med + 3.5 * 1.4826 * mad);
    peaks = findPeaks(novelty, std::max<long long>(1, static_cast<long long>(180.0 * 1e-3 * sr)), prom);
    if (peaks.empty()) return;
    const long long radius = std::max<long long>(1, static_cast<long long>(0.040 * sr));
    for (long long p : peaks) {
        double m = -std::numeric_limits<double>::infinity();
        for (long long i = p; i < std::min<long long>(static_cast<long long>(power.size()), p + radius); ++i) m = std::max(m, power[static_cast<std::size_t>(i)]);
        strengths.push_back(m);
    }
    if (peaks.size() >= 12) {
        const double gate = quantileOf(strengths, 0.35);
        std::vector<long long> kp;
        std::vector<double> ks;
        for (std::size_t i = 0; i < peaks.size(); ++i)
            if (strengths[i] >= gate) { kp.push_back(peaks[i]); ks.push_back(strengths[i]); }
        peaks = std::move(kp);
        strengths = std::move(ks);
    }
    for (double& s : strengths) s = std::sqrt(std::max(s, kEps));
    const double sm = medianOf(strengths) + kEps;
    for (double& s : strengths) s = std::clamp(s / sm, 0.55, 1.8);
}

std::vector<double> duckShape(const std::vector<double>& t, const double* v) {
    const double onset = v[1] * 1e-3, attack = std::max(v[2], 0.25) * 1e-3, release = std::max(v[3], 1.0) * 1e-3;
    std::vector<double> z(t.size());
    double peak = 0.0;
    for (std::size_t i = 0; i < t.size(); ++i) {
        const double u = std::max(t[i] - onset, 0.0);
        z[i] = t[i] < onset ? 0.0 : (1.0 - std::exp(-u / attack)) * std::exp(-u / release);
        peak = std::max(peak, z[i]);
    }
    for (double& x : z) x = -std::abs(v[0]) * (peak > 0 ? x / peak : x);
    return z;
}

std::vector<double> savgol(const std::vector<double>& x, int win) {
    const std::size_t n = x.size();
    if (static_cast<std::size_t>(win) > n) return x;
    const int h = win / 2;
    auto fitAt = [&](std::size_t from, const std::vector<double>& at) {
        std::vector<double> a(9, 0.0), b(3, 0.0);
        for (int j = 0; j < win; ++j) {
            const double u = j - h;
            const double p[3] = {1.0, u, u * u};
            for (int r = 0; r < 3; ++r) {
                b[static_cast<std::size_t>(r)] += p[r] * x[from + static_cast<std::size_t>(j)];
                for (int c = 0; c < 3; ++c) a[static_cast<std::size_t>(r * 3 + c)] += p[r] * p[c];
            }
        }
        const std::vector<double> c = solveDense(a, b, 3);
        std::vector<double> out;
        for (double pos : at) {
            const double u = pos - h;
            out.push_back(c[0] + c[1] * u + c[2] * u * u);
        }
        return out;
    };
    std::vector<double> coef(static_cast<std::size_t>(win));
    {
        std::vector<double> a(9, 0.0);
        for (int j = 0; j < win; ++j) {
            const double u = j - h;
            const double p[3] = {1.0, u, u * u};
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) a[static_cast<std::size_t>(r * 3 + c)] += p[r] * p[c];
        }
        const std::vector<double> e0 = solveDense(a, {1.0, 0.0, 0.0}, 3);
        for (int j = 0; j < win; ++j) {
            const double u = j - h;
            coef[static_cast<std::size_t>(j)] = e0[0] + e0[1] * u + e0[2] * u * u;
        }
    }
    std::vector<double> y(n, 0.0);
    for (std::size_t i = static_cast<std::size_t>(h); i + static_cast<std::size_t>(h) < n; ++i) {
        double s = 0.0;
        for (int j = 0; j < win; ++j) s += coef[static_cast<std::size_t>(j)] * x[i - static_cast<std::size_t>(h) + static_cast<std::size_t>(j)];
        y[i] = s;
    }
    std::vector<double> head, tail;
    for (int j = 0; j < h; ++j) { head.push_back(j); tail.push_back(win - h + j); }
    const std::vector<double> hv = fitAt(0, head), tv = fitAt(n - static_cast<std::size_t>(win), tail);
    for (int j = 0; j < h; ++j) {
        y[static_cast<std::size_t>(j)] = hv[static_cast<std::size_t>(j)];
        y[n - static_cast<std::size_t>(h) + static_cast<std::size_t>(j)] = tv[static_cast<std::size_t>(j)];
    }
    return y;
}

struct KernelFit {
    double v[4] = {0.0, 0.0, 5.0, 80.0};
};

KernelFit fitKernel(const std::vector<double>& rel, const std::vector<double>& observed, const std::vector<double>& weights, double maxDepth) {
    std::vector<double> tt, yy, ww;
    for (std::size_t i = 0; i < rel.size(); ++i)
        if (rel[i] >= -0.015 && rel[i] <= 0.220 && std::isfinite(observed[i]) && weights[i] > 0) {
            tt.push_back(rel[i]);
            yy.push_back(observed[i]);
            ww.push_back(std::sqrt(weights[i]));
        }
    KernelFit fit;
    if (tt.size() < 20) return fit;
    const double lo[4] = {0.0, -12.0, 0.25, 12.0}, hi[4] = {maxDepth, 20.0, 40.0, 350.0};
    const double fs = 0.12;
    const std::size_t m = tt.size();
    auto residual = [&](const double* v) {
        std::vector<double> r = duckShape(tt, v);
        for (std::size_t i = 0; i < m; ++i) r[i] = ww[i] * (r[i] - yy[i]);
        return r;
    };
    auto robust = [&](const std::vector<double>& r) {
        double c = 0.0;
        for (double x : r) { const double z = (x / fs) * (x / fs); c += 2.0 * (std::sqrt(1.0 + z) - 1.0); }
        return 0.5 * fs * fs * c;
    };
    double bestCost = std::numeric_limits<double>::infinity();
    const double starts[3][4] = {{std::min(0.5, maxDepth), -3.0, 3.0, 60.0},
                                 {std::min(1.0, maxDepth), 0.0, 8.0, 100.0},
                                 {std::min(0.3, maxDepth), -8.0, 1.0, 35.0}};
    for (const auto& s0 : starts) {
        double x[4];
        for (int k = 0; k < 4; ++k) x[k] = std::clamp(s0[k], lo[k], hi[k]);
        std::vector<double> r = residual(x);
        double cost = robust(r);
        double lambda = 1e-3;
        int evals = 1;
        while (evals < 300) {
            std::vector<double> w(m);
            for (std::size_t i = 0; i < m; ++i) w[i] = 1.0 / std::sqrt(1.0 + (r[i] / fs) * (r[i] / fs));
            std::vector<std::vector<double>> J(4, std::vector<double>(m));
            for (int k = 0; k < 4; ++k) {
                double xp[4] = {x[0], x[1], x[2], x[3]};
                double step = 1e-6 * std::max(1.0, std::abs(x[k]));
                if (xp[k] + step > hi[k]) step = -step;
                xp[k] += step;
                const std::vector<double> rp = residual(xp);
                ++evals;
                for (std::size_t i = 0; i < m; ++i) J[static_cast<std::size_t>(k)][i] = (rp[i] - r[i]) / step;
            }
            std::vector<double> a(16, 0.0), g(4, 0.0);
            for (int p = 0; p < 4; ++p) {
                for (std::size_t i = 0; i < m; ++i) g[static_cast<std::size_t>(p)] += w[i] * J[static_cast<std::size_t>(p)][i] * r[i];
                for (int q = 0; q < 4; ++q) {
                    double s = 0.0;
                    for (std::size_t i = 0; i < m; ++i) s += w[i] * J[static_cast<std::size_t>(p)][i] * J[static_cast<std::size_t>(q)][i];
                    a[static_cast<std::size_t>(p * 4 + q)] = s;
                }
            }
            bool improved = false;
            while (evals < 300) {
                std::vector<double> aa = a, rhs(4);
                for (int p = 0; p < 4; ++p) {
                    aa[static_cast<std::size_t>(p * 4 + p)] += lambda * std::max(a[static_cast<std::size_t>(p * 4 + p)], 1e-12);
                    rhs[static_cast<std::size_t>(p)] = -g[static_cast<std::size_t>(p)];
                }
                const std::vector<double> d = solveDense(aa, rhs, 4);
                double xn[4];
                double moved = 0.0;
                for (int k = 0; k < 4; ++k) {
                    xn[k] = std::clamp(x[k] + d[static_cast<std::size_t>(k)], lo[k], hi[k]);
                    moved = std::max(moved, std::abs(xn[k] - x[k]) / std::max(1.0, std::abs(x[k])));
                }
                if (moved < 1e-10) break;
                std::vector<double> rn = residual(xn);
                ++evals;
                const double cn = robust(rn);
                if (cn < cost) {
                    const double gain = cost - cn;
                    for (int k = 0; k < 4; ++k) x[k] = xn[k];
                    r = std::move(rn);
                    cost = cn;
                    lambda = std::max(lambda / 3.0, 1e-12);
                    improved = gain > 1e-12 * std::max(cost, 1e-30) && moved > 1e-9;
                    break;
                }
                lambda *= 4.0;
                if (lambda > 1e12) break;
            }
            if (!improved) break;
        }
        double mse = 0.0;
        for (double v : r) mse += v * v;
        mse /= static_cast<double>(m);
        if (mse < bestCost) {
            bestCost = mse;
            for (int k = 0; k < 4; ++k) fit.v[k] = x[k];
        }
    }
    return fit;
}

}

KickDuck kickDuck(const Audio& mix, const Audio& reference, double sr, double maxDepthDb) {
    if (mix.channels != 2 || reference.channels != 2) throw Error("kick duck matching needs stereo input");
    const std::size_t n = std::min(mix.frames(), reference.frames());
    std::vector<double> ys(n), xs(n), xm(n);
    for (std::size_t i = 0; i < n; ++i) {
        ys[i] = 0.5 * (static_cast<double>(mix.at(i, 0)) - mix.at(i, 1));
        xs[i] = 0.5 * (static_cast<double>(reference.at(i, 0)) - reference.at(i, 1));
        xm[i] = 0.5 * (static_cast<double>(reference.at(i, 0)) + reference.at(i, 1));
    }
    const int q = std::max(1, static_cast<int>(std::ceil(sr / 22050.0)));
    const double asr = q == 1 ? sr : std::nearbyint(sr / q);
    const std::vector<double> ms = resampleDown(ys, q), rs = resampleDown(xs, q), rm = resampleDown(xm, q);

    std::vector<long long> peaks;
    std::vector<double> strengths;
    detectKicks(rm, asr, peaks, strengths);
    if (peaks.size() < 8) throw Error("too few trustworthy kick events");

    const double nyq = asr * 0.5;
    std::vector<std::pair<double, double>> bands;
    for (auto [a, b] : std::vector<std::pair<double, double>>{{120, 300}, {300, 700}, {700, 1500}, {1500, 3000}, {3000, std::min(8000.0, nyq * 0.90)}})
        if (b > a * 1.10) bands.emplace_back(a, b);
    const long long pre = static_cast<long long>(std::nearbyint(0.060 * asr)), post = static_cast<long long>(std::nearbyint(0.240 * asr));
    const std::size_t len = static_cast<std::size_t>(pre + post);
    std::vector<double> rel(len);
    std::vector<char> base(len);
    for (std::size_t i = 0; i < len; ++i) {
        rel[i] = (static_cast<double>(i) - static_cast<double>(pre)) / asr;
        base[i] = rel[i] >= -0.055 && rel[i] <= -0.018;
    }
    auto baseMedian = [&](const double* seg) {
        std::vector<double> v;
        for (std::size_t i = 0; i < len; ++i)
            if (base[i]) v.push_back(seg[i]);
        return medianOf(std::move(v));
    };

    const std::size_t ne = peaks.size(), nb = bands.size();
    std::vector<std::vector<std::vector<double>>> train(nb);
    std::vector<std::vector<std::vector<double>>> held(ne, std::vector<std::vector<double>>(nb));
    for (std::size_t bi = 0; bi < nb; ++bi) {
        const std::vector<double> ym = bandpass(ms, asr, bands[bi].first, bands[bi].second);
        const std::vector<double> xr = bandpass(rs, asr, bands[bi].first, bands[bi].second);
        std::vector<double> em = smoothPower(ym, asr, 8.0), er = smoothPower(xr, asr, 8.0);
        std::vector<double> ratio(em.size());
        for (std::size_t i = 0; i < em.size(); ++i) {
            em[i] = std::sqrt(std::max(em[i], kEps));
            er[i] = std::sqrt(std::max(er[i], kEps));
            ratio[i] = 20.0 * std::log10((em[i] + 1e-8) / (er[i] + 1e-8));
        }
        const double floor = quantileOf(er, 0.45);
        for (std::size_t ei = 0; ei < ne; ++ei) {
            const long long p = peaks[ei];
            if (p - pre < 0 || p + post > static_cast<long long>(ratio.size())) continue;
            const std::size_t at = static_cast<std::size_t>(p - pre);
            if (baseMedian(er.data() + at) < std::max(floor * 0.35, 1e-7)) continue;
            std::vector<double> seg(ratio.begin() + static_cast<std::ptrdiff_t>(at), ratio.begin() + static_cast<std::ptrdiff_t>(at + len));
            const double b0 = baseMedian(seg.data());
            for (double& v : seg) v -= b0;
            if (ei % 2 == 0) {
                for (double& v : seg) v = std::clamp(v, -maxDepthDb * 2.0, maxDepthDb * 2.0);
                train[bi].push_back(std::move(seg));
            } else {
                held[ei][bi] = std::move(seg);
            }
        }
    }

    std::vector<std::vector<double>> profiles, weights;
    for (const auto& ev : train) {
        if (ev.size() < 5) continue;
        std::vector<double> med(len), w(len), col(ev.size());
        const double rel0 = std::clamp(static_cast<double>(ev.size()) / 20.0, 0.2, 1.0);
        for (std::size_t s = 0; s < len; ++s) {
            for (std::size_t e = 0; e < ev.size(); ++e) col[e] = ev[e][s];
            med[s] = medianOf(col);
            for (std::size_t e = 0; e < ev.size(); ++e) col[e] = std::abs(ev[e][s] - med[s]);
            w[s] = rel0 / (medianOf(col) + 0.05);
        }
        profiles.push_back(std::move(med));
        weights.push_back(std::move(w));
    }
    if (profiles.size() < 2) throw Error("not enough reliable side-channel bands");

    std::vector<double> common(len), wcommon(len), col(profiles.size());
    for (std::size_t s = 0; s < len; ++s) {
        for (std::size_t b = 0; b < profiles.size(); ++b) col[b] = profiles[b][s];
        common[s] = quantileOf(col, 0.40);
        for (std::size_t b = 0; b < profiles.size(); ++b) col[b] = weights[b][s];
        wcommon[s] = medianOf(col);
    }
    {
        const double b0 = baseMedian(common.data());
        for (double& v : common) v -= b0;
    }
    int win = std::max(5, static_cast<int>(std::nearbyint(0.007 * asr)) | 1);
    if (static_cast<std::size_t>(win) >= len) win = std::max(5, static_cast<int>((len / 2) * 2) - 1);
    common = savgol(common, win);

    KickDuck out;
    out.kicks = static_cast<int>(ne);
    KernelFit fit = fitKernel(rel, common, wcommon, maxDepthDb);
    std::vector<double> kernel = duckShape(rel, fit.v);
    std::vector<double> act;
    for (std::size_t i = 0; i < len; ++i)
        if (rel[i] >= -0.010 && rel[i] <= 0.180) act.push_back(common[i]);
    if (fit.v[0] < 0.04 || quantileOf(act, 0.20) > -0.03) {
        fit.v[0] = 0.0;
        std::fill(kernel.begin(), kernel.end(), 0.0);
    }
    out.depthDb = fit.v[0];
    out.onsetMs = fit.v[1];
    out.attackMs = fit.v[2];
    out.releaseMs = fit.v[3];

    std::vector<double> scales(ne);
    for (std::size_t e = 0; e < ne; ++e) scales[e] = std::clamp(std::sqrt(strengths[e]), 0.65, 1.45);

    std::vector<double> gainA(ms.size(), 0.0);
    for (std::size_t e = 0; e < ne; ++e)
        for (std::size_t i = 0; i < len; ++i) {
            const long long idx = peaks[e] - pre + static_cast<long long>(i);
            if (idx >= 0 && idx < static_cast<long long>(gainA.size())) gainA[static_cast<std::size_t>(idx)] += scales[e] * kernel[i];
        }
    for (double& g : gainA) g = std::clamp(g, -maxDepthDb, 0.0);

    auto heldout = [&](double strength) {
        std::vector<std::vector<double>> eventCommon;
        std::vector<double> bandCol;
        for (std::size_t e = 1; e < ne; e += 2) {
            std::vector<const std::vector<double>*> segs;
            for (const auto& s : held[e])
                if (!s.empty()) segs.push_back(&s);
            if (segs.size() < 2) continue;
            std::vector<double> prof(len);
            bandCol.resize(segs.size());
            for (std::size_t s = 0; s < len; ++s) {
                for (std::size_t b = 0; b < segs.size(); ++b) bandCol[b] = (*segs[b])[s] - strength * scales[e] * kernel[s];
                prof[s] = quantileOf(bandCol, 0.40);
            }
            eventCommon.push_back(std::move(prof));
        }
        if (eventCommon.size() < 3) return std::numeric_limits<double>::infinity();
        std::vector<double> prof(len), c(eventCommon.size());
        for (std::size_t s = 0; s < len; ++s) {
            for (std::size_t e = 0; e < eventCommon.size(); ++e) c[e] = eventCommon[e][s];
            prof[s] = medianOf(c);
        }
        const double b0 = baseMedian(prof.data());
        double neg = 0.0, pos = 0.0;
        std::size_t count = 0;
        for (std::size_t s = 0; s < len; ++s) {
            if (rel[s] < -0.005 || rel[s] > 0.180) continue;
            const double v = prof[s] - b0;
            neg += std::min(v, 0.0) * std::min(v, 0.0);
            pos += std::max(v, 0.0) * std::max(v, 0.0);
            ++count;
        }
        return count == 0 ? std::numeric_limits<double>::infinity() : (neg + 0.20 * pos) / static_cast<double>(count);
    };

    const double baseScore = heldout(0.0);
    double bestStrength = 0.0, bestScore = baseScore;
    for (int k = 0; k < 13; ++k) {
        const double s = 1.5 * k / 12.0;
        const double sc = heldout(s);
        if (sc < bestScore) { bestStrength = s; bestScore = sc; }
    }
    if (!std::isfinite(bestScore) || bestScore >= baseScore * 0.995) bestStrength = 0.0;
    out.strength = bestStrength;
    out.changeDb = std::isfinite(bestScore) ? 10.0 * std::log10((bestScore + kEps) / (baseScore + kEps)) : 0.0;
    for (double& g : gainA) g *= bestStrength;

    out.gainDb.assign(n, 0.0f);
    const double last = static_cast<double>(gainA.size() - 1) / asr;
    for (std::size_t j = 0; j < n; ++j) {
        const double t = static_cast<double>(j) / sr;
        if (t > last) continue;
        const double pos = t * asr;
        const std::size_t i0 = static_cast<std::size_t>(pos);
        const std::size_t i1 = std::min(i0 + 1, gainA.size() - 1);
        const double f = pos - static_cast<double>(i0);
        out.gainDb[j] = static_cast<float>(gainA[i0] + (gainA[i1] - gainA[i0]) * f);
    }
    return out;
}

void applyGainDb(Audio& a, const std::vector<float>& gainDb) {
    const std::size_t n = std::min(a.frames(), gainDb.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (gainDb[i] == 0.0f) continue;
        const float g = static_cast<float>(std::pow(10.0, gainDb[i] / 20.0));
        for (int c = 0; c < a.channels; ++c) a.at(i, c) *= g;
    }
}

}
}
