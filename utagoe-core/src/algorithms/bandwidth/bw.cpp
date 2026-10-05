#include "bw.h"
#include "mathconst.h"
#include "fft.h"
#include "parallel.h"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace utagoe {
namespace bw {
namespace {


int fftSize(double sr) {
    const double want = 4096.0 * sr / 44100.0;
    int n = 1024;
    while (n * 2 <= 16384 && std::abs(n * 2 - want) < std::abs(n - want)) n *= 2;
    return n;
}

std::vector<double> hann(int n) {
    std::vector<double> w(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) w[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / n);
    return w;
}

double quantile(std::vector<double> v, double q) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double pos = q * static_cast<double>(v.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(pos);
    const std::size_t hi = std::min(lo + 1, v.size() - 1);
    return v[lo] + (v[hi] - v[lo]) * (pos - static_cast<double>(lo));
}

double frameCutoff(const std::vector<double>& S, double binHz) {
    const int K = static_cast<int>(S.size());
    const int b = std::max(4, static_cast<int>(std::lround(250.0 / binHz)));
    const int kmin = std::max(b, static_cast<int>(std::lround(3000.0 / binHz)));
    if (K - 1 - b <= kmin) return -1.0;
    std::vector<double> cs(static_cast<std::size_t>(K) + 1, 0.0);
    for (int k = 0; k < K; ++k) cs[static_cast<std::size_t>(k) + 1] = cs[static_cast<std::size_t>(k)] + S[static_cast<std::size_t>(k)];
    auto avg = [&](int a, int e) { return (cs[static_cast<std::size_t>(e)] - cs[static_cast<std::size_t>(a)]) / std::max(1, e - a); };
    const double peak = *std::max_element(S.begin(), S.end());
    double bestDepth = -1e300, bestAbove = 0;
    int best = -1;
    for (int k = kmin; k <= K - 1 - b; ++k) {
        const double below = avg(k - b, k), above = avg(k + 1, k + 1 + b);
        if (below - above > bestDepth) {
            bestDepth = below - above;
            bestAbove = above;
            best = k;
        }
    }
    if (best < 0 || bestDepth < 30.0 || bestAbove > peak - 55.0) return -1.0;
    if (avg(best + 1, K) > bestAbove + 6.0) return -1.0;
    const double level = bestAbove + 0.5 * bestDepth;
    int edge = best;
    for (int k = std::min(K - 1, best + b); k >= std::max(0, best - b); --k)
        if (S[static_cast<std::size_t>(k)] >= level) { edge = k; break; }
    return (edge + 0.5) * binHz;
}

}

Profile analyse(const std::vector<float>& x, int channels, double sr) {
    Profile p;
    p.sr = sr;
    p.nFft = fftSize(sr);
    p.hop = p.nFft / 4;
    const int n = p.nFft, hop = p.hop, half = n / 2, K = n / 2 + 1;
    const long long len = static_cast<long long>(x.size() / static_cast<std::size_t>(std::max(1, channels)));
    const int T = static_cast<int>(len / hop + 1);
    p.cutoffHz.assign(static_cast<std::size_t>(T), -1.0);
    p.levelDb.assign(static_cast<std::size_t>(T), -300.0);
    p.active.assign(static_cast<std::size_t>(T), 0);
    const std::vector<double> w = hann(n);
    double wsum = 0;
    for (double v : w) wsum += v;
    const double scale = 2.0 / wsum;
    const double binHz = sr / n;
    const int smoothBins = std::max(1, static_cast<int>(std::lround(30.0 / binHz)));
    const int step = std::max(1, static_cast<int>(std::lround(50.0 / binHz)));
    const int G = (K + step - 1) / step;
    std::vector<float> coarse(static_cast<std::size_t>(T) * static_cast<std::size_t>(G));
    FftTables tables(static_cast<std::size_t>(n));
    parallelFor(T, 16, [&](long long b0, long long e0) {
        std::vector<float> buf(static_cast<std::size_t>(2 * n));
        std::vector<double> P(static_cast<std::size_t>(K)), S(static_cast<std::size_t>(K));
        for (long long t = b0; t < e0; ++t) {
            std::fill(P.begin(), P.end(), 0.0);
            for (int c = 0; c < channels; ++c) {
                for (int j = 0; j < n; ++j) {
                    const long long i = t * hop - half + j;
                    const float v = (i >= 0 && i < len) ? x[static_cast<std::size_t>(i) * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)] : 0.0f;
                    buf[static_cast<std::size_t>(2 * j)] = static_cast<float>(v * w[static_cast<std::size_t>(j)]);
                    buf[static_cast<std::size_t>(2 * j + 1)] = 0.0f;
                }
                fft_forward(buf.data(), static_cast<std::size_t>(n), tables);
                for (int k = 0; k < K; ++k) {
                    const double re = buf[static_cast<std::size_t>(2 * k)] * scale, im = buf[static_cast<std::size_t>(2 * k + 1)] * scale;
                    P[static_cast<std::size_t>(k)] += (re * re + im * im) / channels;
                }
            }
            double total = 0;
            for (int k = 0; k < K; ++k) total += P[static_cast<std::size_t>(k)];
            for (int k = 0; k < K; ++k) {
                const int a = std::max(0, k - smoothBins), e = std::min(K, k + smoothBins + 1);
                double sum = 0;
                for (int j = a; j < e; ++j) sum += P[static_cast<std::size_t>(j)];
                S[static_cast<std::size_t>(k)] = 10.0 * std::log10(sum / (e - a) + 1e-30);
            }
            p.levelDb[static_cast<std::size_t>(t)] = 10.0 * std::log10(total + 1e-30);
            p.cutoffHz[static_cast<std::size_t>(t)] = frameCutoff(S, binHz);
            for (int g = 0; g < G; ++g) coarse[static_cast<std::size_t>(t) * static_cast<std::size_t>(G) + static_cast<std::size_t>(g)] = static_cast<float>(S[static_cast<std::size_t>(g * step)]);
        }
    });
    const double loud = quantile(p.levelDb, 0.9);
    long long activeCount = 0, cliffCount = 0;
    std::vector<double> cliffs;
    for (int t = 0; t < T; ++t) {
        const bool on = p.levelDb[static_cast<std::size_t>(t)] > std::max(loud - 45.0, -90.0);
        p.active[static_cast<std::size_t>(t)] = on;
        if (!on) {
            p.cutoffHz[static_cast<std::size_t>(t)] = -1.0;
            continue;
        }
        ++activeCount;
        if (p.cutoffHz[static_cast<std::size_t>(t)] > 0) {
            ++cliffCount;
            cliffs.push_back(p.cutoffHz[static_cast<std::size_t>(t)]);
        }
    }
    p.cliffShare = activeCount > 0 ? static_cast<double>(cliffCount) / static_cast<double>(activeCount) : 0.0;
    p.typicalHz = p.cliffShare >= 0.3 ? quantile(cliffs, 0.5) : sr / 2;
    if (activeCount < 16) return p;
    const double gridHz = step * binHz;
    std::vector<double> avg(static_cast<std::size_t>(G), 0.0);
    for (int t = 0; t < T; ++t) {
        if (!p.active[static_cast<std::size_t>(t)]) continue;
        const float* c = &coarse[static_cast<std::size_t>(t) * static_cast<std::size_t>(G)];
        for (int g = 0; g < G; ++g) avg[static_cast<std::size_t>(g)] += c[g];
    }
    for (double& v : avg) v /= static_cast<double>(activeCount);
    const int wide = std::max(2, static_cast<int>(std::lround(600.0 / gridHz))), near = std::max(1, static_cast<int>(std::lround(100.0 / gridHz)));
    std::vector<double> cs(static_cast<std::size_t>(G) + 1, 0.0);
    for (int g = 0; g < G; ++g) cs[static_cast<std::size_t>(g) + 1] = cs[static_cast<std::size_t>(g)] + avg[static_cast<std::size_t>(g)];
    auto mean = [&](int a, int e) { return (cs[static_cast<std::size_t>(e)] - cs[static_cast<std::size_t>(a)]) / std::max(1, e - a); };
    const double top = *std::max_element(avg.begin(), avg.end());
    double bestDrop = 0, bestBelow = 0;
    int bestG = -1;
    for (int g = std::max(wide, static_cast<int>(std::lround(3000.0 / gridHz))); g + wide < G; ++g) {
        const double below = mean(g - wide, g - near), above = mean(g + near + 1, g + wide + 1);
        if (below - above > bestDrop && above < top - 50.0) {
            bestDrop = below - above;
            bestBelow = below;
            bestG = g;
        }
    }
    if (bestG < 0 || bestDrop < 18.0) return p;
    int edge = bestG;
    for (int g = std::min(G - 1, bestG + wide); g >= bestG - wide; --g)
        if (avg[static_cast<std::size_t>(g)] >= bestBelow - 0.5 * bestDrop) { edge = g; break; }
    const double typ = (edge + 0.5) * gridHz;
    const int lo0 = static_cast<int>(std::lround((typ - 700.0) / gridHz)), lo1 = static_cast<int>(std::lround((typ - 150.0) / gridHz));
    const int hi0 = static_cast<int>(std::lround((typ + 150.0) / gridHz)), hi1 = static_cast<int>(std::lround((typ + 700.0) / gridHz));
    if (lo0 < 1 || hi1 >= G - 1) return p;
    long long evidence = 0, wall = 0;
    std::vector<char> fits(static_cast<std::size_t>(T), 0);
    for (int t = 0; t < T; ++t) {
        if (!p.active[static_cast<std::size_t>(t)]) continue;
        const float* c = &coarse[static_cast<std::size_t>(t) * static_cast<std::size_t>(G)];
        double below = 0, above = 0;
        for (int g = lo0; g <= lo1; ++g) below += c[g];
        for (int g = hi0; g <= hi1; ++g) above += c[g];
        below /= lo1 - lo0 + 1;
        above /= hi1 - hi0 + 1;
        if (below < -110.0) continue;
        ++evidence;
        if (below - above >= 15.0) {
            ++wall;
            fits[static_cast<std::size_t>(t)] = 1;
        }
    }
    if (evidence < std::max<long long>(8, activeCount / 20)) return p;
    const double share = static_cast<double>(wall) / static_cast<double>(evidence);
    if (share <= p.cliffShare) return p;
    p.cliffShare = share;
    p.evidenceShare = static_cast<double>(evidence) / static_cast<double>(activeCount);
    p.typicalHz = share >= 0.3 ? typ : sr / 2;
    if (share >= 0.3)
        for (int t = 0; t < T; ++t) {
            double& c = p.cutoffHz[static_cast<std::size_t>(t)];
            if (fits[static_cast<std::size_t>(t)]) c = typ;
            else if (share >= 0.5 && c > typ + 500.0) c = -1.0;
        }
    return p;
}

