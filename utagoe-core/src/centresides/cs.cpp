#include "cs.h"
#include "../../include/fft.h"
#include "../parallel.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <mutex>

namespace utagoe {
namespace cs {

namespace {

using cd = std::complex<double>;
constexpr double kEps = 1e-12;
constexpr double kPi = 3.14159265358979323846;

struct Grid {
    int F = 0, T = 0;
    std::vector<double> v;
    Grid() = default;
    Grid(int f, int t) : F(f), T(t), v(static_cast<std::size_t>(f) * t, 0.0) {}
    double& at(int f, int t) { return v[static_cast<std::size_t>(f) * T + t]; }
    double at(int f, int t) const { return v[static_cast<std::size_t>(f) * T + t]; }
};

struct CGrid {
    int F = 0, T = 0;
    std::vector<cd> v;
    CGrid() = default;
    CGrid(int f, int t) : F(f), T(t), v(static_cast<std::size_t>(f) * t) {}
    cd& at(int f, int t) { return v[static_cast<std::size_t>(f) * T + t]; }
    const cd& at(int f, int t) const { return v[static_cast<std::size_t>(f) * T + t]; }
};

std::vector<double> kernel(double sigma) {
    const int r = static_cast<int>(4.0 * sigma + 0.5);
    std::vector<double> k(static_cast<std::size_t>(2 * r + 1));
    double s = 0.0;
    for (int i = -r; i <= r; ++i) {
        const double v = std::exp(-0.5 * (i * i) / (sigma * sigma));
        k[static_cast<std::size_t>(i + r)] = v;
        s += v;
    }
    for (double& v : k) v /= s;
    return k;
}

void smooth(Grid& g, double sf, double st) {
    const int F = g.F, T = g.T;
    std::vector<double> tmp(g.v.size());
    {
        const std::vector<double> k = kernel(sf);
        const int r = static_cast<int>(k.size() / 2);
        for (int f = 0; f < F; ++f)
            for (int t = 0; t < T; ++t) {
                double s = 0.0;
                for (int i = -r; i <= r; ++i) s += k[static_cast<std::size_t>(i + r)] * g.at(std::clamp(f + i, 0, F - 1), t);
                tmp[static_cast<std::size_t>(f) * T + t] = s;
            }
    }
    {
        const std::vector<double> k = kernel(st);
        const int r = static_cast<int>(k.size() / 2);
        for (int f = 0; f < F; ++f) {
            const double* row = tmp.data() + static_cast<std::size_t>(f) * T;
            for (int t = 0; t < T; ++t) {
                double s = 0.0;
                for (int i = -r; i <= r; ++i) s += k[static_cast<std::size_t>(i + r)] * row[std::clamp(t + i, 0, T - 1)];
                g.at(f, t) = s;
            }
        }
    }
}

Grid smoothed(Grid g, double sf, double st) {
    smooth(g, sf, st);
    return g;
}

double phaseAgreement(double ipd, double power) {
    return std::pow(std::clamp(std::cos(0.5 * ipd), 0.0, 1.0), power);
}

Grid softCenterMask(const CGrid& L, const CGrid& R, double ildSigma, double phasePower, double cohPower, double sf, double st) {
    const int F = L.F, T = L.T;
    Grid pll(F, T), prr(F, T), re(F, T), im(F, T);
    for (std::size_t i = 0; i < L.v.size(); ++i) {
        pll.v[i] = std::norm(L.v[i]);
        prr.v[i] = std::norm(R.v[i]);
        const cd x = L.v[i] * std::conj(R.v[i]);
        re.v[i] = x.real();
        im.v[i] = x.imag();
    }
    smooth(pll, sf, st);
    smooth(prr, sf, st);
    smooth(re, sf, st);
    smooth(im, sf, st);
    Grid mask(F, T);
    for (std::size_t i = 0; i < mask.v.size(); ++i) {
        const cd gamma = cd(re.v[i], im.v[i]) / std::sqrt(pll.v[i] * prr.v[i] + kEps);
        const double coh = std::clamp(std::abs(gamma), 0.0, 1.0);
        const double ild = 10.0 * std::log10((pll.v[i] + kEps) / (prr.v[i] + kEps));
        const double balance = std::exp(-0.5 * std::pow(ild / std::max(ildSigma, 1e-3), 2));
        mask.v[i] = std::clamp(balance * phaseAgreement(std::arg(gamma), phasePower) * std::pow(coh, cohPower), 0.0, 1.0);
    }
    return mask;
}

CGrid maskedMid(const CGrid& L, const CGrid& R, const Grid& mask) {
    CGrid c(L.F, L.T);
    for (std::size_t i = 0; i < c.v.size(); ++i) c.v[i] = mask.v[i] * 0.5 * (L.v[i] + R.v[i]);
    return c;
}

CGrid phantom(const CGrid& L, const CGrid& R, Grid& maskOut) {
    Grid agree(L.F, L.T);
    for (std::size_t i = 0; i < agree.v.size(); ++i) agree.v[i] = phaseAgreement(std::arg(L.v[i] * std::conj(R.v[i])), 4.0);
    smooth(agree, 0.7, 1.5);
    CGrid c(L.F, L.T);
    for (std::size_t i = 0; i < c.v.size(); ++i) {
        const double mag = std::min(std::abs(L.v[i]), std::abs(R.v[i])) * agree.v[i];
        c.v[i] = std::polar(mag, std::arg(L.v[i] + R.v[i] + kEps));
    }
    maskOut = std::move(agree);
    return c;
}

CGrid coherence(const CGrid& L, const CGrid& R, Grid& maskOut) {
    maskOut = softCenterMask(L, R, 4.5, 2.0, 1.5, 1.0, 3.0);
    return maskedMid(L, R, maskOut);
}

CGrid adress(const CGrid& L, const CGrid& R, Grid& maskOut) {
    Grid mask(L.F, L.T);
    for (std::size_t i = 0; i < mask.v.size(); ++i) {
        const cd l = L.v[i], r = R.v[i];
        double k = (l * std::conj(r)).real() / (std::norm(r) + kEps);
        k = std::clamp(k, 1.0 / 64.0, 64.0);
        const double residual = std::abs(l - k * r) / (std::abs(l) + k * std::abs(r) + kEps);
        const double pan = std::exp(-0.5 * std::pow(std::abs(std::log2(k)) / 0.42, 2));
        const double null = std::exp(-0.5 * std::pow(residual / 0.24, 2));
        mask.v[i] = std::clamp(pan * null, 0.0, 1.0);
    }
    smooth(mask, 0.8, 1.2);
    maskOut = mask;
    return maskedMid(L, R, mask);
}

CGrid duet(const CGrid& L, const CGrid& R, double sr, int n, Grid& maskOut) {
    Grid mask(L.F, L.T);
    const double delaySigma = 0.11 * 1e-3;
    for (int f = 0; f < L.F; ++f) {
        const double freq = static_cast<double>(f) * sr / n;
        const double omega = 2.0 * kPi * freq;
        const double blend = std::clamp((freq - 60.0) / 120.0, 0.0, 1.0);
        for (int t = 0; t < L.T; ++t) {
            const cd ratio = R.at(f, t) / (L.at(f, t) + kEps);
            const double logAtt = std::log(std::abs(ratio) + kEps);
            const double ipd = std::arg(ratio);
            const double delay = freq > 80.0 ? ipd / (omega + kEps) : 0.0;
            const double att = std::exp(-0.5 * std::pow(logAtt / 0.28, 2));
            const double ds = std::exp(-0.5 * std::pow(delay / delaySigma, 2));
            const double low = std::exp(-0.5 * std::pow(ipd / 0.42, 2));
            mask.at(f, t) = std::clamp(att * (blend * ds + (1.0 - blend) * low), 0.0, 1.0);
        }
    }
    smooth(mask, 0.7, 1.1);
    maskOut = mask;
    return maskedMid(L, R, mask);
}

CGrid pca(const CGrid& L, const CGrid& R, Grid& maskOut) {
    const int F = L.F, T = L.T;
    Grid a(F, T), d(F, T), re(F, T), im(F, T);
    for (std::size_t i = 0; i < a.v.size(); ++i) {
        a.v[i] = std::norm(L.v[i]);
        d.v[i] = std::norm(R.v[i]);
        const cd x = L.v[i] * std::conj(R.v[i]);
        re.v[i] = x.real();
        im.v[i] = x.imag();
    }
    smooth(a, 1.2, 3.0);
    smooth(d, 1.2, 3.0);
    smooth(re, 1.2, 3.0);
    smooth(im, 1.2, 3.0);
    Grid mask(F, T);
    for (std::size_t i = 0; i < mask.v.size(); ++i) {
        const cd b(re.v[i], im.v[i]);
        const double tr = a.v[i] + d.v[i];
        const double disc = std::sqrt(std::max((a.v[i] - d.v[i]) * (a.v[i] - d.v[i]) + 4.0 * std::norm(b), 0.0));
        const double lam1 = 0.5 * (tr + disc), lam2 = 0.5 * (tr - disc);
        cd v0 = b, v1 = cd(lam1 - a.v[i], 0.0);
        const double nrm = std::sqrt(std::norm(v0) + std::norm(v1) + kEps);
        v0 /= nrm;
        v1 /= nrm;
        if (nrm < 1e-8) { v0 = cd(1.0, 0.0); v1 = cd(0.0, 0.0); }
        const double magBalance = std::clamp(2.0 * std::abs(v0) * std::abs(v1), 0.0, 1.0);
        const double orientation = std::clamp(magBalance * phaseAgreement(std::arg(v0 * std::conj(v1)), 2.0), 0.0, 1.0);
        const double dominance = std::clamp((lam1 - lam2) / (lam1 + lam2 + kEps), 0.0, 1.0);
        mask.v[i] = std::clamp(std::pow(orientation, 2.2) * std::pow(dominance, 0.65), 0.0, 1.0);
    }
    smooth(mask, 0.6, 1.0);
    maskOut = mask;
    return maskedMid(L, R, mask);
}

CGrid prettyOne(const CGrid& L, const CGrid& R, Grid& maskOut) {
    Grid instant(L.F, L.T);
    for (std::size_t i = 0; i < instant.v.size(); ++i) {
        const double al = std::abs(L.v[i]), ar = std::abs(R.v[i]);
        const double balance = 1.0 - std::abs(al - ar) / (al + ar + kEps);
        instant.v[i] = std::pow(std::clamp(balance, 0.0, 1.0), 1.5) * phaseAgreement(std::arg(L.v[i] * std::conj(R.v[i])), 3.0);
    }
    const Grid coh = softCenterMask(L, R, 5.5, 1.5, 1.0, 1.1, 3.5);
    Grid mask(L.F, L.T);
    for (std::size_t i = 0; i < mask.v.size(); ++i)
        mask.v[i] = std::sqrt(std::clamp(instant.v[i], 0.0, 1.0) * std::clamp(coh.v[i], 0.0, 1.0));
    smooth(mask, 1.4, 3.2);
    for (double& v : mask.v) v = std::pow(std::clamp(v, 0.0, 1.0), 0.82);
    maskOut = mask;
    return maskedMid(L, R, mask);
}

std::vector<double> hannPeriodic(int n) {
    std::vector<double> w(static_cast<std::size_t>(n));
    for (int j = 0; j < n; ++j) w[static_cast<std::size_t>(j)] = 0.5 - 0.5 * std::cos(2.0 * kPi * j / n);
    return w;
}

bool stftCenter(const std::vector<float>& x, double sr, int n, Method m, std::vector<double>& out, double& maskSum, double& maskCount,
                const std::function<bool(double)>& tick) {
    const long long len = static_cast<long long>(x.size() / 2);
    const int hop = n / 4, half = n / 2;
    const long long xp = std::max<long long>(len, n) + 2LL * half;
    const long long extra = ((-(xp - n)) % hop + hop) % hop;
    const int T = static_cast<int>((xp + extra - n) / hop + 1);
    const int F = n / 2 + 1;
    const std::vector<double> w = hannPeriodic(n);
    double wsum = 0.0;
    for (double v : w) wsum += v;
    const double scale = 1.0 / wsum;
    std::vector<double> acc(static_cast<std::size_t>(len), 0.0), norm(static_cast<std::size_t>(len), 0.0);
    for (int t = 0; t < T; ++t)
        for (int j = 0; j < n; ++j) {
            const long long i = static_cast<long long>(t) * hop - half + j;
            if (i >= 0 && i < len) norm[static_cast<std::size_t>(i)] += w[static_cast<std::size_t>(j)] * w[static_cast<std::size_t>(j)];
        }
    constexpr int kCore = 192, kMargin = 32;
    const int blocks = (T + kCore - 1) / kCore;
    std::mutex lock;
    std::atomic<int> done{0};
    std::atomic<bool> stop{false};
    FftTables tables(static_cast<std::size_t>(n));
    parallelFor(blocks, 1, [&](long long b0, long long e0) {
        std::vector<float> buf(static_cast<std::size_t>(2 * n));
        for (long long bi = b0; bi < e0 && !stop; ++bi) {
            const int c0 = static_cast<int>(bi) * kCore, c1 = std::min(T, c0 + kCore);
            const int g0 = std::max(0, c0 - kMargin), g1 = std::min(T, c1 + kMargin);
            const int G = g1 - g0;
            CGrid L(F, G), R(F, G);
            for (int t = 0; t < G; ++t)
                for (int ch = 0; ch < 2; ++ch) {
                    for (int j = 0; j < n; ++j) {
                        const long long i = static_cast<long long>(g0 + t) * hop - half + j;
                        const float v = (i >= 0 && i < len) ? x[static_cast<std::size_t>(i) * 2 + ch] : 0.0f;
                        buf[static_cast<std::size_t>(2 * j)] = static_cast<float>(v * w[static_cast<std::size_t>(j)]);
                        buf[static_cast<std::size_t>(2 * j + 1)] = 0.0f;
                    }
                    fft_forward(buf.data(), static_cast<std::size_t>(n), tables);
                    CGrid& z = ch == 0 ? L : R;
                    for (int f = 0; f < F; ++f) z.at(f, t) = cd(buf[static_cast<std::size_t>(2 * f)], buf[static_cast<std::size_t>(2 * f + 1)]) * scale;
                }
            Grid mask;
            CGrid c;
            switch (m) {
            case Method::Phantom: c = phantom(L, R, mask); break;
            case Method::Coherence: c = coherence(L, R, mask); break;
            case Method::Adress: c = adress(L, R, mask); break;
            case Method::Duet: c = duet(L, R, sr, n, mask); break;
            case Method::Pca: c = pca(L, R, mask); break;
            default: c = prettyOne(L, R, mask); break;
            }
            double ms = 0.0, mc = 0.0;
            std::vector<double> local(static_cast<std::size_t>(c1 - c0) * hop + n, 0.0);
            for (int t = c0; t < c1; ++t) {
                const int tt = t - g0;
                for (int f = 0; f < F; ++f) {
                    cd v = c.at(f, tt) * wsum;
                    if (f == 0 || f == n / 2) v = cd(v.real(), 0.0);
                    buf[static_cast<std::size_t>(2 * f)] = static_cast<float>(v.real());
                    buf[static_cast<std::size_t>(2 * f + 1)] = static_cast<float>(v.imag());
                    if (f > 0 && f < n / 2) {
                        buf[static_cast<std::size_t>(2 * (n - f))] = static_cast<float>(v.real());
                        buf[static_cast<std::size_t>(2 * (n - f) + 1)] = static_cast<float>(-v.imag());
                    }
                    ms += mask.at(f, tt);
                    mc += 1.0;
                }
                fft_inverse(buf.data(), static_cast<std::size_t>(n), tables);
                const std::size_t o = static_cast<std::size_t>(t - c0) * hop;
                for (int j = 0; j < n; ++j) local[o + static_cast<std::size_t>(j)] += buf[static_cast<std::size_t>(2 * j)] * w[static_cast<std::size_t>(j)];
            }
            {
                std::lock_guard<std::mutex> g(lock);
                const long long base = static_cast<long long>(c0) * hop - half;
                for (std::size_t k = 0; k < local.size(); ++k) {
                    const long long i = base + static_cast<long long>(k);
                    if (i >= 0 && i < len) acc[static_cast<std::size_t>(i)] += local[k];
                }
                maskSum += ms;
                maskCount += mc;
                if (tick && !tick(static_cast<double>(++done) / blocks)) stop = true;
            }
        }
    });
    if (stop) return false;
    out.assign(static_cast<std::size_t>(len), 0.0);
    for (long long i = 0; i < len; ++i)
        out[static_cast<std::size_t>(i)] = norm[static_cast<std::size_t>(i)] > 1e-10 ? acc[static_cast<std::size_t>(i)] / norm[static_cast<std::size_t>(i)] : acc[static_cast<std::size_t>(i)];
    return true;
}

}

const char* methodName(Method m) {
    switch (m) {
    case Method::TrueMs: return "True M/S";
    case Method::Phantom: return "Phantom";
    case Method::Coherence: return "Coherence";
    case Method::Adress: return "ADRess";
    case Method::Duet: return "DUET";
    case Method::Pca: return "PCA";
    case Method::Pretty: return "Pretty";
    }
    return "?";
}

Split centerOf(const std::vector<float>& x, double sr, Method m, const Progress& progress) {
    Split s;
    const std::size_t len = x.size() / 2;
    if (m == Method::TrueMs) {
        s.center.resize(len);
        for (std::size_t i = 0; i < len; ++i) s.center[i] = 0.5 * (static_cast<double>(x[2 * i]) + x[2 * i + 1]);
        s.maskMean = 1.0;
        if (progress) progress(1.0);
        return s;
    }
    double maskSum = 0.0, maskCount = 0.0;
    if (m != Method::Pretty) {
        if (!stftCenter(x, sr, 4096, m, s.center, maskSum, maskCount, progress)) s.cancelled = true;
        s.maskMean = maskCount > 0 ? maskSum / maskCount : 0.0;
        return s;
    }
    const int sizes[3] = {1024, 4096, 8192};
    const double weights[3] = {0.24, 0.52, 0.24};
    s.center.assign(len, 0.0);
    for (int k = 0; k < 3; ++k) {
        std::vector<double> mono;
        double ms = 0.0, mc = 0.0;
        auto tick = [&](double f) { return !progress || progress((k + f) / 3.0); };
        if (!stftCenter(x, sr, sizes[k], m, mono, ms, mc, tick)) {
            s.cancelled = true;
            return s;
        }
        for (std::size_t i = 0; i < len; ++i) s.center[i] += weights[k] * mono[i];
        s.maskMean += weights[k] * (mc > 0 ? ms / mc : 0.0);
    }
    return s;
}

}
}
