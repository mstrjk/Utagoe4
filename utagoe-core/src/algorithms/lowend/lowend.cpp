#include <atomic>
#include "log.h"
#include "cancel.h"
#include "lowend.h"
#include "mathconst.h"
#include "../refcancel/rc.h"
#include "parallel.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace utagoe {
namespace lowend {
namespace {

using cd = std::complex<double>;

std::size_t pow2At(std::size_t n) {
    std::size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

std::vector<double> monoOf(const std::vector<float>& x, int channels) {
    const std::size_t n = x.size() / static_cast<std::size_t>(channels);
    std::vector<double> m(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        double s = 0;
        for (int c = 0; c < channels; ++c) s += x[i * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)];
        m[i] = s / channels;
    }
    return m;
}

std::vector<double> crossCorrelate(const std::vector<double>& a, const std::vector<double>& b, std::size_t n) {
    std::vector<cd> fa(n, 0.0), fb(n, 0.0);
    for (std::size_t i = 0; i < a.size() && i < n; ++i) fa[i] = a[i];
    for (std::size_t i = 0; i < b.size() && i < n; ++i) fb[i] = b[i];
    rc::fftDoubleMany({&fa, &fb}, false);
    for (std::size_t i = 0; i < n; ++i) fa[i] = std::conj(fa[i]) * fb[i];
    rc::fftDoubleMany({&fa}, true);
    std::vector<double> c(n);
    for (std::size_t i = 0; i < n; ++i) c[i] = fa[i].real();
    return c;
}

std::vector<double> firFrom(const std::vector<cd>& G, int N, int lead = -1) {
    if (lead < 0) lead = N / 2;
    std::vector<cd> spec(static_cast<std::size_t>(N));
    for (int q = 0; q <= N / 2; ++q) {
        spec[static_cast<std::size_t>(q)] = G[static_cast<std::size_t>(q)];
        if (q > 0 && q < N / 2) spec[static_cast<std::size_t>(N - q)] = std::conj(G[static_cast<std::size_t>(q)]);
    }
    spec[static_cast<std::size_t>(N / 2)] = cd(spec[static_cast<std::size_t>(N / 2)].real(), 0.0);
    spec[0] = cd(spec[0].real(), 0.0);
    rc::fftDouble(spec, true);
    std::vector<double> h(static_cast<std::size_t>(N));
    for (int j = 0; j < N; ++j) {
        const int src = ((j - lead) % N + N) % N;
        double w;
        if (lead == N / 2) {
            w = 0.42 - 0.5 * std::cos(2.0 * kPi * j / (N - 1)) + 0.08 * std::cos(4.0 * kPi * j / (N - 1));
        } else if (j < lead) {
            w = 0.5 - 0.5 * std::cos(kPi * j / lead);
        } else {
            const double t = static_cast<double>(j - lead) / static_cast<double>(N - lead);
            w = 0.42 + 0.5 * std::cos(kPi * t) + 0.08 * std::cos(2.0 * kPi * t);
        }
        h[static_cast<std::size_t>(j)] = spec[static_cast<std::size_t>(src)].real() * w;
    }
    return h;
}

std::vector<float> filterInterleaved(const std::vector<float>& x, int channels, const std::vector<double>& h, int N, int lead = -1) {
    if (lead < 0) lead = N / 2;
    const std::size_t n = x.size() / static_cast<std::size_t>(channels);
    const std::size_t L = 1 << 17, B = L - static_cast<std::size_t>(N) + 1;
    std::vector<cd> hf(L, 0.0);
    for (int j = 0; j < N; ++j) hf[static_cast<std::size_t>(j)] = h[static_cast<std::size_t>(j)];
    rc::fftDouble(hf, false);
    std::vector<float> y(x.size(), 0.0f);
    const std::size_t blocks = (n + B - 1) / B;
    const std::size_t wave = static_cast<std::size_t>(std::max(1, workerCount()));
    std::vector<std::vector<cd>> bufs(std::min(wave, std::max<std::size_t>(1, blocks)), std::vector<cd>(L));
    for (int pair = 0; pair < channels; pair += 2) {
        const int c0 = pair, c1 = std::min(pair + 1, channels - 1);
        const bool two = c1 != c0;
        std::vector<double> acc0(n + L, 0.0), acc1(two ? n + L : 0, 0.0);
        for (std::size_t first = 0; first < blocks; first += bufs.size()) {
            if (cancel::requested()) return y;
            const std::size_t count = std::min(bufs.size(), blocks - first);
            parallelFor(static_cast<long long>(count), 1, [&](long long b, long long e) {
                for (long long w = b; w < e; ++w) {
                    if (cancel::requested()) return;
                    std::vector<cd>& buf = bufs[static_cast<std::size_t>(w)];
                    const std::size_t s = (first + static_cast<std::size_t>(w)) * B;
                    std::fill(buf.begin(), buf.end(), cd(0, 0));
                    for (std::size_t j = 0; j < B && s + j < n; ++j) {
                        const std::size_t idx = (s + j) * static_cast<std::size_t>(channels);
                        buf[j] = cd(x[idx + static_cast<std::size_t>(c0)], two ? x[idx + static_cast<std::size_t>(c1)] : 0.0f);
                    }
                    rc::fftDouble(buf, false);
                    for (std::size_t j = 0; j < L; ++j) buf[j] *= hf[j];
                    rc::fftDouble(buf, true);
                }
            });
            for (std::size_t w = 0; w < count; ++w) {
                const std::vector<cd>& buf = bufs[w];
                const std::size_t s = (first + w) * B;
                for (std::size_t j = 0; j < L; ++j) {
                    acc0[s + j] += buf[j].real();
                    if (two) acc1[s + j] += buf[j].imag();
                }
            }
        }
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t k = i + static_cast<std::size_t>(lead);
            y[i * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c0)] = static_cast<float>(acc0[k]);
            if (two) y[i * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c1)] = static_cast<float>(acc1[k]);
        }
    }
    return y;
}