long long estimateLag(const Profile& a, const Profile& b, double& score) {
    auto prepared = [](const Profile& p) {
        const double loud = quantile(p.levelDb, 0.9);
        std::vector<double> v(p.levelDb.size());
        double m = 0;
        for (std::size_t i = 0; i < v.size(); ++i) {
            v[i] = std::max(p.levelDb[i], loud - 60.0);
            m += v[i];
        }
        m /= std::max<std::size_t>(1, v.size());
        for (double& x : v) x -= m;
        return v;
    };
    const std::vector<double> x = prepared(a), y = prepared(b);
    const long long Ta = static_cast<long long>(x.size()), Tb = static_cast<long long>(y.size());
    const long long maxLag = std::max<long long>(1, static_cast<long long>(30.0 * a.sr / std::max(1, a.hop)));
    std::vector<double> corr(static_cast<std::size_t>(2 * maxLag + 1), -2.0);
    parallelFor(2 * maxLag + 1, 16, [&](long long b0, long long e0) {
        for (long long li = b0; li < e0; ++li) {
            const long long lag = li - maxLag;
            double sxy = 0, sxx = 0, syy = 0;
            long long count = 0;
            for (long long u = std::max<long long>(0, -lag); u < Tb && u + lag < Ta; ++u) {
                const double xv = x[static_cast<std::size_t>(u + lag)], yv = y[static_cast<std::size_t>(u)];
                sxy += xv * yv;
                sxx += xv * xv;
                syy += yv * yv;
                ++count;
            }
            if (count * 4 >= std::min(Ta, Tb)) corr[static_cast<std::size_t>(li)] = sxy / std::sqrt(std::max(sxx * syy, 1e-30));
        }
    });
    const std::size_t best = static_cast<std::size_t>(std::max_element(corr.begin(), corr.end()) - corr.begin());
    score = corr[best];
    if (score < 0.3) {
        score = std::max(score, 0.0);
        return 0;
    }
    return static_cast<long long>(best) - maxLag;
}

