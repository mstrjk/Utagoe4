#include "upmix_internal.h"
#include "../hardpair/hp.h"
#include "parallel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace utagoe {
namespace upmix {
namespace {

using Out5 = std::array<CField, 5>;

constexpr double kAngles5[5] = {-30.0, 30.0, 0.0, -110.0, 110.0};
constexpr double kSortAngles[6] = {0.0, 30.0, 110.0, 250.0, 330.0, 360.0};
constexpr int kSortChannels[6] = {2, 1, 4, 3, 0, 2};

Out5 makeOut(int F, int T) {
    Out5 y;
    for (auto& c : y) c = CField(F, T);
    return y;
}

double position(const cd& l, const cd& r) {
    const double theta = std::atan2(std::abs(r) + kEps, std::abs(l) + kEps);
    return std::clamp(4.0 * theta / kPi - 1.0, -1.0, 1.0);
}

double wrapDeg(double a) {
    double v = std::fmod(a + 180.0, 360.0);
    if (v < 0) v += 360.0;
    return v - 180.0;
}

double angularDistance(double a, double b) { return std::abs(wrapDeg(a - b)); }

void ringPan(double az, double g[5]) {
    for (int i = 0; i < 5; ++i) g[i] = 0;
    double a = std::fmod(az, 360.0);
    if (a < 0) a += 360.0;
    int seg = 0;
    for (int i = 1; i < 6; ++i)
        if (kSortAngles[i] <= a) seg = i;
    seg = std::clamp(seg, 0, 4);
    const double t = (a - kSortAngles[seg]) / std::max(kSortAngles[seg + 1] - kSortAngles[seg], 1e-12);
    g[kSortChannels[seg]] += std::cos(0.5 * kPi * t);
    g[kSortChannels[seg + 1]] += std::sin(0.5 * kPi * t);
}

void foldCorrectionFronts(Out5& y, const StereoSpec& x) {
    const std::size_t n = y[0].v.size();
    for (std::size_t i = 0; i < n; ++i) {
        const cd fl = y[0].v[i] + kDown * y[2].v[i] + kDown * y[3].v[i];
        const cd fr = y[1].v[i] + kDown * y[2].v[i] + kDown * y[4].v[i];
        y[0].v[i] += x.l.v[i] - fl;
        y[1].v[i] += x.r.v[i] - fr;
    }
}

void adjacentFill(Out5& y, double amount) {
    if (amount <= 0) return;
    amount = std::clamp(amount, 0.0, 0.45);
    const int ring[5] = {2, 1, 4, 3, 0};
    const std::size_t n = y[0].v.size();
    parallelFor(static_cast<long long>(n), 65536, [&](long long b, long long e) {
        for (long long i = b; i < e; ++i) {
            cd src[5];
            for (int c = 0; c < 5; ++c) src[c] = y[static_cast<std::size_t>(c)].v[static_cast<std::size_t>(i)];
            for (int k = 0; k < 5; ++k) {
                const int ch = ring[k], prev = ring[(k + 4) % 5], next = ring[(k + 1) % 5];
                y[static_cast<std::size_t>(ch)].v[static_cast<std::size_t>(i)] = (1.0 - 2 * amount) * src[ch] + amount * src[prev] + amount * src[next];
            }
        }
    });
}

Out5 angleField(const StereoSpec& x, double width, int mode, double adjacent, bool preserve) {
    const int F = x.l.F, T = x.l.T;
    Out5 y = makeOut(F, T);
    const std::size_t n = x.l.v.size();
    parallelFor(static_cast<long long>(n), 16384, [&](long long b, long long e) {
        double g1[5], g2[5];
        for (long long i = b; i < e; ++i) {
            const cd l = x.l.v[static_cast<std::size_t>(i)], r = x.r.v[static_cast<std::size_t>(i)];
            const double az = position(l, r) * (width * 0.5);
            const bool leftStrong = std::abs(l) >= std::abs(r);
            const cd strong = leftStrong ? l : r, weak = leftStrong ? r : l;
            ringPan(az, g1);
            if (mode == 0) ringPan(az, g2);
            else if (mode == 1) ringPan(-az, g2);
            else ringPan(wrapDeg(az + 180.0), g2);
            for (int c = 0; c < 5; ++c) y[static_cast<std::size_t>(c)].v[static_cast<std::size_t>(i)] = strong * g1[c] + weak * g2[c];
        }
    });
    adjacentFill(y, adjacent);
    if (preserve) foldCorrectionFronts(y, x);
    return y;
}

void sliceGains(double az, double softness, double hardMix, double out[5]) {
    double soft[5], dist[5];
    int idx = 0;
    double ss = 0;
    for (int k = 0; k < 5; ++k) {
        dist[k] = angularDistance(az, kAngles5[k]);
        if (dist[k] < dist[idx]) idx = k;
        soft[k] = std::exp(-0.5 * std::pow(dist[k] / std::max(softness, 1.0), 2));
        ss += soft[k] * soft[k];
    }
    ss = std::sqrt(ss + 1e-12);
    const double h = std::clamp(hardMix, 0.0, 1.0);
    double gs = 0;
    for (int k = 0; k < 5; ++k) {
        out[k] = (1.0 - h) * soft[k] / ss + (k == idx ? h : 0.0);
        gs += out[k] * out[k];
    }
    gs = std::sqrt(gs + 1e-12);
    for (int k = 0; k < 5; ++k) out[k] /= gs;
}

Out5 sectorSlice(const StereoSpec& x, double width, double softness, double stage1, double stage2, bool preserve) {
    const int F = x.l.F, T = x.l.T;
    Out5 y = makeOut(F, T);
    const std::size_t n = x.l.v.size();
    parallelFor(static_cast<long long>(n), 16384, [&](long long b, long long e) {
        double cont[5], staged[5], hard2[5], gains[5];
        for (long long i = b; i < e; ++i) {
            const cd l = x.l.v[static_cast<std::size_t>(i)], r = x.r.v[static_cast<std::size_t>(i)];
            const double pos = position(l, r);
            const double az = pos * (width * 0.5);
            ringPan(az, cont);
            sliceGains(az, softness, stage1, staged);
            const double rearDist = std::min(angularDistance(az, -110.0), angularDistance(az, 110.0));
            const double gate = std::clamp(1.0 - rearDist / 80.0, 0.0, 1.0);
            sliceGains(az, std::max(8.0, softness * 0.55), stage2, hard2);
            const double wet = std::clamp(stage1, 0.0, 1.0);
            double gs = 0;
            for (int k = 0; k < 5; ++k) {
                gains[k] = wet * (staged[k] * (1.0 - gate) + hard2[k] * gate) + (1.0 - wet) * cont[k];
                gs += gains[k] * gains[k];
            }
            gs = std::sqrt(gs + 1e-12);
            const cd carrier = l + r;
            for (int k = 0; k < 5; ++k) y[static_cast<std::size_t>(k)].v[static_cast<std::size_t>(i)] = carrier * (gains[k] / gs);
            const cd side = 0.5 * (l - r);
            const double edge = std::clamp(std::pow(std::abs(pos), 1.5), 0.0, 1.0);
            if (pos < 0) y[3].v[static_cast<std::size_t>(i)] += edge * side;
            if (pos > 0) y[4].v[static_cast<std::size_t>(i)] -= edge * side;
        }
    });
    if (preserve) foldCorrectionFronts(y, x);
    return y;
}

std::vector<double> transientMask(const StereoSpec& x, double thresholdQuantile, double softness) {
    const int F = x.l.F, T = x.l.T;
    std::vector<double> flux(static_cast<std::size_t>(T), 0.0), denom(static_cast<std::size_t>(T), 0.0);
    for (int t = 0; t < T; ++t) {
        double s = 0, d = 0;
        for (int f = 0; f < F; ++f) {
            const double m = std::sqrt(std::norm(x.l.at(f, t)) + std::norm(x.r.at(f, t)));
            d += m;
            if (t > 0) {
                const double p = std::sqrt(std::norm(x.l.at(f, t - 1)) + std::norm(x.r.at(f, t - 1)));
                s += std::max(m - p, 0.0);
            }
        }
        flux[static_cast<std::size_t>(t)] = s;
        denom[static_cast<std::size_t>(t)] = d + 1e-12;
    }
    for (int t = 0; t < T; ++t) flux[static_cast<std::size_t>(t)] /= denom[static_cast<std::size_t>(t)];
    hp::gaussianReflect(flux, 1.0);
    const double thr = quantile(flux, std::clamp(thresholdQuantile, 0.5, 0.99));
    std::vector<double> dev(flux.size());
    for (std::size_t i = 0; i < flux.size(); ++i) dev[i] = std::abs(flux[i] - thr);
    const double scale = std::max(median(dev) * 1.4826, 1e-6);
    std::vector<double> tm(flux.size());
    for (std::size_t i = 0; i < flux.size(); ++i) {
        const double z = std::clamp((flux[i] - thr) / (std::max(softness, 1e-3) * scale), -30.0, 30.0);
        tm[i] = 1.0 / (1.0 + std::exp(-z));
    }
    return tm;
}

Out5 transientHybrid(const StereoSpec& x, double width, int mode, bool preserve) {
    Out5 ya = angleField(x, width, mode, 0.02, false);
    const Out5 ys = sectorSlice(x, std::max(width, 280.0), 18.0, 0.92, 0.97, false);
    const std::vector<double> tm = transientMask(x, 0.84, 0.22);
    const int F = x.l.F, T = x.l.T;
    for (int c = 0; c < 5; ++c)
        for (int f = 0; f < F; ++f)
            for (int t = 0; t < T; ++t) {
                const double m = tm[static_cast<std::size_t>(t)];
                ya[static_cast<std::size_t>(c)].at(f, t) = ya[static_cast<std::size_t>(c)].at(f, t) * (1.0 - m) + ys[static_cast<std::size_t>(c)].at(f, t) * m;
            }
    if (preserve) foldCorrectionFronts(ya, x);
    return ya;
}

struct Masks {
    Field center, left, right, coh;
};

Masks spatialMasks(const StereoSpec& x, double centerIld, double centerPhase, double rearStart) {
    const int F = x.l.F, T = x.l.T;
    CField cross(F, T);
    Field pl(F, T), pr(F, T);
    for (std::size_t i = 0; i < cross.v.size(); ++i) {
        cross.v[i] = x.l.v[i] * std::conj(x.r.v[i]);
        pl.v[i] = std::norm(x.l.v[i]);
        pr.v[i] = std::norm(x.r.v[i]);
    }
    boxSmooth(cross, 3, 7);
    boxSmooth(pl, 3, 7);
    boxSmooth(pr, 3, 7);
    Masks m{Field(F, T), Field(F, T), Field(F, T), Field(F, T)};
    parallelFor(static_cast<long long>(cross.v.size()), 16384, [&](long long b, long long e) {
        for (long long ii = b; ii < e; ++ii) {
            const std::size_t i = static_cast<std::size_t>(ii);
            const double coh = std::clamp(std::abs(cross.v[i]) / std::sqrt(std::max(pl.v[i], kEps) * std::max(pr.v[i], kEps)), 0.0, 1.0);
            const double phase = std::arg(cross.v[i]);
            const double ild = 20.0 * std::log10((std::abs(x.l.v[i]) + kEps) / (std::abs(x.r.v[i]) + kEps));
            double c = std::exp(-0.5 * std::pow(ild / std::max(centerIld, 0.25), 2));
            c *= std::exp(-0.5 * std::pow(phase / std::max(centerPhase, 0.05), 2));
            c *= std::pow(coh, 1.15);
            const double pos = position(x.l.v[i], x.r.v[i]);
            const double left = std::pow(std::clamp((-pos - rearStart) / std::max(1 - rearStart, 1e-3), 0.0, 1.0), 1.25);
            const double right = std::pow(std::clamp((pos - rearStart) / std::max(1 - rearStart, 1e-3), 0.0, 1.0), 1.25);
            const double diffuse = 0.72 + 0.28 * (1.0 - coh);
            m.center.v[i] = std::clamp(c, 0.0, 1.0);
            m.left.v[i] = left * diffuse;
            m.right.v[i] = right * diffuse;
            m.coh.v[i] = coh;
        }
    });
    return m;
}

Out5 foldExactCore(const StereoSpec& x, const Field& cm, const Field& lm, const Field& rm, double centerStrength, double rearStrength) {
    Out5 y = makeOut(x.l.F, x.l.T);
    const std::size_t n = x.l.v.size();
    for (std::size_t i = 0; i < n; ++i) {
        const cd l = x.l.v[i], r = x.r.v[i];
        const cd shared = 0.5 * (l + r);
        const cd c = centerStrength * cm.v[i] * shared / kDown;
        const cd sl = rearStrength * lm.v[i] * l / kDown;
        const cd sr = rearStrength * rm.v[i] * r / kDown;
        y[0].v[i] = l - kDown * c - kDown * sl;
        y[1].v[i] = r - kDown * c - kDown * sr;
        y[2].v[i] = c;
        y[3].v[i] = sl;
        y[4].v[i] = sr;
    }
    return y;
}

Out5 foldExact(const StereoSpec& x) {
    const Masks m = spatialMasks(x, 5.5, 0.65, 0.38);
    return foldExactCore(x, m.center, m.left, m.right, 0.92, 0.66);
}

Out5 guidedScene(const StereoSpec& mix, const StereoSpec& ref, double guideStrength) {
    const int F = mix.l.F, T = mix.l.T;
    CField xs(F, T), rs(F, T);
    for (std::size_t i = 0; i < xs.v.size(); ++i) {
        xs.v[i] = 0.5 * (mix.l.v[i] - mix.r.v[i]);
        rs.v[i] = 0.5 * (ref.l.v[i] - ref.r.v[i]);
    }
    std::vector<double> hr(static_cast<std::size_t>(F)), hi(static_cast<std::size_t>(F));
    parallelFor(F, 8, [&](long long b, long long e) {
        std::vector<double> mag(static_cast<std::size_t>(T)), w(static_cast<std::size_t>(T), 1.0);
        for (long long f = b; f < e; ++f) {
            double den = 0;
            cd num(0, 0);
            for (int t = 0; t < T; ++t) {
                den += std::norm(rs.at(static_cast<int>(f), t));
                num += xs.at(static_cast<int>(f), t) * std::conj(rs.at(static_cast<int>(f), t));
            }
            cd h = num / (den + 1e-12);
            for (int it = 0; it < 4; ++it) {
                for (int t = 0; t < T; ++t) mag[static_cast<std::size_t>(t)] = std::abs(xs.at(static_cast<int>(f), t) - h * rs.at(static_cast<int>(f), t));
                const double scale = median(mag) + 1e-9;
                cd nn(0, 0);
                double dd = 0;
                for (int t = 0; t < T; ++t) {
                    const double wt = std::min(1.0, 2.5 * scale / (mag[static_cast<std::size_t>(t)] + 1e-12));
                    nn += wt * xs.at(static_cast<int>(f), t) * std::conj(rs.at(static_cast<int>(f), t));
                    dd += wt * std::norm(rs.at(static_cast<int>(f), t));
                }
                h = nn / (dd + 1e-12);
            }
            hr[static_cast<std::size_t>(f)] = h.real();
            hi[static_cast<std::size_t>(f)] = h.imag();
        }
    });
    hp::gaussianReflect(hr, 2.0);
    hp::gaussianReflect(hi, 2.0);
    std::vector<cd> h(static_cast<std::size_t>(F));
    for (int f = 0; f < F; ++f) {
        const cd v(hr[static_cast<std::size_t>(f)], hi[static_cast<std::size_t>(f)]);
        h[static_cast<std::size_t>(f)] = std::polar(std::clamp(std::abs(v), 0.35, 2.8), std::arg(v));
    }
    CField cross(F, T);
    Field px(F, T), pr(F, T);
    for (std::size_t i = 0; i < cross.v.size(); ++i) {
        cross.v[i] = xs.v[i] * std::conj(rs.v[i]);
        px.v[i] = std::norm(xs.v[i]);
        pr.v[i] = std::norm(rs.v[i]);
    }
    boxSmooth(cross, 3, 7);
    boxSmooth(px, 3, 7);
    boxSmooth(pr, 3, 7);
    StereoSpec predicted{CField(F, T), CField(F, T)}, residual{CField(F, T), CField(F, T)};
    const double gs = std::clamp(guideStrength, 0.0, 1.0);
    for (int f = 0; f < F; ++f)
        for (int t = 0; t < T; ++t) {
            const std::size_t i = static_cast<std::size_t>(f) * T + static_cast<std::size_t>(t);
            const double coh = std::clamp(std::abs(cross.v[i]) / std::sqrt(std::max(px.v[i], kEps) * std::max(pr.v[i], kEps)), 0.0, 1.0);
            const double conf = std::pow(coh, 1.5) * gs;
            predicted.l.v[i] = conf * h[static_cast<std::size_t>(f)] * ref.l.v[i];
            predicted.r.v[i] = conf * h[static_cast<std::size_t>(f)] * ref.r.v[i];
            residual.l.v[i] = mix.l.v[i] - predicted.l.v[i];
            residual.r.v[i] = mix.r.v[i] - predicted.r.v[i];
        }
    const Masks a = spatialMasks(ref, 5.0, 0.62, 0.32);
    Out5 y = foldExactCore(predicted, a.center, a.left, a.right, 0.95, 0.72);
    Masks v = spatialMasks(residual, 6.5, 0.78, 0.52);
    for (double& c : v.center.v) c = std::clamp(c * 1.15, 0.0, 1.0);
    const Out5 yv = foldExactCore(residual, v.center, v.left, v.right, std::min(1.0, 0.98), 0.12);
    for (int c = 0; c < 5; ++c)
        for (std::size_t i = 0; i < y[static_cast<std::size_t>(c)].v.size(); ++i) y[static_cast<std::size_t>(c)].v[i] += yv[static_cast<std::size_t>(c)].v[i];
    return y;
}

void addInto(Out5& y, const Out5& b) {
    for (int c = 0; c < 5; ++c)
        for (std::size_t i = 0; i < y[static_cast<std::size_t>(c)].v.size(); ++i) y[static_cast<std::size_t>(c)].v[i] += b[static_cast<std::size_t>(c)].v[i];
}

std::vector<double> derivKernel(double sigma) {
    const int r = static_cast<int>(4.0 * sigma + 0.5);
    std::vector<double> k(static_cast<std::size_t>(2 * r + 1));
    double s = 0;
    for (int i = -r; i <= r; ++i) s += std::exp(-0.5 * i * i / (sigma * sigma));
    for (int i = -r; i <= r; ++i) k[static_cast<std::size_t>(i + r)] = (i / (sigma * sigma)) * std::exp(-0.5 * i * i / (sigma * sigma)) / s;
    return k;
}

std::vector<double> smoothKernel(double sigma) {
    const int r = static_cast<int>(4.0 * sigma + 0.5);
    std::vector<double> k(static_cast<std::size_t>(2 * r + 1));
    double s = 0;
    for (int i = -r; i <= r; ++i) s += k[static_cast<std::size_t>(i + r)] = std::exp(-0.5 * i * i / (sigma * sigma));
    for (double& v : k) v /= s;
    return k;
}

void correlateNearest(std::vector<double>& x, const std::vector<double>& k) {
    const int n = static_cast<int>(x.size()), r = static_cast<int>(k.size() / 2);
    const std::vector<double> src = x;
    for (int i = 0; i < n; ++i) {
        double s = 0;
        for (int d = -r; d <= r; ++d) s += k[static_cast<std::size_t>(d + r)] * src[static_cast<std::size_t>(std::clamp(i + d, 0, n - 1))];
        x[static_cast<std::size_t>(i)] = s;
    }
}

Field derivative(const Field& a, double sigma, bool alongTime) {
    const std::vector<double> k = derivKernel(sigma);
    const int r = static_cast<int>(k.size() / 2), F = a.F, T = a.T;
    Field out(F, T);
    if (alongTime) {
        parallelFor(F, 16, [&](long long b, long long e) {
            for (long long f = b; f < e; ++f)
                for (int t = 0; t < T; ++t) {
                    double s = 0;
                    for (int d = -r; d <= r; ++d) s += k[static_cast<std::size_t>(d + r)] * a.at(static_cast<int>(f), std::clamp(t + d, 0, T - 1));
                    out.at(static_cast<int>(f), t) = s;
                }
        });
    } else {
        parallelFor(T, 64, [&](long long b, long long e) {
            for (long long t = b; t < e; ++t)
                for (int f = 0; f < F; ++f) {
                    double s = 0;
                    for (int d = -r; d <= r; ++d) s += k[static_cast<std::size_t>(d + r)] * a.at(std::clamp(f + d, 0, F - 1), static_cast<int>(t));
                    out.at(f, static_cast<int>(t)) = s;
                }
        });
    }
    return out;
}

Field referencePowerScale(const StereoSpec& mix, const StereoSpec& ref) {
    const int F = mix.l.F, T = mix.l.T;
    Field pm(F, T), pr(F, T);
    for (std::size_t i = 0; i < pm.v.size(); ++i) {
        pm.v[i] = std::norm(0.5 * (mix.l.v[i] - mix.r.v[i]));
        pr.v[i] = std::norm(0.5 * (ref.l.v[i] - ref.r.v[i]));
    }
    boxSmooth(pm, 5, 13);
    boxSmooth(pr, 5, 13);
    for (std::size_t i = 0; i < pm.v.size(); ++i) {
        pm.v[i] = std::max(pm.v[i], kEps);
        pr.v[i] = std::max(pr.v[i], kEps);
    }
    Field ratio(F, T);
    for (std::size_t i = 0; i < ratio.v.size(); ++i) ratio.v[i] = std::clamp(pm.v[i] / pr.v[i], 0.08, 12.0);
    const double thr = quantile(pr.v, 0.35);
    std::vector<double> logs;
    for (std::size_t i = 0; i < pr.v.size(); ++i)
        if (pr.v[i] > thr) logs.push_back(std::log(ratio.v[i] + kEps));
    const double globalLog = logs.empty() ? 0.0 : median(logs);
    const double floor = quantile(pr.v, 0.55) + kEps;
    Field loga(F, T);
    for (std::size_t i = 0; i < loga.v.size(); ++i) {
        const double rel = std::clamp(pr.v[i] / (pr.v[i] + 0.30 * floor), 0.0, 1.0);
        loga.v[i] = rel * std::log(ratio.v[i] + kEps) + (1.0 - rel) * globalLog;
    }
    gaussianNearest(loga, 2.0, 3.0);
    for (double& v : loga.v) v = std::clamp(std::exp(v), 0.10, 10.0);
    return loga;
}

struct Cov {
    Field c00, c11;
    CField c01;
};

Cov covariance(const StereoSpec& z) {
    const int F = z.l.F, T = z.l.T;
    Cov c{Field(F, T), Field(F, T), CField(F, T)};
    for (std::size_t i = 0; i < c.c00.v.size(); ++i) {
        c.c00.v[i] = std::norm(z.l.v[i]);
        c.c11.v[i] = std::norm(z.r.v[i]);
        c.c01.v[i] = z.l.v[i] * std::conj(z.r.v[i]);
    }
    boxSmooth(c.c00, 3, 9);
    boxSmooth(c.c11, 3, 9);
    boxSmooth(c.c01, 3, 9);
    for (std::size_t i = 0; i < c.c00.v.size(); ++i) {
        c.c00.v[i] = std::max(c.c00.v[i], 0.0);
        c.c11.v[i] = std::max(c.c11.v[i], 0.0);
    }
    return c;
}

Out5 renderPair(const StereoSpec& ref, const StereoSpec& supported, const StereoSpec& innovation, double refIld, double refPhase, double refRear,
                double refCentre, double refRearStrength, double inIld, double inPhase, double inRear, double inBoost, double inCentre,
                double inRearStrength) {
    const Masks a = spatialMasks(ref, refIld, refPhase, refRear);
    Out5 y = foldExactCore(supported, a.center, a.left, a.right, refCentre, refRearStrength);
    Masks v = spatialMasks(innovation, inIld, inPhase, inRear);
    for (double& c : v.center.v) c = std::clamp(inBoost * c, 0.0, 1.0);
    addInto(y, foldExactCore(innovation, v.center, v.left, v.right, inCentre, inRearStrength));
    return y;
}

Out5 contrastCov(const StereoSpec& mix, const StereoSpec& ref) {
    const int F = mix.l.F, T = mix.l.T;
    const Cov m = covariance(mix), r = covariance(ref);
    const Field alpha = referencePowerScale(mix, ref);
    Field gain(F, T);
    std::vector<cd> v0(gain.v.size()), v1(gain.v.size());
    parallelFor(static_cast<long long>(gain.v.size()), 16384, [&](long long b, long long e) {
        for (long long ii = b; ii < e; ++ii) {
            const std::size_t i = static_cast<std::size_t>(ii);
            const double al = alpha.v[i];
            const double s00 = al * r.c00.v[i], s11 = al * r.c11.v[i];
            const cd s01 = al * r.c01.v[i];
            const double a = m.c00.v[i] - s00, bb = m.c11.v[i] - s11;
            const cd c = m.c01.v[i] - s01;
            const double disc = std::sqrt(std::max((a - bb) * (a - bb) + 4.0 * std::norm(c), 0.0));
            const double lam = 0.5 * (a + bb + disc);
            const double lamPos = std::max(lam, 0.0);
            cd x0 = c, x1(lam - a, 0.0);
            const cd y0(lam - bb, 0.0), y1 = std::conj(c);
            if (std::norm(y0) + std::norm(y1) > std::norm(x0) + std::norm(x1)) {
                x0 = y0;
                x1 = y1;
            }
            double n = std::sqrt(std::norm(x0) + std::norm(x1));
            if (n < 1e-15) {
                x0 = a >= bb ? cd(1, 0) : cd(0, 0);
                x1 = a >= bb ? cd(0, 0) : cd(1, 0);
                n = 1.0;
            }
            x0 /= std::max(n, 1e-15);
            x1 /= std::max(n, 1e-15);
            const double refVar = std::max(s00 * std::norm(x0) + s11 * std::norm(x1) + 2.0 * std::real(std::conj(x0) * s01 * x1), 0.0);
            const double novelty = std::clamp(lamPos / std::max(m.c00.v[i] + m.c11.v[i], kEps), 0.0, 1.0);
            double g = lamPos / (lamPos + refVar + kEps);
            g *= std::sqrt(novelty);
            gain.v[i] = std::clamp(g, 0.0, 0.94);
            v0[i] = x0;
            v1[i] = x1;
        }
    });
    gaussianNearest(gain, 0.75, 1.25);
    StereoSpec innovation{CField(F, T), CField(F, T)}, supported{CField(F, T), CField(F, T)};
    for (std::size_t i = 0; i < gain.v.size(); ++i) {
        const cd coeff = std::conj(v0[i]) * mix.l.v[i] + std::conj(v1[i]) * mix.r.v[i];
        innovation.l.v[i] = gain.v[i] * v0[i] * coeff;
        innovation.r.v[i] = gain.v[i] * v1[i] * coeff;
        supported.l.v[i] = mix.l.v[i] - innovation.l.v[i];
        supported.r.v[i] = mix.r.v[i] - innovation.r.v[i];
    }
    return renderPair(ref, supported, innovation, 5.2, 0.66, 0.30, 0.92, 0.76, 7.0, 0.86, 0.62, 1.18, 0.985, 0.035);
}

Field logMagnitude(const StereoSpec& z) {
    Field out(z.l.F, z.l.T);
    for (std::size_t i = 0; i < out.v.size(); ++i) out.v[i] = 0.5 * std::log(std::max(std::norm(z.l.v[i]) + std::norm(z.r.v[i]), 1e-18));
    return out;
}

Field featureSimilarity(const Field& a, const Field& b, double floor) {
    Field local(a.F, a.T), out(a.F, a.T);
    for (std::size_t i = 0; i < local.v.size(); ++i) local.v[i] = std::abs(a.v[i]) + std::abs(b.v[i]);
    gaussianNearest(local, 1.0, 2.0);
    for (std::size_t i = 0; i < out.v.size(); ++i) out.v[i] = std::exp(-std::abs(a.v[i] - b.v[i]) / std::max(0.35 * local.v[i], floor));
    return out;
}

Out5 modulationLock(const StereoSpec& mix, const StereoSpec& ref) {
    const int F = mix.l.F, T = mix.l.T;
    const Field lm = logMagnitude(mix), lr0 = logMagnitude(ref);
    std::vector<double> frame(static_cast<std::size_t>(T)), freq(static_cast<std::size_t>(F)), col(static_cast<std::size_t>(F)), row(static_cast<std::size_t>(T));
    for (int t = 0; t < T; ++t) {
        for (int f = 0; f < F; ++f) col[static_cast<std::size_t>(f)] = lm.at(f, t) - lr0.at(f, t);
        frame[static_cast<std::size_t>(t)] = median(col);
    }
    correlateNearest(frame, smoothKernel(3.0));
    for (int f = 0; f < F; ++f) {
        for (int t = 0; t < T; ++t) row[static_cast<std::size_t>(t)] = lm.at(f, t) - lr0.at(f, t) - frame[static_cast<std::size_t>(t)];
        freq[static_cast<std::size_t>(f)] = median(row);
    }
    correlateNearest(freq, smoothKernel(6.0));
    Field lr(F, T);
    for (int f = 0; f < F; ++f)
        for (int t = 0; t < T; ++t) lr.at(f, t) = lr0.at(f, t) + frame[static_cast<std::size_t>(t)] + freq[static_cast<std::size_t>(f)];

    const Field st1 = featureSimilarity(derivative(lm, 1.0, true), derivative(lr, 1.0, true), 0.055);
    const Field st4 = featureSimilarity(derivative(lm, 4.0, true), derivative(lr, 4.0, true), 0.025);
    const Field sf1 = featureSimilarity(derivative(lm, 1.0, false), derivative(lr, 1.0, false), 0.055);
    const Field sf4 = featureSimilarity(derivative(lm, 4.0, false), derivative(lr, 4.0, false), 0.025);

    Field shared(F, T);
    parallelFor(static_cast<long long>(shared.v.size()), 16384, [&](long long b, long long e) {
        for (long long ii = b; ii < e; ++ii) {
            const std::size_t i = static_cast<std::size_t>(ii);
            const double magAgree = std::exp(-std::abs(lm.v[i] - lr.v[i]) / 0.78);
            const double cap = std::clamp(0.30 + 0.70 * std::exp(std::clamp(lr.v[i] - lm.v[i], -8.0, 0.0)), 0.0, 1.0);
            const double dp = (position(mix.l.v[i], mix.r.v[i]) - position(ref.l.v[i], ref.r.v[i])) / 0.22;
            const double sPos = std::exp(-0.5 * dp * dp);
            const double dIpd = std::abs(std::arg(std::polar(1.0, std::arg(mix.l.v[i] * std::conj(mix.r.v[i])) - std::arg(ref.l.v[i] * std::conj(ref.r.v[i]))))) / 0.72;
            const double sIpd = std::exp(-0.5 * dIpd * dIpd);
            const double logs = 0.22 * std::log(st1.v[i] + kEps) + 0.18 * std::log(st4.v[i] + kEps) + 0.16 * std::log(sf1.v[i] + kEps) +
                                0.12 * std::log(sf4.v[i] + kEps) + 0.14 * std::log(magAgree + kEps) + 0.10 * std::log(sPos + kEps) +
                                0.08 * std::log(sIpd + kEps);
            shared.v[i] = std::clamp(std::exp(logs) * cap, 0.0, 1.0);
        }
    });
    gaussianNearest(shared, 0.85, 1.6);
    StereoSpec innovation{CField(F, T), CField(F, T)}, supported{CField(F, T), CField(F, T)};
    for (std::size_t i = 0; i < shared.v.size(); ++i) {
        const double s = std::clamp(shared.v[i], 0.0, 1.0);
        supported.l.v[i] = s * mix.l.v[i];
        supported.r.v[i] = s * mix.r.v[i];
        innovation.l.v[i] = mix.l.v[i] - supported.l.v[i];
        innovation.r.v[i] = mix.r.v[i] - supported.r.v[i];
    }
    return renderPair(ref, supported, innovation, 5.0, 0.66, 0.28, 0.91, 0.78, 7.3, 0.90, 0.66, 1.20, 0.99, 0.025);
}

std::vector<double> bandFreqs(int F, double workSr) {
    std::vector<double> f(static_cast<std::size_t>(F));
    for (int i = 0; i < F; ++i) f[static_cast<std::size_t>(i)] = F > 1 ? 0.5 * workSr * i / (F - 1) : 0.0;
    return f;
}

std::vector<double> raised(const std::vector<double>& freq, double cutoff, double widthOct, bool highPass) {
    cutoff = std::max(cutoff, 1.0);
    const double lo = cutoff / std::pow(2.0, 0.5 * widthOct), hi = cutoff * std::pow(2.0, 0.5 * widthOct);
    std::vector<double> y(freq.size());
    for (std::size_t i = 0; i < freq.size(); ++i) {
        const double x = std::clamp(std::log2(std::max(freq[i], 1e-9) / lo) / std::max(std::log2(hi / lo), 1e-9), 0.0, 1.0);
        double v = highPass ? 0.5 - 0.5 * std::cos(kPi * x) : 0.5 + 0.5 * std::cos(kPi * x);
        if (freq[i] <= lo) v = highPass ? 0.0 : 1.0;
        if (freq[i] >= hi) v = highPass ? 1.0 : 0.0;
        y[i] = v;
    }
    return y;
}

std::vector<double> highShelf(const std::vector<double>& freq, double startHz, double gainDb) {
    std::vector<double> y(freq.size(), 1.0);
    if (std::abs(gainDb) < 1e-12) return y;
    for (std::size_t i = 0; i < freq.size(); ++i) {
        double x = std::clamp((std::log2(std::max(freq[i], 1.0) / std::max(startHz, 1.0)) + 0.5) / 1.5, 0.0, 1.0);
        x = x * x * (3.0 - 2.0 * x);
        y[i] = std::pow(10.0, gainDb * x / 20.0);
    }
    return y;
}

CField softCentre(const StereoSpec& z, double floor, double strength) {
    const int F = z.l.F, T = z.l.T;
    Field ratio(F, T, 1.0);
    const int scales[3][2] = {{5, 1}, {11, 3}, {23, 7}};
    for (const auto& sc : scales) {
        Field pm(F, T), ps(F, T);
        for (std::size_t i = 0; i < pm.v.size(); ++i) {
            pm.v[i] = std::norm(0.5 * (z.l.v[i] + z.r.v[i]));
            ps.v[i] = std::norm(0.5 * (z.l.v[i] - z.r.v[i]));
        }
        boxSmooth(pm, sc[1], sc[0]);
        boxSmooth(ps, sc[1], sc[0]);
        for (std::size_t i = 0; i < pm.v.size(); ++i) {
            const double m = std::max(pm.v[i], kEps), s = std::max(ps.v[i], kEps);
            ratio.v[i] *= m / (m + strength * s + kEps);
        }
    }
    CField cross(F, T);
    Field pl(F, T), pr(F, T);
    for (std::size_t i = 0; i < cross.v.size(); ++i) {
        cross.v[i] = z.l.v[i] * std::conj(z.r.v[i]);
        pl.v[i] = std::norm(z.l.v[i]);
        pr.v[i] = std::norm(z.r.v[i]);
    }
    boxSmooth(cross, 5, 15);
    boxSmooth(pl, 5, 15);
    boxSmooth(pr, 5, 15);
    Field conf(F, T);
    for (std::size_t i = 0; i < conf.v.size(); ++i) {
        const double l = std::max(pl.v[i], kEps), r = std::max(pr.v[i], kEps);
        const double coh = std::clamp(std::abs(cross.v[i]) / std::sqrt(l * r), 0.0, 1.0);
        const double ild = 10.0 * std::log10(l / r) / 7.0, ipd = std::arg(cross.v[i]) / 0.90;
        const double geometry = std::exp(-0.5 * ild * ild) * std::exp(-0.5 * ipd * ipd) * (0.55 + 0.45 * coh);
        conf.v[i] = std::sqrt(std::clamp(std::cbrt(std::max(ratio.v[i], 0.0)), 0.0, 1.0) * std::clamp(geometry, 0.0, 1.0));
    }
    gaussianNearest(conf, 1.25, 2.75);
    floor = std::clamp(floor, 0.0, 0.98);
    CField c(F, T);
    for (std::size_t i = 0; i < c.v.size(); ++i) c.v[i] = std::clamp(floor + (1.0 - floor) * conf.v[i], 0.0, 1.0) * 0.5 * (z.l.v[i] + z.r.v[i]);
    return c;
}

Field wideVocalMask(const StereoSpec& v, double widthStart, double widthRamp) {
    const int F = v.l.F, T = v.l.T;
    Field acc(F, T);
    std::vector<double> power(static_cast<std::size_t>(F) * T);
    for (std::size_t i = 0; i < power.size(); ++i) power[i] = std::norm(v.l.v[i]) + std::norm(v.r.v[i]);
    const double floor = 1e-7 * *std::max_element(power.begin(), power.end()) + kEps;
    const int scales[3][2] = {{5, 1}, {11, 3}, {23, 7}};
    for (const auto& sc : scales) {
        Field pl(F, T), pr(F, T);
        CField cross(F, T);
        for (std::size_t i = 0; i < pl.v.size(); ++i) {
            pl.v[i] = std::norm(v.l.v[i]);
            pr.v[i] = std::norm(v.r.v[i]);
            cross.v[i] = v.l.v[i] * std::conj(v.r.v[i]);
        }
        boxSmooth(pl, sc[1], sc[0]);
        boxSmooth(pr, sc[1], sc[0]);
        boxSmooth(cross, sc[1], sc[0]);
        for (std::size_t i = 0; i < acc.v.size(); ++i) {
            const double l = std::max(pl.v[i], kEps), r = std::max(pr.v[i], kEps);
            const double coh = std::clamp(std::abs(cross.v[i]) / std::sqrt(l * r), 0.0, 1.0);
            const double pos = std::clamp(4.0 * std::atan2(std::sqrt(r), std::sqrt(l)) / kPi - 1.0, -1.0, 1.0);
            const double width = 1.0 - (1.0 - std::abs(pos)) * coh;
            const double x = std::clamp((width - widthStart) / widthRamp, 0.0, 1.0);
            const double present = (l + r) / (l + r + floor);
            acc.v[i] += present * (0.5 - 0.5 * std::cos(kPi * x)) / 3.0;
        }
    }
    gaussianNearest(acc, 1.25, 2.75);
    for (double& m : acc.v) m = std::clamp(m, 0.0, 1.0);
    return acc;
}

Out5 vocalAnchor(const StereoSpec& mix, const StereoSpec& ref, const StereoSpec& vocal, double workSr) {
    const int F = mix.l.F, T = mix.l.T;
    Out5 y = contrastCov(mix, ref);
    const CField vc = softCentre(vocal, 0.64, 1.15);
    const std::vector<double> freq = bandFreqs(F, workSr);
    const std::vector<double> hp170 = raised(freq, 170.0, 0.55, true), lp9500 = raised(freq, 9500.0, 0.85, false);
    const double gFront = std::pow(10.0, -5.0 / 20.0), width = 0.14;
    double vp = kEps, projL = 0, projR = 0;
    for (std::size_t i = 0; i < vc.v.size(); ++i) {
        vp += std::norm(vc.v[i]);
        projL += std::real(y[0].v[i] * std::conj(vc.v[i]));
        projR += std::real(y[1].v[i] * std::conj(vc.v[i]));
    }
    const double fillL = std::max(0.0, gFront - projL / vp), fillR = std::max(0.0, gFront - projR / vp);

    const CField ic = softCentre(ref, 0.0, 1.85);
    const std::vector<double> chp = raised(freq, 190.0, 0.60, true), clp = raised(freq, 6800.0, 0.90, false);
    CField icBand(F, T);
    Field pv(F, T), pr(F, T);
    for (int f = 0; f < F; ++f)
        for (int t = 0; t < T; ++t) {
            icBand.at(f, t) = ic.at(f, t) * (chp[static_cast<std::size_t>(f)] * clp[static_cast<std::size_t>(f)]);
            pv.at(f, t) = std::norm(vc.at(f, t));
            pr.at(f, t) = std::norm(icBand.at(f, t));
        }
    boxSmooth(pv, 5, 17);
    boxSmooth(pr, 5, 17);
    Field activity(F, T);
    for (std::size_t i = 0; i < activity.v.size(); ++i) activity.v[i] = std::sqrt(std::clamp(pv.v[i] / (pv.v[i] + pr.v[i] + kEps), 0.0, 1.0));
    gaussianNearest(activity, 1.0, 3.5);

    const std::vector<double> shp = raised(freq, 145.0, 0.65, true), air = highShelf(freq, 4200.0, 1.2);
    const double gBed = std::pow(10.0, -9.0 / 20.0), gSur = std::pow(10.0, -2.0 / 20.0), gWide = std::pow(10.0, -3.0 / 20.0);
    const Field wide = wideVocalMask(vocal, 0.40, 0.30);
    for (int f = 0; f < F; ++f) {
        const std::size_t fi = static_cast<std::size_t>(f);
        const double sideBand = hp170[fi] * lp9500[fi], surround = gSur * shp[fi] * air[fi];
        for (int t = 0; t < T; ++t) {
            const std::size_t i = fi * static_cast<std::size_t>(T) + static_cast<std::size_t>(t);
            const cd vside = 0.5 * (vocal.l.v[i] - vocal.r.v[i]) * sideBand;
            y[0].v[i] += fillL * vc.v[i] + gFront * width * vside;
            y[1].v[i] += fillR * vc.v[i] - gFront * width * vside;
            const double duck = std::pow(10.0, -3.5 * activity.v[i] / 20.0);
            y[2].v[i] = vc.v[i] + gBed * duck * icBand.v[i];
            const double w = gWide * wide.v[i] * sideBand;
            y[3].v[i] = surround * (ref.l.v[i] - ic.v[i]) + w * vocal.l.v[i];
            y[4].v[i] = surround * (ref.r.v[i] - ic.v[i]) + w * vocal.r.v[i];
        }
    }
    return y;
}

std::vector<double> processBlock(const std::vector<double>& x, const std::vector<double>* ref, const std::vector<double>* voc, int sr, Method m) {
    const int os = 2, nBase = 4096;
    const std::size_t len = x.size() / 2;
    std::vector<double> xl = channelOf(x, 2, 0), xr = channelOf(x, 2, 1);
    const std::vector<double> ul = resamplePoly(xl, os, 1, 10.0), ur = resamplePoly(xr, os, 1, 10.0);
    const int n = nBase * os, hop = n / 4;
    StereoSpec X{stft(ul, n, hop), stft(ur, n, hop)};
    Out5 y;
    switch (m) {
    case Method::Angle: y = angleField(X, 290.0, 1, 0.025, true); break;
    case Method::Slice: y = sectorSlice(X, 290.0, 21.0, 0.88, 0.93, true); break;
    case Method::Hybrid: y = transientHybrid(X, 290.0, 1, true); break;
    case Method::FoldExact: y = foldExact(X); break;
    case Method::GuidedScene:
    case Method::ContrastCov:
    case Method::ModulationLock:
    case Method::VocalAnchor: {
        if (!ref) throw std::runtime_error(std::string(methodName(m)) + " needs the instrumental");
        const StereoSpec R{stft(resamplePoly(channelOf(*ref, 2, 0), os, 1, 10.0), n, hop), stft(resamplePoly(channelOf(*ref, 2, 1), os, 1, 10.0), n, hop)};
        if (m == Method::VocalAnchor) {
            if (!voc) throw std::runtime_error("Vocal anchor needs the extracted vocal");
            const StereoSpec V{stft(resamplePoly(channelOf(*voc, 2, 0), os, 1, 10.0), n, hop), stft(resamplePoly(channelOf(*voc, 2, 1), os, 1, 10.0), n, hop)};
            y = vocalAnchor(X, R, V, static_cast<double>(sr) * os);
        } else {
            y = m == Method::GuidedScene ? guidedScene(X, R, 0.90) : m == Method::ContrastCov ? contrastCov(X, R) : modulationLock(X, R);
        }
        break;
    }
    default: throw std::runtime_error("not a SpecField method");
    }
    std::vector<double> out(len * 5, 0.0);
    for (int c = 0; c < 5; ++c) {
        std::vector<double> yt = resamplePoly(istft(y[static_cast<std::size_t>(c)], n, hop, ul.size()), 1, os, 10.0);
        yt.resize(len, 0.0);
        for (std::size_t i = 0; i < len; ++i) out[i * 5 + static_cast<std::size_t>(c)] = yt[i];
    }
    if (m == Method::VocalAnchor) return out;
    for (std::size_t i = 0; i < len; ++i) {
        double* s = &out[i * 5];
        const double fl = s[0] + kDown * s[2] + kDown * s[3], fr = s[1] + kDown * s[2] + kDown * s[4];
        s[0] += xl[i] - fl;
        s[1] += xr[i] - fr;
    }
    return out;
}

}

Multi specField(const std::vector<double>& stereo, int sr, Method m, const std::vector<double>* ref, bool lfe, const Progress& progress,
                bool& cancelled, const std::vector<double>* vocal) {
    cancelled = false;
    const std::size_t len = stereo.size() / 2;
    const std::size_t block = std::max<std::size_t>(4096, static_cast<std::size_t>(std::lround(12.0 * sr)));
    const std::size_t overlap = static_cast<std::size_t>(std::lround(1.5 * sr));
    const std::size_t step = block - overlap;
    std::vector<double> out(len * 5, 0.0), weights(len, 0.0);
    std::vector<std::size_t> starts;
    for (std::size_t s = 0; s < len; s += step) starts.push_back(s);
    for (std::size_t bi = 0; bi < starts.size(); ++bi) {
        const std::size_t start = starts[bi], end = std::min(len, start + block);
        if (end - start < 2048 && start > 0) continue;
        if (progress && !progress(static_cast<double>(bi) / starts.size())) {
            cancelled = true;
            return {};
        }
        const std::vector<double> xb(stereo.begin() + static_cast<std::ptrdiff_t>(start * 2), stereo.begin() + static_cast<std::ptrdiff_t>(end * 2));
        std::vector<double> rb;
        if (ref) rb.assign(ref->begin() + static_cast<std::ptrdiff_t>(start * 2), ref->begin() + static_cast<std::ptrdiff_t>(end * 2));
        std::vector<double> vb(vocal ? (end - start) * 2 : 0);
        for (std::size_t i = 0; i < vb.size(); ++i) vb[i] = (*vocal)[start * 2 + i];
        const std::vector<double> yb = processBlock(xb, ref ? &rb : nullptr, vocal ? &vb : nullptr, sr, m);
        const std::size_t L = end - start;
        const std::size_t ov = std::min(overlap, L / 2);
        for (std::size_t i = 0; i < L; ++i) {
            double w = 1.0;
            if (ov > 0) {
                if (start > 0 && i < ov) w = std::pow(std::sin(0.5 * kPi * static_cast<double>(i) / static_cast<double>(ov)), 2);
                if (end < len && i >= L - ov) {
                    const std::size_t j = i - (L - ov);
                    w = std::pow(std::sin(0.5 * kPi * static_cast<double>(ov - 1 - j) / static_cast<double>(ov)), 2);
                }
            }
            weights[start + i] += w;
            for (int c = 0; c < 5; ++c) out[(start + i) * 5 + static_cast<std::size_t>(c)] += yb[i * 5 + static_cast<std::size_t>(c)] * w;
        }
        if (end == len) break;
    }
    for (std::size_t i = 0; i < len; ++i) {
        if (weights[i] < 1e-12) {
            for (int c = 0; c < 5; ++c) out[i * 5 + static_cast<std::size_t>(c)] = 0;
            out[i * 5] = stereo[i * 2];
            out[i * 5 + 1] = stereo[i * 2 + 1];
            weights[i] = 1.0;
        }
        for (int c = 0; c < 5; ++c) out[i * 5 + static_cast<std::size_t>(c)] /= weights[i];
    }
    std::vector<double> mid(len);
    for (std::size_t i = 0; i < len; ++i) mid[i] = 0.5 * (stereo[i * 2] + stereo[i * 2 + 1]);
    const std::vector<double> lfeCh = phasePreservingLfe(mid, sr, lfe ? -12.0 : -110.0);
    Multi r;
    r.channels = 6;
    r.names = {"FL", "FR", "C", "LFE", "SL", "SR"};
    r.v.resize(len * 6);
    for (std::size_t i = 0; i < len; ++i) {
        const double* s = &out[i * 5];
        double* d = &r.v[i * 6];
        d[0] = s[0];
        d[1] = s[1];
        d[2] = s[2];
        d[3] = lfeCh[i];
        d[4] = s[3];
        d[5] = s[4];
    }
    if (progress) progress(1.0);
    return r;
}

}
}