void correlatePair(const std::vector<double>& a, const std::vector<double>& b, std::size_t n, int sr, double lo, double hi,
                   std::vector<double>& plain, std::vector<double>& band) {
    std::vector<cd> fa(n, 0.0), fb(n, 0.0);
    for (std::size_t i = 0; i < a.size() && i < n; ++i) fa[i] = a[i];
    for (std::size_t i = 0; i < b.size() && i < n; ++i) fb[i] = b[i];
    rc::fftDouble(fa, false);
    rc::fftDouble(fb, false);
    std::vector<cd> p(n), q(n);
    for (std::size_t i = 0; i < n; ++i) {
        const cd x = std::conj(fa[i]) * fb[i];
        p[i] = x;
        const double f = static_cast<double>(std::min(i, n - i)) * sr / static_cast<double>(n);
        const double m = std::abs(x);
        q[i] = (f >= lo && f <= hi && m > 1e-30) ? x : cd(0, 0);
    }
    rc::fftDouble(p, true);
    rc::fftDouble(q, true);
    plain.resize(n);
    band.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        plain[i] = p[i].real();
        band[i] = q[i].real();
    }
}

long long coarseLag(const std::vector<double>& mix, const std::vector<double>& inst, int sr) {
    const int dec = 8;
    auto down = [&](const std::vector<double>& x) {
        std::vector<double> d(x.size() / dec);
        for (std::size_t i = 0; i < d.size(); ++i) {
            double s = 0;
            for (int k = 0; k < dec; ++k) s += x[i * dec + static_cast<std::size_t>(k)];
            d[i] = s / dec;
        }
        return d;
    };
    const std::vector<double> m = down(mix), s = down(inst);
    const std::size_t n = pow2At(m.size() + s.size());
    const std::vector<double> c = crossCorrelate(m, s, n);
    const long long maxLag = static_cast<long long>(10.0 * sr / dec);
    long long best = 0;
    double bestV = -1;
    for (long long k = -maxLag; k <= maxLag; ++k) {
        const double v = std::abs(c[static_cast<std::size_t>((k % static_cast<long long>(n) + static_cast<long long>(n)) % static_cast<long long>(n))]);
        if (v > bestV) {
            bestV = v;
            best = k;
        }
    }
    return -best * dec;
}

}

