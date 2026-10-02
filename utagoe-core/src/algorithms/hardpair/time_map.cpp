#include "hp.h"
#include "parallel.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace utagoe {
namespace hp {
namespace {

constexpr double kPi = 3.14159265358979323846;

int findSpan(const std::vector<double>& t, int nCoef, double x) {
    if (x >= t[static_cast<std::size_t>(nCoef)]) return nCoef - 1;
    int lo = 3, hi = nCoef - 1;
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        if (t[static_cast<std::size_t>(mid)] <= x) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

void basis(const std::vector<double>& t, int span, double x, std::array<double, 4>& out) {
    std::array<double, 4> left{}, right{};
    out[0] = 1.0;
    for (int j = 1; j <= 3; ++j) {
        left[static_cast<std::size_t>(j)] = x - t[static_cast<std::size_t>(span + 1 - j)];
        right[static_cast<std::size_t>(j)] = t[static_cast<std::size_t>(span + j)] - x;
        double saved = 0.0;
        for (int r = 0; r < j; ++r) {
            const double den = right[static_cast<std::size_t>(r + 1)] + left[static_cast<std::size_t>(j - r)];
            const double temp = den != 0.0 ? out[static_cast<std::size_t>(r)] / den : 0.0;
            out[static_cast<std::size_t>(r)] = saved + right[static_cast<std::size_t>(r + 1)] * temp;
            saved = left[static_cast<std::size_t>(j - r)] * temp;
        }
        out[static_cast<std::size_t>(j)] = saved;
    }
}

std::vector<double> arange(double start, double stop, double step) {
    std::vector<double> v;
    const double count = std::ceil((stop - start) / step);
    for (long long i = 0; i < static_cast<long long>(count); ++i) v.push_back(start + static_cast<double>(i) * step);
    return v;
}

struct Anchor {
    double sec = 0, delay = 0, score = 0;
    bool used = false;
};

std::pair<double, double> anchorDelay(const double* yIn, const double* xIn, int len, int maxLag, int oversample,
                                      const std::vector<double>& win, std::size_t n, const std::vector<cd>& twiddle) {
    std::vector<double> y(yIn, yIn + len), x(xIn, xIn + len);
    double my = 0, mx = 0;
    for (int i = 0; i < len; ++i) { my += y[static_cast<std::size_t>(i)]; mx += x[static_cast<std::size_t>(i)]; }
    my /= len;
    mx /= len;
    double ny = 0, nx = 0;
    for (int i = 0; i < len; ++i) {
        y[static_cast<std::size_t>(i)] = (y[static_cast<std::size_t>(i)] - my) * win[static_cast<std::size_t>(i)];
        x[static_cast<std::size_t>(i)] = (x[static_cast<std::size_t>(i)] - mx) * win[static_cast<std::size_t>(i)];
        ny += y[static_cast<std::size_t>(i)] * y[static_cast<std::size_t>(i)];
        nx += x[static_cast<std::size_t>(i)] * x[static_cast<std::size_t>(i)];
    }
    const double energy = std::sqrt(nx) * std::sqrt(ny);
    if (energy < 1e-15) return {0.0, 0.0};
    const std::vector<cd> Y = rfftAny(y, n), X = rfftAny(x, n);
    const std::size_t half = n / 2 + 1;
    std::vector<cd> c(half);
    for (std::size_t k = 0; k < half; ++k) c[k] = Y[k] * std::conj(X[k]);
    const long long N = static_cast<long long>(n) * oversample;
    const int count = 2 * maxLag * oversample + 1;
    std::vector<double> vals(static_cast<std::size_t>(count));
    for (int li = 0; li < count; ++li) {
        const long long m = li - maxLag * oversample;
        double acc = 0.0;
        for (std::size_t k = 1; k < half; ++k) {
            long long idx = (static_cast<long long>(k) * m) % N;
            if (idx < 0) idx += N;
            const cd e = twiddle[static_cast<std::size_t>(idx)];
            acc += c[k].real() * e.real() - c[k].imag() * e.imag();
        }
        vals[static_cast<std::size_t>(li)] = (c[0].real() + 2.0 * acc) / static_cast<double>(N) * oversample;
    }
    const int k = static_cast<int>(std::max_element(vals.begin(), vals.end()) - vals.begin());
    double frac = 0.0;
    if (k > 0 && k < count - 1) {
        const double a = vals[static_cast<std::size_t>(k - 1)], b = vals[static_cast<std::size_t>(k)], c0 = vals[static_cast<std::size_t>(k + 1)];
        const double den = a - 2 * b + c0;
        if (std::abs(den) > 1e-30) frac = std::clamp(0.5 * (a - c0) / den, -0.5, 0.5);
    }
    const double lag = static_cast<double>(k - maxLag * oversample) + frac;
    return {-lag / oversample, std::clamp(vals[static_cast<std::size_t>(k)] / energy, 0.0, 1.0)};
}

}

double evalSpline(const SplineStage& s, double seconds) {
    const double x = std::clamp(seconds, s.start, s.end);
    const int nCoef = static_cast<int>(s.coef.size());
    const int span = findSpan(s.knots, nCoef, x);
    std::array<double, 4> b{};
    basis(s.knots, span, x, b);
    double v = 0.0;
    for (int j = 0; j < 4; ++j) v += b[static_cast<std::size_t>(j)] * s.coef[static_cast<std::size_t>(span - 3 + j)];
    return v;
}

double TimeMap::positionAt(double sample, double sr) const {
    double p = sample;
    for (const auto& s : stages) p += evalSpline(s, sample / sr);
    return p;
}

SplineStage fitSpline(const std::vector<double>& t, const std::vector<double>& d, const std::vector<double>& quality,
                      double duration, double spacing, double huber, double smooth) {
    if (t.size() < 4) throw Error("not enough reliable time anchors");
    SplineStage s;
    s.knots = {0, 0, 0, 0};
    for (double v : arange(spacing, duration, spacing)) s.knots.push_back(v);
    for (int i = 0; i < 4; ++i) s.knots.push_back(duration);
    const int nb = static_cast<int>(s.knots.size()) - 4;
    const std::size_t m = t.size();
    std::vector<int> spans(m);
    std::vector<std::array<double, 4>> rows(m);
    for (std::size_t i = 0; i < m; ++i) {
        spans[i] = findSpan(s.knots, nb, t[i]);
        basis(s.knots, spans[i], t[i], rows[i]);
    }
    std::vector<double> pen(static_cast<std::size_t>(nb) * nb, 0.0);
    for (int r = 0; r + 2 < nb; ++r) {
        const double dr[3] = {1, -2, 1};
        for (int a = 0; a < 3; ++a)
            for (int b = 0; b < 3; ++b) pen[static_cast<std::size_t>(r + a) * nb + r + b] += dr[a] * dr[b];
    }
    std::vector<double> base(m), w(m), c, residual(m);
    for (std::size_t i = 0; i < m; ++i) {
        base[i] = std::pow(std::clamp(quality[i], 0.0, 1.0), 8);
        w[i] = base[i];
    }
    for (int it = 0; it < 10; ++it) {
        std::vector<double> A(static_cast<std::size_t>(nb) * nb, 0.0), rhs(static_cast<std::size_t>(nb), 0.0);
        for (std::size_t i = 0; i < m; ++i) {
            const int off = spans[i] - 3;
            for (int a = 0; a < 4; ++a) {
                rhs[static_cast<std::size_t>(off + a)] += rows[i][static_cast<std::size_t>(a)] * w[i] * d[i];
                for (int b = 0; b < 4; ++b)
                    A[static_cast<std::size_t>(off + a) * nb + off + b] += rows[i][static_cast<std::size_t>(a)] * w[i] * rows[i][static_cast<std::size_t>(b)];
            }
        }
        for (std::size_t k = 0; k < A.size(); ++k) A[k] += smooth * pen[k];
        for (int k = 0; k < nb; ++k) A[static_cast<std::size_t>(k) * nb + k] += 1e-9;
        c = solveDense(std::move(A), std::move(rhs), nb);
        for (std::size_t i = 0; i < m; ++i) {
            double v = 0.0;
            for (int a = 0; a < 4; ++a) v += rows[i][static_cast<std::size_t>(a)] * c[static_cast<std::size_t>(spans[i] - 3 + a)];
            residual[i] = d[i] - v;
            w[i] = base[i] * std::min(1.0, huber / (std::abs(residual[i]) + 1e-12));
        }
    }
    s.coef = c;
    s.start = t.front();
    s.end = t.back();
    return s;
}

TimeMap initialMap(const Audio& mix, const Audio& ref, double sr) {
    const std::vector<double> ys = side(mix);
    std::vector<double> xs = side(ref);
    auto rmsOf = [](const std::vector<double>& v) {
        double s = 0;
        for (double x : v) s += x * x;
        return std::sqrt(s / std::max<std::size_t>(1, v.size()));
    };
    if (rmsOf(xs) < 1e-7 || rmsOf(ys) < 1e-7)
        throw Error("the stereo side channel is effectively silent, so a vocal-safe time map cannot be estimated. These algorithms need stereo material");
    const int factor = std::max(1, static_cast<int>(sr / 11025));
    const double rate = sr / factor;
    const std::vector<double> yl = resampleDown(ys, factor), xl = resampleDown(xs, factor);
    const double duration = static_cast<double>(std::min(mix.frames(), ref.frames())) / sr;
    if (duration < 3) throw Error("automatic time mapping needs at least 3 seconds of audio");
    const int win = static_cast<int>(0.8 * rate);
    const int search = static_cast<int>(8.0 * rate);
    std::vector<double> coarseTimes = arange(1.0, duration - 1.0, std::max(2.0, duration / 35));
    if (coarseTimes.size() < 4) {
        coarseTimes.clear();
        for (int i = 0; i < 4; ++i) coarseTimes.push_back(0.6 + (duration - 1.2) * i / 3.0);
    }
    const std::size_t nc = coarseTimes.size();
    std::vector<double> cSec(nc), cDelay(nc), cCorr(nc);
    parallelFor(static_cast<long long>(nc), 1, [&](long long b0, long long e0) {
        for (long long i = b0; i < e0; ++i) {
            const double sec = coarseTimes[static_cast<std::size_t>(i)];
            const long long a = static_cast<long long>(sec * rate) - win / 2;
            const long long b = std::max<long long>(0, a - search);
            const long long e = std::min<long long>(static_cast<long long>(xl.size()), a + win + search);
            const long long qa = std::max<long long>(0, a), qb = std::min<long long>(static_cast<long long>(yl.size()), a + win);
            std::vector<double> q(yl.begin() + qa, yl.begin() + qb), r(xl.begin() + b, xl.begin() + e);
            std::vector<double> cr = correlateValid(r, q);
            const std::vector<double> norm = windowEnergyValid(r, static_cast<int>(q.size()));
            double qq = 0;
            for (double v : q) qq += v * v;
            std::size_t best = 0;
            for (std::size_t k = 0; k < cr.size(); ++k) {
                cr[k] /= std::sqrt(qq * std::max(norm[k], 1e-24) + 1e-24);
                if (std::abs(cr[k]) > std::abs(cr[best])) best = k;
            }
            cSec[static_cast<std::size_t>(i)] = sec;
            cDelay[static_cast<std::size_t>(i)] = static_cast<double>((b + static_cast<long long>(best) - a) * factor);
            cCorr[static_cast<std::size_t>(i)] = cr.empty() ? 0.0 : cr[best];
        }
    });
    TimeMap map;
    for (std::size_t i = 0; i < nc; ++i) map.coarse.push_back({cSec[i], cDelay[i], cCorr[i]});
    map.polarity = medianOf(cCorr) >= 0 ? 1 : -1;
    for (double& v : xs) v *= map.polarity;
    std::vector<double> ct, cdl;
    for (std::size_t i = 0; i < nc; ++i) {
        const double dt = gradientAt(cSec, cDelay, i) / sr;
        if (std::abs(cCorr[i]) > 0.25 && std::abs(dt) < 0.01) {
            ct.push_back(cSec[i]);
            cdl.push_back(cDelay[i]);
        }
    }
    if (ct.size() < 4) throw Error("no trustworthy near-identity time path between the two files (are they the same recording?)");
    const Sos sos = butterBandpass(3, 200.0, std::min(9000.0, sr * 0.43), sr);
    const std::vector<double> xf = sosfiltfilt(sos, xs), yf = sosfiltfilt(sos, ys);
    const int dwin = static_cast<int>(0.12 * sr);
    const int pad = std::max(16, static_cast<int>(0.002 * sr));
    const double step = std::min(0.5, (duration - 0.6) / 10);
    const std::vector<double> secs = arange(0.3, duration - 0.3, step);
    std::vector<Anchor> recs(secs.size());
    parallelFor(static_cast<long long>(secs.size()), 4, [&](long long b0, long long e0) {
        for (long long i = b0; i < e0; ++i) {
            const double sec = secs[static_cast<std::size_t>(i)];
            const long long a = static_cast<long long>(sec * sr) - dwin / 2;
            const double guess = interp(sec, ct, cdl);
            const long long b = static_cast<long long>(std::nearbyint(static_cast<double>(a) + guess)) - pad;
            const long long e = b + dwin + 2 * pad;
            if (b < 0 || e > static_cast<long long>(xf.size()) || a < 0 || a + dwin > static_cast<long long>(yf.size())) continue;
            std::vector<double> q(yf.begin() + a, yf.begin() + a + dwin), r(xf.begin() + b, xf.begin() + e);
            std::vector<double> cr = correlateValid(r, q);
            const std::vector<double> nr = windowEnergyValid(r, dwin);
            double qq = 0;
            for (double v : q) qq += v * v;
            std::size_t k = 0;
            for (std::size_t j = 0; j < cr.size(); ++j) {
                cr[j] /= std::sqrt(qq * std::max(nr[j], 1e-24) + 1e-24);
                if (cr[j] > cr[k]) k = j;
            }
            double frac = 0.0;
            if (k > 0 && k + 1 < cr.size()) {
                const double aa = cr[k - 1], bb = cr[k], cc = cr[k + 1];
                const double den = aa - 2 * bb + cc;
                if (std::abs(den) > 1e-20) frac = std::clamp(0.5 * (aa - cc) / den, -0.5, 0.5);
            }
            auto& rec = recs[static_cast<std::size_t>(i)];
            rec.sec = sec;
            rec.delay = static_cast<double>(b + static_cast<long long>(k)) + frac - static_cast<double>(a);
            rec.score = cr[k];
            rec.used = true;
        }
    });
    std::vector<double> t, d, qv, all;
    map.anchors.emplace_back();
    for (const auto& r : recs) {
        if (!r.used) continue;
        map.anchors.back().push_back({r.sec, r.delay, r.score});
        all.push_back(r.score);
        if (r.score > 0.4) { t.push_back(r.sec); d.push_back(r.delay); qv.push_back(r.score); }
    }
    if (t.size() < 8 || medianOf(all) < 0.5) throw Error("the time anchors between the two files are not reliable enough for protected alignment");
    map.stages.push_back(fitSpline(t, d, qv, duration, 8.0, 0.5, 0.02));
    return map;
}

std::vector<double> sincTable(int taps, int phases, double cutoff) {
    std::vector<double> tab(static_cast<std::size_t>(phases + 1) * taps);
    const double i010 = rc::besselI0(10.0);
    for (int p = 0; p <= phases; ++p) {
        double sum = 0.0;
        for (int j = 0; j < taps; ++j) {
            const double u = static_cast<double>(p) / phases - static_cast<double>(j - taps / 2 + 1);
            double h = 0.0;
            if (std::abs(u) < taps / 2.0) {
                const double v = cutoff * u;
                const double sinc = v == 0.0 ? 1.0 : std::sin(kPi * v) / (kPi * v);
                const double r = u / (taps / 2.0);
                h = cutoff * sinc * rc::besselI0(10.0 * std::sqrt(std::max(0.0, 1.0 - r * r))) / i010;
            }
            tab[static_cast<std::size_t>(p) * taps + j] = h;
            sum += h;
        }
        for (int j = 0; j < taps; ++j) tab[static_cast<std::size_t>(p) * taps + j] /= sum;
    }
    return tab;
}

Audio warpReference(const Audio& ref, std::size_t length, const TimeMap& map, double sr, int taps) {
    const std::size_t nProbe = std::max<std::size_t>(100, static_cast<std::size_t>(static_cast<double>(length) / sr * 4));
    const double last = std::max(1.0, static_cast<double>(length) - 1);
    double maxRate = 0.0;
    double prevX = 0.0, prevP = map.positionAt(0.0, sr);
    for (std::size_t i = 1; i < nProbe; ++i) {
        const double x = last * static_cast<double>(i) / static_cast<double>(nProbe - 1);
        const double p = map.positionAt(x, sr);
        const double rate = (p - prevP) / (x - prevX);
        if (rate <= 0 || std::abs(rate - 1) > 0.01)
            throw Error("the time map is non-monotonic or changes speed by more than 1% (an edit between the files?)");
        maxRate = std::max(maxRate, rate);
        prevX = x;
        prevP = p;
    }
    const double cutoff = std::min(1.0, 1.0 / maxRate);
    const int phases = 8192;
    const std::vector<double> tab = sincTable(taps, phases, cutoff);
    Audio out;
    out.channels = ref.channels;
    out.v.assign(length * static_cast<std::size_t>(ref.channels), 0.0f);
    const long long nx = static_cast<long long>(ref.frames());
    const int C = ref.channels;
    parallelFor(static_cast<long long>(length), 4096, [&](long long b0, long long e0) {
        for (long long i = b0; i < e0; ++i) {
            const double p = map.positionAt(static_cast<double>(i), sr);
            if (p < 0 || p > static_cast<double>(nx - 1)) continue;
            const long long b = static_cast<long long>(std::floor(p));
            const double f = (p - static_cast<double>(b)) * phases;
            const int k = std::min(static_cast<int>(f), phases - 1);
            const double alpha = f - k;
            for (int c = 0; c < C; ++c) {
                double total = 0.0;
                for (int j = 0; j < taps; ++j) {
                    const long long idx = b + j - taps / 2 + 1;
                    if (idx >= 0 && idx < nx) {
                        const double w = tab[static_cast<std::size_t>(k) * taps + j] * (1 - alpha) + tab[static_cast<std::size_t>(k + 1) * taps + j] * alpha;
                        total += static_cast<double>(ref.at(static_cast<std::size_t>(idx), c)) * w;
                    }
                }
                out.at(static_cast<std::size_t>(i), c) = static_cast<float>(total);
            }
        }
    });
    if (map.polarity != 1)
        for (float& v : out.v) v = static_cast<float>(v * map.polarity);
    return out;
}

void refineMap(const Audio& mix, const Audio& warped, TimeMap& map, double sr, double spacing) {
    const Sos sos = butterBandpass(3, 1500.0, std::min(14000.0, sr * 0.43), sr);
    const std::vector<double> ys = sosfiltfilt(sos, side(mix)), xs = sosfiltfilt(sos, side(warped));
    const double duration = static_cast<double>(mix.frames()) / sr;
    const int win = static_cast<int>(0.20 * sr);
    const std::vector<double> secs = arange(0.4, duration - 0.4, 0.25);
    const std::vector<double> w = hann(win);
    const std::size_t n = nextFastLen(static_cast<std::size_t>(2 * win - 1));
    const int oversample = 16;
    const std::size_t N = n * oversample;
    std::vector<cd> twiddle(N);
    for (std::size_t i = 0; i < N; ++i) twiddle[i] = std::polar(1.0, 2.0 * kPi * static_cast<double>(i) / static_cast<double>(N));
    std::vector<Anchor> recs(secs.size());
    parallelFor(static_cast<long long>(secs.size()), 2, [&](long long b0, long long e0) {
        for (long long i = b0; i < e0; ++i) {
            const double sec = secs[static_cast<std::size_t>(i)];
            const long long a = static_cast<long long>(sec * sr) - win / 2;
            auto& r = recs[static_cast<std::size_t>(i)];
            r.sec = sec;
            if (a < 0 || a + win > static_cast<long long>(ys.size())) continue;
            const auto [delay, score] = anchorDelay(ys.data() + a, xs.data() + a, win, 12, oversample, w, n, twiddle);
            r.delay = delay;
            r.score = score;
            r.used = true;
        }
    });
    std::vector<double> t, d, q;
    map.anchors.emplace_back();
    for (const auto& r : recs)
        if (r.used) map.anchors.back().push_back({r.sec, r.delay, r.score});
    for (const auto& r : recs)
        if (r.used && r.score > 0.65 && std::abs(r.delay) < 11) { t.push_back(r.sec); d.push_back(r.delay); q.push_back(r.score); }
    if (t.size() < 8) throw Error("the fine time refinement found too few reliable anchors");
    map.stages.push_back(fitSpline(t, d, q, duration, spacing, 0.10, 0.1));
}

}
}