Report decide(const Profile& o, const Profile& i, const Source& so, const Source& si) {
    Report r;
    r.originalShare = o.cliffShare;
    r.instrumentalShare = i.cliffShare;
    const bool origLimited = o.cliffShare >= 0.3;
    const bool instLimited = i.cliffShare >= 0.5;
    r.originalHz = origLimited ? o.typicalHz : o.sr / 2;
    r.instrumentalHz = instLimited ? i.typicalHz : i.sr / 2;
    const bool origCodecWorse = so.lossy && (!si.lossy || (so.kbps > 0 && si.kbps > 0 && so.kbps < si.kbps));
    const bool instCodecWorse = si.lossy && (!so.lossy || (so.kbps > 0 && si.kbps > 0 && si.kbps < so.kbps * 0.8));
    if ((origLimited && r.originalHz < r.instrumentalHz - 200.0) || (origCodecWorse && r.originalHz <= r.instrumentalHz + 600.0))
        r.worse = Worse::Original;
    else if ((instLimited && r.instrumentalHz < r.originalHz - 600.0) || (instCodecWorse && r.instrumentalHz <= r.originalHz + 200.0))
        r.worse = Worse::Instrumental;
    return r;
}

std::vector<double> curveFor(const Profile& worse, const Profile& better, Worse which, long long lag) {
    const bool origWorse = which == Worse::Original;
    const std::vector<double> wc = envelope(worse, origWorse ? 1.0 : 2.5);
    const long long Tw = static_cast<long long>(wc.size());
    std::vector<double> curve(better.cutoffHz.size());
    for (std::size_t t = 0; t < curve.size(); ++t) {
        const long long idx = origWorse ? static_cast<long long>(t) + lag : static_cast<long long>(t) - lag;
        curve[t] = wc[static_cast<std::size_t>(std::clamp<long long>(idx, 0, Tw - 1))];
    }
    return curve;
}

