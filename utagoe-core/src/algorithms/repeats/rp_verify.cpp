#include "rp_internal.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace utagoe {
namespace rp {
namespace {

constexpr double kTiny = 1e-20;

double madScale(const std::vector<double>& e, double floor) {
    const double me = median(e);
    std::vector<double> dev(e.size());
    for (std::size_t k = 0; k < e.size(); ++k) dev[k] = std::abs(e[k] - me);
    return std::max(floor, 1.4826 * median(std::move(dev)));
}

bool solveDense(std::vector<double> a, std::vector<double> b, int n, std::vector<double>& x) {
    for (int c = 0; c < n; ++c) {
        int piv = c;
        for (int r = c + 1; r < n; ++r)
            if (std::abs(a[static_cast<std::size_t>(r) * n + c]) > std::abs(a[static_cast<std::size_t>(piv) * n + c])) piv = r;
        if (std::abs(a[static_cast<std::size_t>(piv) * n + c]) < 1e-300) return false;
        if (piv != c) {
            for (int k = 0; k < n; ++k) std::swap(a[static_cast<std::size_t>(c) * n + k], a[static_cast<std::size_t>(piv) * n + k]);
            std::swap(b[static_cast<std::size_t>(c)], b[static_cast<std::size_t>(piv)]);
        }
        for (int r = c + 1; r < n; ++r) {
            const double f = a[static_cast<std::size_t>(r) * n + c] / a[static_cast<std::size_t>(c) * n + c];
            if (f == 0) continue;
            for (int k = c; k < n; ++k) a[static_cast<std::size_t>(r) * n + k] -= f * a[static_cast<std::size_t>(c) * n + k];
            b[static_cast<std::size_t>(r)] -= f * b[static_cast<std::size_t>(c)];
        }
    }
    x.assign(static_cast<std::size_t>(n), 0.0);
    for (int r = n - 1; r >= 0; --r) {
        double s = b[static_cast<std::size_t>(r)];
        for (int k = r + 1; k < n; ++k) s -= a[static_cast<std::size_t>(r) * n + k] * x[static_cast<std::size_t>(k)];
        x[static_cast<std::size_t>(r)] = s / a[static_cast<std::size_t>(r) * n + r];
    }
    return true;
}

Audio scaled(const Audio& a, const std::vector<double>& g) {
    Audio o = a;
    for (std::size_t i = 0; i < o.frames(); ++i)
        for (int c = 0; c < o.channels; ++c) o.at(i, c) *= g[static_cast<std::size_t>(g.size() == 1 ? 0 : c)];
    return o;
}

Audio convolveChannels(const Audio& a, const std::vector<double>& h) {
    Audio o = a;
    for (int c = 0; c < a.channels; ++c) {
        std::vector<double> x(a.frames());
        for (std::size_t i = 0; i < x.size(); ++i) x[i] = a.at(i, c);
        const std::vector<double> y = convolveSame(x, h);
        for (std::size_t i = 0; i < x.size(); ++i) o.at(i, c) = y[i];
    }
    return o;
}

Audio channel(const Audio& a, int c) {
    Audio o;
    o.channels = 1;
    o.v.resize(a.frames());
    for (std::size_t i = 0; i < a.frames(); ++i) o.v[i] = a.at(i, c);
    return o;
}

std::vector<double> firFit(const Audio& r, const Audio& y, const std::vector<char>& train, int taps, double gain) {
    const int h = taps / 2;
    std::vector<double> prior(static_cast<std::size_t>(taps), 0.0);
    prior[static_cast<std::size_t>(h)] = gain;
    std::vector<long long> ids;
    for (std::size_t i = 0; i < train.size(); ++i)
        if (train[i]) ids.push_back(static_cast<long long>(i));
    if (ids.size() > 40000) {
        const std::size_t step = (ids.size() + 39999) / 40000;
        std::vector<long long> s;
        for (std::size_t k = 0; k < ids.size(); k += step) s.push_back(ids[k]);
        ids.swap(s);
    }
    if (static_cast<long long>(ids.size()) < static_cast<long long>(taps) * 5) return prior;
    const long long len = static_cast<long long>(r.frames());
    const std::size_t rows = ids.size() * static_cast<std::size_t>(r.channels);
    std::vector<double> X(rows * static_cast<std::size_t>(taps)), t(rows);
    std::size_t row = 0;
    for (int c = 0; c < r.channels; ++c)
        for (long long i : ids) {
            for (int k = 0; k < taps; ++k) {
                const long long j = i - h + k;
                X[row * static_cast<std::size_t>(taps) + static_cast<std::size_t>(k)] = (j >= 0 && j < len) ? r.at(static_cast<std::size_t>(j), c) : 0.0;
            }
            t[row] = y.at(static_cast<std::size_t>(i), c);
            ++row;
        }
    double trace = 0;
    for (double v : X) trace += v * v;
    const double lam = std::max(trace / taps * .008, 1e-12);
    std::vector<double> coef = prior, w(rows, 1.0), e(rows);
    for (int it = 0; it < 4; ++it) {
        std::vector<double> gram(static_cast<std::size_t>(taps) * taps, 0.0), rhs(static_cast<std::size_t>(taps), 0.0);
        for (std::size_t rr = 0; rr < rows; ++rr) {
            const double* xr = &X[rr * static_cast<std::size_t>(taps)];
            const double wr = w[rr];
            for (int a = 0; a < taps; ++a) {
                const double wa = wr * xr[a];
                rhs[static_cast<std::size_t>(a)] += wa * t[rr];
                for (int b = a; b < taps; ++b) gram[static_cast<std::size_t>(a) * taps + b] += wa * xr[b];
            }
        }
        for (int a = 0; a < taps; ++a) {
            for (int b = 0; b < a; ++b) gram[static_cast<std::size_t>(a) * taps + b] = gram[static_cast<std::size_t>(b) * taps + a];
            gram[static_cast<std::size_t>(a) * taps + a] += lam;
            rhs[static_cast<std::size_t>(a)] += lam * prior[static_cast<std::size_t>(a)];
        }
        if (!solveDense(gram, rhs, taps, coef)) return prior;
        for (std::size_t rr = 0; rr < rows; ++rr) {
            double s = 0;
            for (int k = 0; k < taps; ++k) s += X[rr * static_cast<std::size_t>(taps) + static_cast<std::size_t>(k)] * coef[static_cast<std::size_t>(k)];
            e[rr] = t[rr] - s;
        }
        const double scale = madScale(e, 1e-8);
        for (std::size_t rr = 0; rr < rows; ++rr) w[rr] = std::min(1.0, 1.5 * scale / (std::abs(e[rr]) + 1e-12));
    }
    double nrm = 0;
    for (double c : coef) nrm += c * c;
    if (std::sqrt(nrm) > 4 * std::max(std::abs(gain), .25)) return prior;
    return coef;
}

std::vector<double> gaussianVec(const std::vector<double>& x, double sigma) {
    Mat m(static_cast<int>(x.size()), 1);
    m.v = x;
    gaussian1d(m, sigma, 0);
    return m.v;
}

std::vector<double> smoothTransfer(const Audio& r, const Audio& y, const std::vector<char>& train, int sr, double gain) {
    int nfft = 1 << static_cast<int>(std::ceil(std::log2(std::max(256.0, sr * .032))));
    nfft = std::min(nfft, 2048);
    const int hop = nfft / 4, half = nfft / 2;
    const long long len = static_cast<long long>(r.frames());
    std::vector<long long> cs(static_cast<std::size_t>(len + 1), 0);
    for (long long i = 0; i < len; ++i) cs[static_cast<std::size_t>(i + 1)] = cs[static_cast<std::size_t>(i)] + (train[static_cast<std::size_t>(i)] ? 1 : 0);
    std::vector<long long> centers;
    for (long long c = half; c < len - half; c += hop)
        if (cs[static_cast<std::size_t>(c + half)] - cs[static_cast<std::size_t>(c - half)] == nfft) centers.push_back(c);
    if (centers.size() < 3) return {gain};
    const std::vector<double> win = hannPeriodic(nfft);
    const int F = nfft / 2 + 1, C = r.channels;
    std::vector<double> power(static_cast<std::size_t>(F), 0.0), py(static_cast<std::size_t>(F), 0.0);
    std::vector<cd> cross(static_cast<std::size_t>(F), cd(0, 0));
    std::vector<double> bx(static_cast<std::size_t>(nfft)), by(static_cast<std::size_t>(nfft));
    for (long long c : centers)
        for (int ch = 0; ch < C; ++ch) {
            for (int k = 0; k < nfft; ++k) {
                bx[static_cast<std::size_t>(k)] = r.at(static_cast<std::size_t>(c - half + k), ch) * win[static_cast<std::size_t>(k)];
                by[static_cast<std::size_t>(k)] = y.at(static_cast<std::size_t>(c - half + k), ch) * win[static_cast<std::size_t>(k)];
            }
            const std::vector<cd> X = rfft(bx, static_cast<std::size_t>(nfft)), T = rfft(by, static_cast<std::size_t>(nfft));
            for (int f = 0; f < F; ++f) {
                power[static_cast<std::size_t>(f)] += std::norm(X[static_cast<std::size_t>(f)]);
                py[static_cast<std::size_t>(f)] += std::norm(T[static_cast<std::size_t>(f)]);
                cross[static_cast<std::size_t>(f)] += T[static_cast<std::size_t>(f)] * std::conj(X[static_cast<std::size_t>(f)]);
            }
        }
    const double cnt = static_cast<double>(centers.size()) * C;
    for (int f = 0; f < F; ++f) {
        power[static_cast<std::size_t>(f)] /= cnt;
        py[static_cast<std::size_t>(f)] /= cnt;
        cross[static_cast<std::size_t>(f)] /= cnt;
    }
    const double reg = std::max(median(power) * .03, kTiny);
    std::vector<double> hr(static_cast<std::size_t>(F)), hi(static_cast<std::size_t>(F)), coh(static_cast<std::size_t>(F));
    for (int f = 0; f < F; ++f) {
        const cd h = cross[static_cast<std::size_t>(f)] / (power[static_cast<std::size_t>(f)] + reg);
        hr[static_cast<std::size_t>(f)] = h.real();
        hi[static_cast<std::size_t>(f)] = h.imag();
        coh[static_cast<std::size_t>(f)] = std::norm(cross[static_cast<std::size_t>(f)]) / (power[static_cast<std::size_t>(f)] * py[static_cast<std::size_t>(f)] + kTiny);
    }
    hr = gaussianVec(hr, 2);
    hi = gaussianVec(hi, 2);
    std::vector<cd> H(static_cast<std::size_t>(F));
    for (int f = 0; f < F; ++f) {
        const double trust = std::clamp((coh[static_cast<std::size_t>(f)] - .15) / .65, 0.0, 1.0);
        cd h = cd(gain, 0) + trust * (cd(hr[static_cast<std::size_t>(f)], hi[static_cast<std::size_t>(f)]) - cd(gain, 0));
        h *= std::min(1.0, 2.5 / std::max(std::abs(h), 1e-12));
        H[static_cast<std::size_t>(f)] = h;
    }
    const std::vector<double> imp = irfft(H, static_cast<std::size_t>(nfft));
    std::vector<double> shifted(static_cast<std::size_t>(nfft));
    for (int k = 0; k < nfft; ++k) shifted[static_cast<std::size_t>(k)] = imp[static_cast<std::size_t>((k + half) % nfft)];
    const int hl = std::min(128, nfft / 8), M = 2 * hl + 1;
    const double alpha = .3;
    const int width = static_cast<int>(std::floor(alpha * (M - 1) / 2.0));
    std::vector<double> out(static_cast<std::size_t>(M));
    for (int k = 0; k < M; ++k) {
        double t = 1.0;
        if (k <= width) t = .5 * (1 + std::cos(kPi * (-1 + 2.0 * k / alpha / (M - 1))));
        else if (k >= M - width - 1) t = .5 * (1 + std::cos(kPi * (-2.0 / alpha + 1 + 2.0 * k / alpha / (M - 1))));
        out[static_cast<std::size_t>(k)] = shifted[static_cast<std::size_t>(half - hl + k)] * t;
    }
    return out;
}

void subsample(std::vector<double>& v, std::size_t limit) {
    if (v.size() <= limit) return;
    const std::size_t step = (v.size() + limit - 1) / limit;
    std::vector<double> o;
    for (std::size_t k = 0; k < v.size(); k += step) o.push_back(v[k]);
    v.swap(o);
}

std::vector<double> masked(const Audio& a, const std::vector<char>& mask) {
    std::vector<double> o;
    for (std::size_t i = 0; i < a.frames(); ++i)
        if (mask[i])
            for (int c = 0; c < a.channels; ++c) o.push_back(a.at(i, c));
    return o;
}

std::vector<Span> coherentSubregions(const Audio& y, const Audio& r, int sr, double minimum) {
    const long long block = std::max(32, static_cast<int>(.1 * sr));
    double tot = 0;
    for (double v : r.v) tot += v * v;
    const double total = std::max(tot / std::max<double>(1, static_cast<double>(r.frames())), 1e-20);
    std::vector<char> good;
    std::vector<double> qual;
    for (long long s = 0; s + block <= static_cast<long long>(y.frames()); s += block) {
        double aa = 0, bb = 0, ab = 0;
        for (long long i = s; i < s + block; ++i)
            for (int c = 0; c < y.channels; ++c) {
                const double a = y.at(static_cast<std::size_t>(i), c), b = r.at(static_cast<std::size_t>(i), c);
                aa += a * a;
                bb += b * b;
                ab += a * b;
            }
        const double q = std::abs(ab) / std::sqrt(std::max(1e-20, aa * bb));
        const double g = std::abs(ab) / (bb + 1e-20);
        good.push_back(q > .70 && bb / static_cast<double>(block) > total * 1e-4 && g > .2 && g < 3.);
        qual.push_back(q);
    }
    for (std::size_t k = 1; k + 1 < good.size(); ++k)
        if (!good[k] && good[k - 1] && good[k + 1]) good[k] = 1;
    std::vector<Span> out;
    std::size_t k = 0;
    while (k < good.size()) {
        if (!good[k]) { ++k; continue; }
        std::size_t e = k;
        while (e < good.size() && good[e]) ++e;
        const double st = static_cast<double>(k * block) / sr, en = static_cast<double>(e * block) / sr;
        if (static_cast<double>((e - k) * block) / sr >= minimum)
            out.push_back({st, en, median(std::vector<double>(qual.begin() + static_cast<std::ptrdiff_t>(k), qual.begin() + static_cast<std::ptrdiff_t>(e)))});
        k = e;
    }
    return out;
}

Audio rollFrames(const Audio& a, long long shift) {
    Audio o = a;
    const long long n = static_cast<long long>(a.frames());
    if (n == 0) return o;
    shift %= n;
    for (long long i = 0; i < n; ++i)
        for (int c = 0; c < a.channels; ++c) o.at(static_cast<std::size_t>((i + shift) % n), c) = a.at(static_cast<std::size_t>(i), c);
    return o;
}

double meanTrue(const std::vector<char>& m) {
    if (m.empty()) return 0;
    long long s = 0;
    for (char c : m) s += c ? 1 : 0;
    return static_cast<double>(s) / static_cast<double>(m.size());
}

long long countTrue(const std::vector<char>& m) {
    long long s = 0;
    for (char c : m) s += c ? 1 : 0;
    return s;
}

}

void foldMasks(std::size_t n, int sr, const std::vector<char>& valid, std::vector<char>& a, std::vector<char>& b, std::vector<char>& c) {
    const long long block = std::max<long long>(64, static_cast<long long>(n) / 9);
    long long guard = std::max(8, static_cast<int>(.015 * sr));
    guard = std::min(guard, std::max<long long>(1, block / 5));
    std::vector<char>* outs[3] = {&a, &b, &c};
    for (int role = 0; role < 3; ++role) {
        std::vector<char>& out = *outs[role];
        out.assign(n, 0);
        long long i = 0;
        const long long N = static_cast<long long>(n);
        while (i < N) {
            auto in = [&](long long k) { return (k / block) % 3 == role && valid[static_cast<std::size_t>(k)]; };
            if (!in(i)) { ++i; continue; }
            long long e = i;
            while (e < N && in(e)) ++e;
            if (e - i > 2 * guard)
                for (long long k = i + guard; k < e - guard; ++k) out[static_cast<std::size_t>(k)] = 1;
            i = e;
        }
    }
}

double robustGain(const Audio& x, const Audio& y, const std::vector<char>& mask) {
    std::vector<double> a = masked(x, mask), b = masked(y, mask);
    subsample(a, 100000);
    subsample(b, 100000);
    double ab = 0, aa = 0;
    for (std::size_t k = 0; k < a.size(); ++k) {
        ab += a[k] * b[k];
        aa += a[k] * a[k];
    }
    double g = ab / (aa + kTiny);
    std::vector<double> res(a.size());
    for (int it = 0; it < 6; ++it) {
        for (std::size_t k = 0; k < a.size(); ++k) res[k] = b[k] - g * a[k];
        const double scale = madScale(res, 1e-9);
        double num = 0, den = 0;
        for (std::size_t k = 0; k < a.size(); ++k) {
            const double w = std::min(1.0, 1.5 * scale / (std::abs(res[k]) + 1e-12));
            num += w * a[k] * b[k];
            den += w * a[k] * a[k];
        }
        g = num / (den + kTiny);
    }
    return std::clamp(g, -2.5, 2.5);
}

Metrics metrics(const Audio& y, const Audio& p, const std::vector<char>& mask) {
    double power = 0, res = 0, ab = 0, bb = 0;
    long long n = 0;
    for (std::size_t i = 0; i < y.frames(); ++i)
        if (mask[i])
            for (int c = 0; c < y.channels; ++c) {
                const double a = y.at(i, c), b = p.at(i, c);
                power += a * a;
                res += (a - b) * (a - b);
                ab += a * b;
                bb += b * b;
                ++n;
            }
    Metrics m;
    m.correlation = power > kTiny ? ab / std::sqrt(std::max(kTiny, power * bb)) : 0.0;
    m.reductionDb = 10 * std::log10((power + kTiny) / (res + kTiny));
    m.residualRatio = res / (power + kTiny);
    m.observations = n;
    return m;
}

std::vector<double> convexWeights(const std::vector<double>& y, const std::vector<std::vector<double>>& p, double entropy) {
    const std::size_t K = p.size(), N = y.size();
    std::vector<std::vector<std::size_t>> groups;
    auto norm = [](const std::vector<double>& v) {
        double s = 0;
        for (double x : v) s += x * x;
        return std::sqrt(s);
    };
    for (std::size_t j = 0; j < K; ++j) {
        bool placed = false;
        for (auto& g : groups) {
            double d = 0;
            for (std::size_t i = 0; i < N; ++i) d += (p[j][i] - p[g[0]][i]) * (p[j][i] - p[g[0]][i]);
            if (std::sqrt(d) / (norm(p[j]) + norm(p[g[0]]) + 1e-20) < 1e-6) {
                g.push_back(j);
                placed = true;
                break;
            }
        }
        if (!placed) groups.push_back({j});
    }
    const int k = static_cast<int>(groups.size());
    std::vector<std::vector<double>> e(static_cast<std::size_t>(k), std::vector<double>(N));
    for (int g = 0; g < k; ++g)
        for (std::size_t i = 0; i < N; ++i) {
            double s = 0;
            for (std::size_t j : groups[static_cast<std::size_t>(g)]) s += p[j][i];
            e[static_cast<std::size_t>(g)][i] = s / static_cast<double>(groups[static_cast<std::size_t>(g)].size()) - y[i];
        }
    double yy = 0;
    for (double v : y) yy += v * v;
    const double scale = std::max(yy, 1e-20);
    std::vector<double> G(static_cast<std::size_t>(k) * k);
    for (int a = 0; a < k; ++a)
        for (int b = a; b < k; ++b) {
            double s = 0;
            for (std::size_t i = 0; i < N; ++i) s += e[static_cast<std::size_t>(a)][i] * e[static_cast<std::size_t>(b)][i];
            G[static_cast<std::size_t>(a) * k + b] = G[static_cast<std::size_t>(b) * k + a] = s / scale;
        }
    double dmin = 1e300;
    for (int a = 0; a < k; ++a) dmin = std::min(dmin, G[static_cast<std::size_t>(a) * k + a]);
    const double T = entropy * std::max(dmin, 1e-6);
    std::vector<double> w(static_cast<std::size_t>(k), 1.0 / k);
    if (k > 1) {
        const double prior = 1.0 / k;
        auto objective = [&](const std::vector<double>& x) {
            double s = 0;
            for (int a = 0; a < k; ++a) {
                for (int b = 0; b < k; ++b) s += x[static_cast<std::size_t>(a)] * G[static_cast<std::size_t>(a) * k + b] * x[static_cast<std::size_t>(b)];
                s += T * x[static_cast<std::size_t>(a)] * std::log(std::max(x[static_cast<std::size_t>(a)], 1e-15) / prior);
            }
            return s;
        };
        for (int it = 0; it < 200; ++it) {
            std::vector<double> grad(static_cast<std::size_t>(k)), A(static_cast<std::size_t>(k + 1) * (k + 1), 0.0), rhs(static_cast<std::size_t>(k + 1), 0.0);
            for (int a = 0; a < k; ++a) {
                double s = 0;
                for (int b = 0; b < k; ++b) s += G[static_cast<std::size_t>(a) * k + b] * w[static_cast<std::size_t>(b)];
                grad[static_cast<std::size_t>(a)] = 2 * s + T * (std::log(std::max(w[static_cast<std::size_t>(a)], 1e-15) / prior) + 1);
                for (int b = 0; b < k; ++b) A[static_cast<std::size_t>(a) * (k + 1) + b] = 2 * G[static_cast<std::size_t>(a) * k + b];
                A[static_cast<std::size_t>(a) * (k + 1) + a] += T / std::max(w[static_cast<std::size_t>(a)], 1e-15);
                A[static_cast<std::size_t>(a) * (k + 1) + k] = 1;
                A[static_cast<std::size_t>(k) * (k + 1) + a] = 1;
                rhs[static_cast<std::size_t>(a)] = -grad[static_cast<std::size_t>(a)];
            }
            std::vector<double> sol;
            if (!solveDense(A, rhs, k + 1, sol)) break;
            double step = 1.0;
            for (int a = 0; a < k; ++a)
                if (sol[static_cast<std::size_t>(a)] < 0) step = std::min(step, -.99 * w[static_cast<std::size_t>(a)] / sol[static_cast<std::size_t>(a)]);
            const double f0 = objective(w);
            double dec = 0;
            for (int a = 0; a < k; ++a) dec += grad[static_cast<std::size_t>(a)] * sol[static_cast<std::size_t>(a)];
            std::vector<double> nw(static_cast<std::size_t>(k));
            for (int ls = 0; ls < 60; ++ls) {
                for (int a = 0; a < k; ++a) nw[static_cast<std::size_t>(a)] = std::max(w[static_cast<std::size_t>(a)] + step * sol[static_cast<std::size_t>(a)], 1e-300);
                if (objective(nw) <= f0 + 1e-4 * step * dec) break;
                step *= .5;
            }
            double change = 0;
            for (int a = 0; a < k; ++a) change = std::max(change, std::abs(nw[static_cast<std::size_t>(a)] - w[static_cast<std::size_t>(a)]));
            w = nw;
            if (change < 1e-13) break;
        }
        double s = 0;
        for (double& v : w) {
            v = std::max(v, 0.0);
            s += v;
        }
        if (!(s > 0) || !std::isfinite(s)) w.assign(static_cast<std::size_t>(k), prior);
        else
            for (double& v : w) v /= s;
    }
    std::vector<double> expanded(K, 0.0);
    for (int g = 0; g < k; ++g)
        for (std::size_t j : groups[static_cast<std::size_t>(g)])
            expanded[j] = w[static_cast<std::size_t>(g)] / static_cast<double>(groups[static_cast<std::size_t>(g)].size());
    return expanded;
}

Extraction extractMatch(const Audio& audio, int sr, const Match& m, const Config& cfg) {
    Extraction x;
    Audio r, y;
    std::vector<char> valid;
    alignRegion(audio, sr, m, cfg, r, y, valid, x.guide, x.delaySamples, x.ratePpm);
    std::vector<char> train, blend, test;
    foldMasks(y.frames(), sr, valid, train, blend, test);
    if (std::min({countTrue(train), countTrue(blend), countTrue(test)}) < 32) throw Error("not enough independent support for fit/calibration/test");
    const std::string& mode = x.guide;
    const Audio rg = guideAudio(r, mode), yg = guideAudio(y, mode);
    const double gain = robustGain(rg, yg, train);
    const double polarity = gain >= 0 ? 1.0 : -1.0;
    std::vector<std::string> names = {"direct", "gain", "channel_gain", "short_fir", "smooth_transfer"};
    std::vector<Audio> preds;
    preds.push_back(scaled(r, {polarity}));
    preds.push_back(scaled(r, {gain}));
    if (mode == "side") preds.push_back(scaled(r, {gain}));
    else {
        std::vector<double> gains;
        for (int c = 0; c < r.channels; ++c) gains.push_back(robustGain(channel(r, c), channel(y, c), train));
        preds.push_back(scaled(r, gains));
    }
    std::vector<double> h = firFit(rg, yg, train, cfg.firTaps, gain);
    std::reverse(h.begin(), h.end());
    preds.push_back(convolveChannels(r, h));
    preds.push_back(convolveChannels(r, smoothTransfer(rg, yg, train, sr, gain)));
    std::vector<Audio> gp;
    for (const Audio& p : preds) gp.push_back(guideAudio(p, mode));
    std::vector<double> obs = masked(yg, blend);
    std::vector<std::vector<double>> mat;
    for (const Audio& p : gp) mat.push_back(masked(p, blend));
    if (obs.size() > 150000) {
        subsample(obs, 150000);
        for (auto& col : mat) subsample(col, 150000);
    }
    const std::vector<double> w = convexWeights(obs, mat, cfg.entropy);
    x.shared = r;
    std::fill(x.shared.v.begin(), x.shared.v.end(), 0.0);
    for (std::size_t k = 0; k < preds.size(); ++k) {
        for (std::size_t i = 0; i < x.shared.v.size(); ++i) x.shared.v[i] += w[k] * preds[k].v[i];
        x.audioWeights.push_back({names[k], w[k]});
    }
    x.residual = y;
    for (std::size_t i = 0; i < y.v.size(); ++i) x.residual.v[i] = y.v[i] - x.shared.v[i];
    const Audio gs = guideAudio(x.shared, mode);
    x.heldOut = metrics(yg, gs, test);
    x.full = metrics(y, x.shared, valid);
    double decoy = -1e300;
    for (double shift : {.173, .311, .487})
        decoy = std::max(decoy, metrics(yg, rollFrames(gs, std::max(1, static_cast<int>(shift * sr))), test).correlation);
    x.correlationAdvantage = x.heldOut.correlation - decoy;
    x.status = "musical_similarity_only";
    if (x.heldOut.reductionDb >= cfg.minValidationDb && x.heldOut.correlation >= cfg.minCorrelation && x.correlationAdvantage > .04 && meanTrue(valid) > .80) {
        x.status = "validated_contrast";
        if (x.full.reductionDb > 65) x.status = "near_null_repeat";
    }
    if (x.status == "near_null_repeat") x.quality = "near_null";
    else if (x.status == "validated_contrast")
        x.quality = x.heldOut.reductionDb >= 12 ? "strong_correspondence" : x.heldOut.reductionDb >= 6 ? "moderate_correspondence" : "partial_correspondence";
    else x.quality = "not_verified";
    x.subregions = coherentSubregions(yg, rg, sr, cfg.minRepeat);
    x.target = std::move(y);
    x.aligned = std::move(r);
    return x;
}

namespace {

std::vector<std::vector<double>> jacobiEigen(std::vector<double> a, int n) {
    for (int sweep = 0; sweep < 100; ++sweep) {
        double off = 0;
        for (int p = 0; p < n; ++p)
            for (int q = p + 1; q < n; ++q) off += a[static_cast<std::size_t>(p) * n + q] * a[static_cast<std::size_t>(p) * n + q];
        if (off < 1e-300) break;
        for (int p = 0; p < n; ++p)
            for (int q = p + 1; q < n; ++q) {
                const double apq = a[static_cast<std::size_t>(p) * n + q];
                if (apq == 0) continue;
                const double app = a[static_cast<std::size_t>(p) * n + p], aqq = a[static_cast<std::size_t>(q) * n + q];
                const double theta = (aqq - app) / (2 * apq);
                const double t = (theta >= 0 ? 1.0 : -1.0) / (std::abs(theta) + std::sqrt(theta * theta + 1));
                const double c = 1 / std::sqrt(t * t + 1), s = t * c;
                for (int k = 0; k < n; ++k) {
                    const double akp = a[static_cast<std::size_t>(k) * n + p], akq = a[static_cast<std::size_t>(k) * n + q];
                    a[static_cast<std::size_t>(k) * n + p] = c * akp - s * akq;
                    a[static_cast<std::size_t>(k) * n + q] = s * akp + c * akq;
                }
                for (int k = 0; k < n; ++k) {
                    const double apk = a[static_cast<std::size_t>(p) * n + k], aqk = a[static_cast<std::size_t>(q) * n + k];
                    a[static_cast<std::size_t>(p) * n + k] = c * apk - s * aqk;
                    a[static_cast<std::size_t>(q) * n + k] = s * apk + c * aqk;
                }
            }
    }
    std::vector<std::vector<double>> out(1);
    for (int k = 0; k < n; ++k) out[0].push_back(a[static_cast<std::size_t>(k) * n + k]);
    return out;
}

std::vector<double> boundedLsq(const std::vector<double>& A, const std::vector<double>& b, const std::vector<double>& lo, const std::vector<double>& hi, int n) {
    std::vector<double> best;
    double bestF = 1e300;
    long long combos = 1;
    for (int k = 0; k < n; ++k) combos *= 3;
    for (long long code = 0; code < combos; ++code) {
        std::vector<int> state(static_cast<std::size_t>(n));
        long long c = code;
        for (int k = 0; k < n; ++k) {
            state[static_cast<std::size_t>(k)] = static_cast<int>(c % 3);
            c /= 3;
        }
        std::vector<double> x(static_cast<std::size_t>(n), 0.0);
        std::vector<int> freeIdx;
        for (int k = 0; k < n; ++k) {
            if (state[static_cast<std::size_t>(k)] == 1) x[static_cast<std::size_t>(k)] = lo[static_cast<std::size_t>(k)];
            else if (state[static_cast<std::size_t>(k)] == 2) x[static_cast<std::size_t>(k)] = hi[static_cast<std::size_t>(k)];
            else freeIdx.push_back(k);
        }
        const int f = static_cast<int>(freeIdx.size());
        if (f > 0) {
            std::vector<double> Af(static_cast<std::size_t>(f) * f), bf(static_cast<std::size_t>(f));
            for (int i = 0; i < f; ++i) {
                double s = b[static_cast<std::size_t>(freeIdx[static_cast<std::size_t>(i)])];
                for (int k = 0; k < n; ++k)
                    if (state[static_cast<std::size_t>(k)] != 0) s -= A[static_cast<std::size_t>(freeIdx[static_cast<std::size_t>(i)]) * n + k] * x[static_cast<std::size_t>(k)];
                bf[static_cast<std::size_t>(i)] = s;
                for (int j = 0; j < f; ++j)
                    Af[static_cast<std::size_t>(i) * f + j] = A[static_cast<std::size_t>(freeIdx[static_cast<std::size_t>(i)]) * n + freeIdx[static_cast<std::size_t>(j)]];
            }
            std::vector<double> xf;
            if (!solveDense(Af, bf, f, xf)) continue;
            bool ok = true;
            for (int i = 0; i < f; ++i) {
                const int k = freeIdx[static_cast<std::size_t>(i)];
                if (xf[static_cast<std::size_t>(i)] < lo[static_cast<std::size_t>(k)] - 1e-12 || xf[static_cast<std::size_t>(i)] > hi[static_cast<std::size_t>(k)] + 1e-12) ok = false;
                x[static_cast<std::size_t>(k)] = xf[static_cast<std::size_t>(i)];
            }
            if (!ok) continue;
        }
        double F = 0;
        for (int i = 0; i < n; ++i) {
            double s = 0;
            for (int j = 0; j < n; ++j) s += A[static_cast<std::size_t>(i) * n + j] * x[static_cast<std::size_t>(j)];
            F += .5 * x[static_cast<std::size_t>(i)] * s - b[static_cast<std::size_t>(i)] * x[static_cast<std::size_t>(i)];
        }
        if (F < bestF) {
            bestF = F;
            best = x;
        }
    }
    return best;
}

}

JointResult combineReferences(const Audio& y, const std::vector<Audio>& refs, int sr, const std::string& guide, const Config& cfg) {
    if (guide != "side" && guide != "full") throw Error("joint guide must be side or full");
    if (refs.size() < 2 || refs.size() > 8) throw Error("provide 2 to 8 aligned references");
    for (const Audio& r : refs)
        if (r.v.size() != y.v.size() || r.channels != y.channels) throw Error("reference shape mismatch");
    std::vector<char> train, blend, test;
    foldMasks(y.frames(), sr, std::vector<char>(y.frames(), 1), train, blend, test);
    if (std::min({countTrue(train), countTrue(blend), countTrue(test)}) < 32) throw Error("not enough independent support for joint validation");
    std::vector<Audio> basis = {refs[0]};
    for (std::size_t j = 1; j < refs.size(); ++j) {
        Audio inn = refs[j];
        for (const Audio& b : basis) {
            const double g = robustGain(guideAudio(b, guide), guideAudio(inn, guide), train);
            for (std::size_t i = 0; i < inn.v.size(); ++i) inn.v[i] -= g * b.v[i];
        }
        double ei = 0, er = 0;
        for (double v : guideAudio(inn, guide).v) ei += v * v;
        for (double v : guideAudio(refs[j], guide).v) er += v * v;
        if (ei / (er + 1e-20) > 1e-5) basis.push_back(std::move(inn));
    }
    const Audio gy = guideAudio(y, guide);
    const int k = static_cast<int>(basis.size());
    std::vector<std::vector<double>> cols;
    std::vector<Audio> gb;
    for (const Audio& b : basis) {
        gb.push_back(guideAudio(b, guide));
        cols.push_back(masked(gb.back(), train));
        subsample(cols.back(), 100000);
    }
    std::vector<double> t = masked(gy, train);
    subsample(t, 100000);
    double meanSq = 0;
    for (const auto& c : cols) {
        double s = 0;
        for (double v : c) s += v * v;
        meanSq += s;
    }
    meanSq /= k;
    const double scale = std::max(meanSq * 1e-5, 1e-12);
    std::vector<double> A(static_cast<std::size_t>(k) * k), b(static_cast<std::size_t>(k));
    for (int i = 0; i < k; ++i) {
        for (int j = 0; j < k; ++j) {
            double s = 0;
            for (std::size_t n = 0; n < t.size(); ++n) s += cols[static_cast<std::size_t>(i)][n] * cols[static_cast<std::size_t>(j)][n];
            A[static_cast<std::size_t>(i) * k + j] = s + (i == j ? scale : 0.0);
        }
        double s = 0;
        for (std::size_t n = 0; n < t.size(); ++n) s += cols[static_cast<std::size_t>(i)][n] * t[n];
        b[static_cast<std::size_t>(i)] = s;
    }
    std::vector<double> lo(static_cast<std::size_t>(k), -1.5), hi(static_cast<std::size_t>(k), 1.5);
    lo[0] = 0;
    const std::vector<double> coef = boundedLsq(A, b, lo, hi, k);
    JointResult jr;
    if (coef.empty()) throw Error("joint fit failed");
    jr.shared = y;
    std::fill(jr.shared.v.begin(), jr.shared.v.end(), 0.0);
    for (int i = 0; i < k; ++i)
        for (std::size_t n = 0; n < y.v.size(); ++n) jr.shared.v[n] += coef[static_cast<std::size_t>(i)] * basis[static_cast<std::size_t>(i)].v[n];
    jr.residual = y;
    for (std::size_t n = 0; n < y.v.size(); ++n) jr.residual.v[n] = y.v[n] - jr.shared.v[n];
    double bestBlend = 1e300, bestTest = 0;
    for (const Audio& r : refs) {
        const Audio gr = guideAudio(r, guide);
        const double g = robustGain(gr, gy, train);
        const Audio pred = guideAudio(scaled(r, {g}), guide);
        const Metrics mb = metrics(gy, pred, blend);
        if (mb.residualRatio < bestBlend) {
            bestBlend = mb.residualRatio;
            bestTest = metrics(gy, pred, test).reductionDb;
        }
    }
    jr.heldOut = metrics(gy, guideAudio(jr.shared, guide), test);
    jr.advantageDb = jr.heldOut.reductionDb - bestTest;
    bool stable = false;
    if (k > 1) {
        std::vector<double> M(static_cast<std::size_t>(k) * k);
        for (int i = 0; i < k; ++i)
            for (int j = 0; j < k; ++j) {
                double s = 0;
                for (std::size_t n = 0; n < t.size(); ++n) s += cols[static_cast<std::size_t>(i)][n] * cols[static_cast<std::size_t>(j)][n];
                M[static_cast<std::size_t>(i) * k + j] = s + (i == j ? scale : 0.0);
            }
        const auto ev = jacobiEigen(M, k)[0];
        double emin = 1e300, emax = 0;
        for (double e : ev) {
            emin = std::min(emin, std::abs(e));
            emax = std::max(emax, std::abs(e));
        }
        stable = emin > 0 && emax / emin < 1e8;
    }
    jr.accepted = stable && jr.advantageDb > .15 && jr.heldOut.reductionDb >= cfg.minValidationDb;
    return jr;
}

}
}