void subsonicCut(std::vector<float>& x, int channels, int sr) {
    const int N = 1 << 16;
    if (x.size() / static_cast<std::size_t>(channels) < static_cast<std::size_t>(N)) return;
    std::vector<cd> G(static_cast<std::size_t>(N / 2) + 1);
    for (int q = 0; q <= N / 2; ++q) {
        const double f = static_cast<double>(q) * sr / N;
        const double t = std::clamp((f - 12.0) / 10.0, 0.0, 1.0);
        G[static_cast<std::size_t>(q)] = 0.5 - 0.5 * std::cos(kPi * t);
    }
    x = filterInterleaved(x, channels, firFrom(G, N), N);
}

Report match(const std::vector<float>& mixIn, const std::vector<float>& instIn, int channels, int sr, std::vector<float>& out,
             const std::function<bool(double)>& progress) {
    Report rep;
    out.clear();
    auto tick = [&](double f) {
        if (cancel::requested() || (progress && !progress(f))) {
            rep.cancelled = true;
            rep.reason = "cancelled";
            return false;
        }
        return true;
    };
    const std::size_t nm = mixIn.size() / static_cast<std::size_t>(channels), ni = instIn.size() / static_cast<std::size_t>(channels);
    const std::size_t seg = static_cast<std::size_t>(8 * sr), R = 4096;
    const int N = 16384;
    if (nm < 3 * seg || ni < 3 * seg) {
        rep.reason = "too short";
        return rep;
    }
    if (!tick(0.0)) return rep;
    const std::vector<double> mm = monoOf(mixIn, channels), im = monoOf(instIn, channels);
    if (!tick(0.01)) return rep;
    const long long L0 = coarseLag(mm, im, sr);
    if (!tick(0.05)) return rep;

    const int K = static_cast<int>(std::min<double>(N / 2, std::ceil(2000.0 * N / sr)));
    std::vector<cd> sio(static_cast<std::size_t>(K) + 1, 0.0);
    std::vector<double> sii(static_cast<std::size_t>(K) + 1, 0.0), soo(static_cast<std::size_t>(K) + 1, 0.0);
    std::vector<double> win(static_cast<std::size_t>(N));
    for (int j = 0; j < N; ++j) win[static_cast<std::size_t>(j)] = 0.5 - 0.5 * std::cos(2.0 * kPi * j / N);
    std::vector<double> lagT, lagV;

    const std::size_t xn = pow2At(seg + seg + 2 * R);
    struct Segment {
        bool used = false;
        double lag = 0, lagFrac = 0;
        std::vector<cd> bins;
    };
    std::vector<std::size_t> starts;
    for (std::size_t s = seg / 2; s + seg + R < nm; s += seg) starts.push_back(s);
    const std::size_t K1 = static_cast<std::size_t>(K) + 1;
    auto measure = [&](std::size_t s, Segment& out) {
        const long long w0 = static_cast<long long>(s) - L0 - static_cast<long long>(R);
        if (w0 < 0 || static_cast<std::size_t>(w0) + seg + 2 * R >= ni) return;
        std::vector<double> a(mm.begin() + static_cast<std::ptrdiff_t>(s), mm.begin() + static_cast<std::ptrdiff_t>(s + seg));
        std::vector<double> b(im.begin() + w0, im.begin() + w0 + static_cast<std::ptrdiff_t>(seg + 2 * R));
        double ea = 0, eb = 0;
        for (double v : a) ea += v * v;
        for (std::size_t i = R; i < R + seg; ++i) eb += b[i] * b[i];
        if (ea <= 1e-12 || eb <= 1e-12) return;
        std::vector<double> plain, c;
        correlatePair(a, b, xn, sr, 150.0, 8000.0, plain, c);
        std::size_t k = 1;
        for (std::size_t q = 1; q < 2 * R; ++q)
            if (c[q] > c[k]) k = q;
        double score = 0;
        for (std::size_t q = k - 1; q <= k + 1; ++q) score = std::max(score, plain[q]);
        score /= std::sqrt(ea * eb);
        if (score < 0.3) return;
        const double y0 = c[k - 1], y1 = c[k], y2 = c[k + 1];
        const double den = y0 - 2 * y1 + y2;
        const double frac = std::abs(den) > 1e-30 ? std::clamp(0.5 * (y0 - y2) / den, -0.5, 0.5) : 0.0;
        out.used = true;
        out.lag = static_cast<double>(L0) + static_cast<double>(R) - (static_cast<double>(k) + frac);
        const long long lagInt = static_cast<long long>(std::floor(out.lag));
        out.lagFrac = out.lag - static_cast<double>(lagInt);
        std::vector<cd> fm(static_cast<std::size_t>(N)), fi(static_cast<std::size_t>(N));
        for (std::size_t j0 = s; j0 + static_cast<std::size_t>(N) <= s + seg; j0 += static_cast<std::size_t>(N / 2)) {
            if (cancel::requested()) return;
            const long long i0 = static_cast<long long>(j0) - lagInt;
            if (i0 < 0 || static_cast<std::size_t>(i0) + static_cast<std::size_t>(N) > ni) continue;
            for (int ch = 0; ch < channels; ++ch) {
                for (int j = 0; j < N; ++j) {
                    fm[static_cast<std::size_t>(j)] = win[static_cast<std::size_t>(j)] * mixIn[(j0 + static_cast<std::size_t>(j)) * static_cast<std::size_t>(channels) + static_cast<std::size_t>(ch)];
                    fi[static_cast<std::size_t>(j)] = win[static_cast<std::size_t>(j)] * instIn[(static_cast<std::size_t>(i0) + static_cast<std::size_t>(j)) * static_cast<std::size_t>(channels) + static_cast<std::size_t>(ch)];
                }
                rc::fftDouble(fm, false);
                rc::fftDouble(fi, false);
                out.bins.insert(out.bins.end(), fm.begin(), fm.begin() + static_cast<std::ptrdiff_t>(K1));
                out.bins.insert(out.bins.end(), fi.begin(), fi.begin() + static_cast<std::ptrdiff_t>(K1));
            }
        }
    };
    const std::size_t wave = static_cast<std::size_t>(std::max(1, workerCount())) * 4;
    log::Bar* bar = log::Bar::current();
    std::atomic<std::size_t> measured{0};
    for (std::size_t first = 0; first < starts.size(); first += wave) {
        if (!tick(0.05 + 0.5 * static_cast<double>(starts[first]) / static_cast<double>(nm))) return rep;
        const std::size_t count = std::min(wave, starts.size() - first);
        std::vector<Segment> segs(count);
        parallelFor(static_cast<long long>(count), 1, [&](long long b, long long e) {
            for (long long i = b; i < e; ++i) {
                if (cancel::requested()) return;
                measure(starts[first + static_cast<std::size_t>(i)], segs[static_cast<std::size_t>(i)]);
                if (bar) bar->update(0.05 + 0.5 * static_cast<double>(++measured) / static_cast<double>(starts.size()), "matching segments");
            }
        });
        for (std::size_t i = 0; i < count; ++i) {
            const Segment& g = segs[i];
            if (!g.used) continue;
            lagT.push_back(static_cast<double>(starts[first + i] + seg / 2));
            lagV.push_back(g.lag);
            ++rep.segments;
            for (std::size_t off = 0; off < g.bins.size(); off += 2 * K1) {
                const cd* fm = g.bins.data() + off;
                const cd* fi = fm + K1;
                for (int q = 0; q <= K; ++q) {
                    const double w = 2.0 * kPi * q / N;
                    const cd M = fm[q], I = fi[q];
                    sio[static_cast<std::size_t>(q)] += M * std::conj(I) * std::polar(1.0, w * g.lagFrac);
                    sii[static_cast<std::size_t>(q)] += std::norm(I);
                    soo[static_cast<std::size_t>(q)] += std::norm(M);
                }
            }
        }
    }
    if (!tick(0.56)) return rep;
    if (rep.segments < 4) {
        rep.reason = "the pair could not be lined up reliably";
        return rep;
    }
    {
        const double n = static_cast<double>(lagT.size());
        double mt = 0, mv = 0;
        for (std::size_t i = 0; i < lagT.size(); ++i) {
            mt += lagT[i] / n;
            mv += lagV[i] / n;
        }
        double num = 0, den = 0;
        for (std::size_t i = 0; i < lagT.size(); ++i) {
            num += (lagT[i] - mt) * (lagV[i] - mv);
            den += (lagT[i] - mt) * (lagT[i] - mt);
        }
        rep.driftPpm = den > 0 ? num / den * 1e6 : 0.0;
        rep.lagSamples = mv;
    }

    auto hz = [&](int q) { return static_cast<double>(q) * sr / N; };
    std::vector<cd> H(static_cast<std::size_t>(K) + 1);
    for (int q = 0; q <= K; ++q) H[static_cast<std::size_t>(q)] = sio[static_cast<std::size_t>(q)] / std::max(sii[static_cast<std::size_t>(q)], 1e-30);
    cd ref(0, 0);
    double lowE = 0, midE = 0, coh = 0, cohW = 0, refAbs = 0;
    for (int q = 1; q <= K; ++q) {
        const double f = hz(q);
        if (f >= 400 && f <= 2000) {
            ref += sio[static_cast<std::size_t>(q)];
            refAbs += std::abs(sio[static_cast<std::size_t>(q)]);
            midE += sii[static_cast<std::size_t>(q)];
        }
        if (f >= 40 && f <= 200) {
            lowE += sii[static_cast<std::size_t>(q)];
            coh += std::norm(sio[static_cast<std::size_t>(q)]) / std::max(sii[static_cast<std::size_t>(q)] * soo[static_cast<std::size_t>(q)], 1e-30) * sii[static_cast<std::size_t>(q)];
            cohW += sii[static_cast<std::size_t>(q)];
        }
    }
    if (midE <= 0 || std::abs(ref) <= 0) {
        rep.reason = "no usable mid band";
        return rep;
    }
    if (refAbs <= 0 || std::abs(ref) < 0.5 * refAbs || midE < 1e-3 * lowE) {
        rep.reason = "the mid band is too thin to use as a reference";
        return rep;
    }
    cd wideSio(0, 0);
    double wideSii = 0;
    for (int q = 1; q <= K; ++q) {
        if (hz(q) < 25) continue;
        wideSio += sio[static_cast<std::size_t>(q)];
        wideSii += sii[static_cast<std::size_t>(q)];
    }
    const cd refGain = std::polar(wideSii > 0 ? std::abs(wideSio) / wideSii : std::abs(ref) / midE, std::arg(ref));
    if (lowE < 1e-4 * midE) {
        rep.reason = "the instrumental has almost no low end";
        return rep;
    }
    if (cohW <= 0 || coh / cohW < 0.5) {
        rep.reason = "the low end of the two files does not match well enough to measure";
        return rep;
    }
    for (cd& h : H) h /= refGain;

    double sn = 0, sd = 0;
    for (int q = 1; q <= K; ++q) {
        const double f = hz(q);
        if (f < 300 || f > 2000) continue;
        const double w = 2.0 * kPi * q / N, wt = sii[static_cast<std::size_t>(q)];
        sn += wt * w * std::arg(H[static_cast<std::size_t>(q)]);
        sd += wt * w * w;
    }
    const double slope = sd > 0 ? sn / sd : 0.0;
    for (int q = 0; q <= K; ++q) H[static_cast<std::size_t>(q)] *= std::polar(1.0, -slope * 2.0 * kPi * q / N);

    const int halfBox = 3;
    std::vector<cd> G(static_cast<std::size_t>(N / 2) + 1, cd(1.0, 0.0));
    const int q15 = static_cast<int>(std::ceil(15.0 * N / sr));
    for (int q = 0; q <= K; ++q) {
        const double f = hz(q);
        cd sm(0, 0);
        int cnt = 0;
        for (int d = -halfBox; d <= halfBox; ++d) {
            const int qq = std::clamp(q + d, q15, K);
            sm += H[static_cast<std::size_t>(qq)];
            ++cnt;
        }
        sm /= static_cast<double>(cnt);
        double fade = std::clamp((f - 250.0) / 150.0, 0.0, 1.0);
        fade = 0.5 - 0.5 * std::cos(kPi * fade);
        G[static_cast<std::size_t>(q)] = (1.0 - fade) * sm + fade * cd(1.0, 0.0);
    }
    for (int q = 0; q < q15; ++q) G[static_cast<std::size_t>(q)] = G[static_cast<std::size_t>(q15)];

    auto at = [&](double f) { return G[static_cast<std::size_t>(std::lround(f * N / sr))]; };
    rep.phase30 = std::arg(at(30)) * 180 / kPi;
    rep.phase60 = std::arg(at(60)) * 180 / kPi;
    rep.phase100 = std::arg(at(100)) * 180 / kPi;
    rep.phase200 = std::arg(at(200)) * 180 / kPi;
    rep.gain30Db = 20 * std::log10(std::abs(at(30)));
    rep.gain60Db = 20 * std::log10(std::abs(at(60)));
    rep.gain100Db = 20 * std::log10(std::abs(at(100)));
    double leakE = 0, leakW = 0, worst = 0;
    for (int q = 1; q <= K; ++q) {
        const double f = hz(q);
        if (f < 20 || f > 250) continue;
        const double d = std::abs(G[static_cast<std::size_t>(q)] - 1.0);
        worst = std::max(worst, d);
        if (f <= 150) {
            leakE += d * d * sii[static_cast<std::size_t>(q)];
            leakW += sii[static_cast<std::size_t>(q)];
        }
    }
    rep.predictedLeakDb = leakW > 0 ? 10 * std::log10(leakE / leakW + 1e-30) : -300;
    const bool staticNeeded = worst >= 0.01;
    const int lead = N / 2;
    if (!tick(0.58)) return rep;
    out = staticNeeded ? filterInterleaved(instIn, channels, firFrom(G, N, lead), N, lead) : instIn;
    if (!tick(0.70)) {
        out.clear();
        return rep;
    }

    const double lagMid = rep.lagSamples, drift = rep.driftPpm * 1e-6;
    double tMid = 0;
    for (double t : lagT) tMid += t / static_cast<double>(lagT.size());
    auto lagAt = [&](double t) { return lagMid + drift * (t - tMid); };

    std::vector<cd> bandG(static_cast<std::size_t>(N / 2) + 1), lowG(static_cast<std::size_t>(N / 2) + 1);
    for (int q = 0; q <= N / 2; ++q) {
        const double f = hz(q);
        auto rise = [](double x, double a, double b) { const double t = std::clamp((x - a) / (b - a), 0.0, 1.0); return 0.5 - 0.5 * std::cos(kPi * t); };
        bandG[static_cast<std::size_t>(q)] = rise(f, 20.0, 30.0) * (1.0 - rise(f, 80.0, 100.0));
        lowG[static_cast<std::size_t>(q)] = 1.0 - rise(f, 180.0, 220.0);
    }
    const std::vector<double> bandH = firFrom(bandG, N), lowH = firFrom(lowG, N);
    const std::vector<float> eMix = filterInterleaved(mixIn, channels, bandH, N);
    const std::vector<float> eInst = filterInterleaved(out, channels, bandH, N);
    if (!tick(0.85)) {
        out.clear();
        return rep;
    }

    const std::size_t W = static_cast<std::size_t>(0.05 * sr), hop = W / 2;
    std::vector<double> hann(W);
    for (std::size_t j = 0; j < W; ++j) hann[j] = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(j) / static_cast<double>(W));
    struct Win { double centreInst, gain, energy; };
    std::vector<Win> wins;
    double sab = 0, saa = 0;
    for (std::size_t s = 0; s + W < nm; s += hop) {
        const double lag = lagAt(static_cast<double>(s + W / 2));
        const long long i0 = static_cast<long long>(s) - std::llround(lag);
        if (i0 < 0 || static_cast<std::size_t>(i0) + W >= ni) continue;
        double ab = 0, aa = 0;
        for (std::size_t j = 0; j < W; ++j)
            for (int c = 0; c < channels; ++c) {
                const double a = hann[j] * eInst[(static_cast<std::size_t>(i0) + j) * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)];
                const double b = hann[j] * eMix[(s + j) * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)];
                ab += a * b;
                aa += a * a;
            }
        sab += ab;
        saa += aa;
        wins.push_back({static_cast<double>(i0) + static_cast<double>(W) / 2, aa > 0 ? ab / aa : 0.0, aa});
    }
    bool rode = false;
    if (saa > 0 && wins.size() > 8) {
        const double g0 = sab / saa;
        std::vector<double> energies;
        for (const Win& w : wins) energies.push_back(w.energy);
        std::nth_element(energies.begin(), energies.begin() + static_cast<std::ptrdiff_t>(energies.size() / 2), energies.end());
        const double quiet = 1e-3 * energies[energies.size() / 2];
        std::vector<double> num(ni, 0.0), den(ni, 0.0);
        std::vector<double> rides;
        for (const Win& w : wins) {
            const double r = w.energy > quiet && g0 != 0 ? std::clamp(w.gain / g0, 0.7, 1.4) : 1.0;
            if (w.energy > quiet) rides.push_back(20 * std::log10(r));
            const long long c0 = std::llround(w.centreInst) - static_cast<long long>(W / 2);
            for (std::size_t j = 0; j < W; ++j) {
                const long long i = c0 + static_cast<long long>(j);
                if (i < 0 || static_cast<std::size_t>(i) >= ni) continue;
                num[static_cast<std::size_t>(i)] += hann[j] * r;
                den[static_cast<std::size_t>(i)] += hann[j];
            }
        }
        if (rides.size() > 8) {
            std::sort(rides.begin(), rides.end());
            rep.ride5Db = rides[rides.size() / 20];
            rep.ride95Db = rides[rides.size() * 19 / 20];
            const std::vector<float> lowPart = filterInterleaved(out, channels, lowH, N);
            if (!tick(0.95)) {
                out.clear();
                return rep;
            }
            for (std::size_t i = 0; i < ni; ++i) {
                const double g = den[i] > 1e-6 ? num[i] / den[i] : 1.0;
                for (int c = 0; c < channels; ++c) {
                    const std::size_t k = i * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c);
                    out[k] += static_cast<float>((g - 1.0) * lowPart[k]);
                }
            }
            rode = true;
        }
    }
    rep.rode = rode;
    if (progress) progress(1.0);
    rep.applied = staticNeeded || rode;
    if (rep.applied) {
        const float floor = 1e-4f;
        std::size_t first = ni, last = 0;
        for (std::size_t i = 0; i < ni; ++i)
            for (int c = 0; c < channels; ++c)
                if (std::abs(instIn[i * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)]) > floor) {
                    first = std::min(first, i);
                    last = i;
                }
        const std::size_t ramp = static_cast<std::size_t>(0.005 * sr);
        for (std::size_t i = 0; i < ni; ++i) {
            double w = 1.0;
            if (first >= ni || i < first || i > last) w = 0.0;
            else if (i - first < ramp) w = 0.5 - 0.5 * std::cos(kPi * static_cast<double>(i - first) / static_cast<double>(ramp));
            else if (last - i < ramp) w = 0.5 - 0.5 * std::cos(kPi * static_cast<double>(last - i) / static_cast<double>(ramp));
            if (w >= 1.0) continue;
            for (int c = 0; c < channels; ++c) {
                const std::size_t k = i * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c);
                out[k] = static_cast<float>(instIn[k] + w * (out[k] - instIn[k]));
            }
        }
    }
    if (!rep.applied) {
        rep.reason = "the low ends already match";
        out.clear();
    }
    return rep;
}

}
}