std::vector<double> envelope(const Profile& p, double windowSeconds) {
    const int T = static_cast<int>(p.cutoffHz.size());
    const double nyq = p.sr / 2;
    std::vector<double> c(static_cast<std::size_t>(T), -1.0);
    for (int t = 0; t < T; ++t)
        if (p.active[static_cast<std::size_t>(t)] && p.cutoffHz[static_cast<std::size_t>(t)] > 0) c[static_cast<std::size_t>(t)] = p.cutoffHz[static_cast<std::size_t>(t)];
    int first = -1;
    for (int t = 0; t < T; ++t)
        if (c[static_cast<std::size_t>(t)] > 0) { first = t; break; }
    if (first < 0) return std::vector<double>(static_cast<std::size_t>(T), nyq);
    for (int t = 0; t < first; ++t) c[static_cast<std::size_t>(t)] = c[static_cast<std::size_t>(first)];
    for (int t = first + 1; t < T; ++t)
        if (c[static_cast<std::size_t>(t)] <= 0) c[static_cast<std::size_t>(t)] = c[static_cast<std::size_t>(t - 1)];
    const double fps = p.sr / p.hop;
    const int half = std::max(1, static_cast<int>(std::lround(windowSeconds * fps / 2)));
    std::vector<double> mx(static_cast<std::size_t>(T));
    for (int t = 0; t < T; ++t) {
        double m = 0;
        for (int d = std::max(0, t - half); d <= std::min(T - 1, t + half); ++d) m = std::max(m, c[static_cast<std::size_t>(d)]);
        mx[static_cast<std::size_t>(t)] = m;
    }
    const int sm = std::max(1, static_cast<int>(std::lround(0.25 * fps)));
    std::vector<double> cs(static_cast<std::size_t>(T) + 1, 0.0);
    for (int t = 0; t < T; ++t) cs[static_cast<std::size_t>(t) + 1] = cs[static_cast<std::size_t>(t)] + mx[static_cast<std::size_t>(t)];
    std::vector<double> out(static_cast<std::size_t>(T));
    for (int t = 0; t < T; ++t) {
        const int a = std::max(0, t - sm), e = std::min(T, t + sm + 1);
        out[static_cast<std::size_t>(t)] = std::clamp((cs[static_cast<std::size_t>(e)] - cs[static_cast<std::size_t>(a)]) / (e - a), 2000.0, nyq);
    }
    return out;
}

double lowpassToCurve(std::vector<float>& x, int channels, double sr, const std::vector<double>& curve, int n, int hop) {
    const double nyq = sr / 2, binHz = sr / n, tw = 200.0;
    const long long len = static_cast<long long>(x.size() / static_cast<std::size_t>(std::max(1, channels)));
    const int T = static_cast<int>(curve.size()), half = n / 2, K = n / 2 + 1;
    bool any = false;
    for (double c : curve) any = any || c < nyq - binHz;
    if (!any) return -300.0;
    const std::vector<double> w = hann(n);
    std::vector<double> norm(static_cast<std::size_t>(len), 0.0);
    for (int t = 0; t < T; ++t)
        for (int j = 0; j < n; ++j) {
            const long long i = static_cast<long long>(t) * hop - half + j;
            if (i >= 0 && i < len) norm[static_cast<std::size_t>(i)] += w[static_cast<std::size_t>(j)] * w[static_cast<std::size_t>(j)];
        }
    std::vector<double> removed(x.size(), 0.0);
    std::mutex lock;
    FftTables tables(static_cast<std::size_t>(n));
    constexpr int kBlock = 64;
    const int blocks = (T + kBlock - 1) / kBlock;
    parallelFor(blocks, 1, [&](long long b0, long long e0) {
        std::vector<float> buf(static_cast<std::size_t>(2 * n));
        std::vector<double> gain(static_cast<std::size_t>(K));
        for (long long bi = b0; bi < e0; ++bi) {
            const int t0 = static_cast<int>(bi) * kBlock, t1 = std::min(T, t0 + kBlock);
            const long long base = static_cast<long long>(t0) * hop - half;
            const std::size_t span = static_cast<std::size_t>(t1 - t0 - 1) * static_cast<std::size_t>(hop) + static_cast<std::size_t>(n);
            std::vector<double> local(span * static_cast<std::size_t>(channels), 0.0);
            bool touched = false;
            for (int t = t0; t < t1; ++t) {
                const double c = curve[static_cast<std::size_t>(t)];
                if (c >= nyq - binHz) continue;
                touched = true;
                for (int k = 0; k < K; ++k) {
                    const double f = k * binHz;
                    double g = 1.0;
                    if (f >= c) g = 0.0;
                    else if (f > c - tw) g = 0.5 + 0.5 * std::cos(kPi * (f - (c - tw)) / tw);
                    gain[static_cast<std::size_t>(k)] = 1.0 - g;
                }
                for (int ch = 0; ch < channels; ++ch) {
                    for (int j = 0; j < n; ++j) {
                        const long long i = static_cast<long long>(t) * hop - half + j;
                        const float v = (i >= 0 && i < len) ? x[static_cast<std::size_t>(i) * static_cast<std::size_t>(channels) + static_cast<std::size_t>(ch)] : 0.0f;
                        buf[static_cast<std::size_t>(2 * j)] = static_cast<float>(v * w[static_cast<std::size_t>(j)]);
                        buf[static_cast<std::size_t>(2 * j + 1)] = 0.0f;
                    }
                    fft_forward(buf.data(), static_cast<std::size_t>(n), tables);
                    for (int k = 0; k < K; ++k) {
                        const float g = static_cast<float>(gain[static_cast<std::size_t>(k)]);
                        buf[static_cast<std::size_t>(2 * k)] *= g;
                        buf[static_cast<std::size_t>(2 * k + 1)] *= g;
                        if (k > 0 && k < n / 2) {
                            buf[static_cast<std::size_t>(2 * (n - k))] *= g;
                            buf[static_cast<std::size_t>(2 * (n - k) + 1)] *= g;
                        }
                    }
                    fft_inverse(buf.data(), static_cast<std::size_t>(n), tables);
                    const std::size_t o = static_cast<std::size_t>(t - t0) * static_cast<std::size_t>(hop);
                    for (int j = 0; j < n; ++j)
                        local[(o + static_cast<std::size_t>(j)) * static_cast<std::size_t>(channels) + static_cast<std::size_t>(ch)] +=
                            buf[static_cast<std::size_t>(2 * j)] * w[static_cast<std::size_t>(j)];
                }
            }
            if (!touched) continue;
            std::lock_guard<std::mutex> g(lock);
            for (std::size_t k = 0; k < span; ++k) {
                const long long i = base + static_cast<long long>(k);
                if (i < 0 || i >= len) continue;
                for (int ch = 0; ch < channels; ++ch)
                    removed[static_cast<std::size_t>(i) * static_cast<std::size_t>(channels) + static_cast<std::size_t>(ch)] +=
                        local[k * static_cast<std::size_t>(channels) + static_cast<std::size_t>(ch)];
            }
        }
    });
    double er = 0, ex = 0;
    for (long long i = 0; i < len; ++i) {
        const double nr = norm[static_cast<std::size_t>(i)];
        for (int ch = 0; ch < channels; ++ch) {
            const std::size_t idx = static_cast<std::size_t>(i) * static_cast<std::size_t>(channels) + static_cast<std::size_t>(ch);
            const double r = nr > 1e-10 ? removed[idx] / nr : 0.0;
            ex += static_cast<double>(x[idx]) * x[idx];
            er += r * r;
            x[idx] = static_cast<float>(x[idx] - r);
        }
    }
    return 10.0 * std::log10((er + 1e-30) / (ex + 1e-30));
}

Report match(std::vector<float>& original, std::vector<float>& instrumental, int channels, double sr) {
    const Profile po = analyse(original, channels, sr), pi = analyse(instrumental, channels, sr);
    Report r = decide(po, pi);
    const long long lag = estimateLag(po, pi, r.lagScore);
    r.lagSeconds = static_cast<double>(lag) * po.hop / sr;
    if (r.worse == Worse::None) return r;
    const bool origWorse = r.worse == Worse::Original;
    const Profile& worse = origWorse ? po : pi;
    const Profile& better = origWorse ? pi : po;
    const std::vector<double> curve = curveFor(worse, better, r.worse, lag);
    r.curveMinHz = *std::min_element(curve.begin(), curve.end());
    r.curveMaxHz = *std::max_element(curve.begin(), curve.end());
    r.removedDb = lowpassToCurve(origWorse ? instrumental : original, channels, sr, curve, better.nFft, better.hop);
    return r;
}

}
}
