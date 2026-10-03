// 較正、位置合わせ、全体の流れ (Python 版 calibration.py, alignment.py, pipeline.py)。
// 位置合わせの Huber 当てはめは scipy.optimize.least_squares の代わりに IRLS で解く (傾きの範囲制限つき)。
// 相互相関の FFT 長は 2 のべき乗にしている (Python 版は 5-smooth の長さ)。どちらも線形相関として同じ値になる。

#include "rc.h"
#include "parallel.h"
#include "log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace utagoe {
namespace rc {
namespace {

constexpr double kPi = 3.14159265358979323846;

Spec selectFrames(const Spec& z, const std::vector<char>& keep) {
    int n = 0;
    for (char k : keep) n += k ? 1 : 0;
    Spec out;
    out.resize(z.F, n, z.C);
    for (int f = 0; f < z.F; ++f) {
        int w = 0;
        for (int t = 0; t < z.T; ++t) {
            if (!keep[static_cast<std::size_t>(t)]) continue;
            for (int c = 0; c < z.C; ++c) out.at(f, w, c) = z.at(f, t, c);
            ++w;
        }
    }
    return out;
}

double meanErrorPower(const Spec& y, const Spec& b) {
    double s = 0.0;
    for (std::size_t i = 0; i < y.v.size(); ++i) {
        const cd d = cd(y.v[i]) - cd(b.v[i]);
        s += d.real() * d.real() + d.imag() * d.imag();
    }
    return s / std::max<std::size_t>(1, y.v.size());
}

std::size_t nextPow2(std::size_t n) {
    std::size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

std::vector<long long> calibrationCenters(std::size_t length, const std::vector<char>& valid, int sr, const Config& cfg) {
    const long long half = cfg.nFft / 2;
    std::vector<long long> bad(length + 1, 0);
    for (std::size_t i = 0; i < length; ++i) bad[i + 1] = bad[i] + (valid[i] ? 0 : 1);
    std::vector<long long> centers;
    for (long long c = half; c < static_cast<long long>(length) - half; c += cfg.hop) {
        if (bad[static_cast<std::size_t>(c + half)] - bad[static_cast<std::size_t>(c - half)] != 0) continue;
        if (!cfg.fitSpans.empty()) {
            bool keep = false;
            for (const auto& s : cfg.fitSpans)
                keep |= (c - half >= s.first * sr) && (c + half < s.second * sr);
            if (!keep) continue;
        }
        centers.push_back(c);
    }
    if (centers.size() < 12)
        throw std::runtime_error("fewer than 12 usable calibration windows; extend the vocal-free spans or use a longer matched pair");
    if (centers.size() > static_cast<std::size_t>(cfg.calibrationFrames)) {
        std::vector<long long> pick(static_cast<std::size_t>(cfg.calibrationFrames));
        const double last = static_cast<double>(centers.size() - 1);
        for (int i = 0; i < cfg.calibrationFrames; ++i) {
            const double pos = cfg.calibrationFrames > 1 ? last * i / (cfg.calibrationFrames - 1) : 0.0;
            pick[static_cast<std::size_t>(i)] = centers[static_cast<std::size_t>(pos)];
        }
        centers.swap(pick);
    }
    return centers;
}

}


Calibration calibrate(const Audio& mix, const Audio& reference, const std::vector<char>& valid,
                      int sr, const Config& cfg, bool nonlinear) {
    Calibration cal;
    const std::vector<long long> centers = calibrationCenters(mix.frames(), valid, sr, cfg);
    cal.frames = static_cast<int>(centers.size());
    const Spec X = stftAt(reference, centers, cfg.nFft);
    const Spec Y = stftAt(mix, centers, cfg.nFft);
    const int F = X.F, T = X.T, C = X.C;
    cal.linear = fitTransfer(X, Y, cfg);

    // 診断用の位相の揃い具合 (coherence の中央値)。
    {
        const Spec base = applyTransfer(X, cal.linear);
        std::vector<double> coh, energy(static_cast<std::size_t>(F), 0.0);
        double maxE = 0.0;
        for (int f = 0; f < F; ++f) {
            for (int t = 0; t < T; ++t)
                for (int c = 0; c < C; ++c) energy[static_cast<std::size_t>(f)] += std::norm(cd(X.at(f, t, c)));
            maxE = std::max(maxE, energy[static_cast<std::size_t>(f)]);
        }
        for (int f = 0; f < F; ++f) {
            if (energy[static_cast<std::size_t>(f)] <= std::max(maxE * 1e-7, 1e-18)) continue;
            for (int c = 0; c < C; ++c) {
                cd cross(0, 0);
                double py = 0, pb = 0;
                for (int t = 0; t < T; ++t) {
                    cross += cd(Y.at(f, t, c)) * std::conj(cd(base.at(f, t, c)));
                    py += std::norm(cd(Y.at(f, t, c)));
                    pb += std::norm(cd(base.at(f, t, c)));
                }
                coh.push_back(std::norm(cross / static_cast<double>(T)) / std::max(py / T * (pb / T), 1e-20));
            }
        }
        cal.medianCoherence = coh.empty() ? 0.0 : median(coh);
    }

    // 位相に頼らない level / EQ。複素回帰とは別に求める (位相が合わない組でも 0 に潰れないように)。
    {
        std::vector<float> px(X.v.size()), py(Y.v.size());
        for (std::size_t i = 0; i < X.v.size(); ++i) {
            px[i] = std::norm(X.v[i]);
            py[i] = std::norm(Y.v[i]);
        }
        gaussian(px.data(), F, T, C, 0, 3.0);
        gaussian(px.data(), F, T, C, 1, 2.0);
        gaussian(py.data(), F, T, C, 0, 3.0);
        gaussian(py.data(), F, T, C, 1, 2.0);
        const double q = cfg.fitSpans.empty() ? 0.25 : 0.5;
        std::vector<float> logRatio(static_cast<std::size_t>(F) * C);
        for (int f = 0; f < F; ++f)
            for (int c = 0; c < C; ++c) {
                float mx = 0;
                for (int t = 0; t < T; ++t) mx = std::max(mx, px[(static_cast<std::size_t>(f) * T + t) * C + c]);
                const double thr = std::max(static_cast<double>(mx) * 1e-4, 1e-16);
                std::vector<double> r(static_cast<std::size_t>(T));
                for (int t = 0; t < T; ++t) {
                    const double a = px[(static_cast<std::size_t>(f) * T + t) * C + c], b = py[(static_cast<std::size_t>(f) * T + t) * C + c];
                    r[static_cast<std::size_t>(t)] = a > thr ? b / std::max(a, 1e-20) : std::numeric_limits<double>::quiet_NaN();
                }
                double ratio = quantile(r, q);
                if (std::isnan(ratio)) ratio = 1.0;
                logRatio[static_cast<std::size_t>(f) * C + c] = static_cast<float>(std::log(std::clamp(ratio, 0.01, 64.0)));
            }
        gaussian(logRatio.data(), F, C, 1, 0, 2.0);
        cal.powerGain.resize(logRatio.size());
        for (std::size_t i = 0; i < logRatio.size(); ++i) cal.powerGain[i] = static_cast<float>(std::exp(0.5 * logRatio[i]));
    }

    if (!nonlinear) return cal;

    // Hammerstein: 参照波形から多項式の特徴を作る (STFT の前に波形で累乗する)。
    {
        const std::size_t n = reference.frames();
        const std::size_t m = std::min<std::size_t>(n, 200000);
        Audio sub;
        sub.channels = reference.channels;
        sub.v.resize(m * static_cast<std::size_t>(C));
        for (std::size_t i = 0; i < m; ++i) {
            const std::size_t idx = m > 1 ? static_cast<std::size_t>(static_cast<double>(n - 1) * i / (m - 1)) : 0;
            for (int c = 0; c < C; ++c) sub.at(i, c) = reference.at(idx, c);
        }
        cal.basis.fit(sub);
    }
    const Spec feat = stftAt(cal.basis.transform(reference), centers, cfg.nFft);
    std::vector<double> penalties(static_cast<std::size_t>(feat.C), 1.0);
    for (int d = C; d < feat.C; ++d) penalties[static_cast<std::size_t>(d)] = cfg.nonlinearRidge / cfg.ridge;
    auto priorFrom = [&](const Transfer& h) {
        Transfer p;
        p.resize(F, feat.C, C);
        for (int f = 0; f < F; ++f)
            for (int d = 0; d < C; ++d)
                for (int o = 0; o < C; ++o) p.at(f, d, o) = h.at(f, d, o);
        return p;
    };

    // 交互の時間 block で検証する。境界付近の window は除く。
    const long long block = std::max<long long>(static_cast<long long>(0.5 * sr), cfg.nFft * 8LL);
    std::vector<int> fold(static_cast<std::size_t>(T));
    std::vector<char> independent(static_cast<std::size_t>(T));
    for (int t = 0; t < T; ++t) {
        const long long c = centers[static_cast<std::size_t>(t)];
        fold[static_cast<std::size_t>(t)] = static_cast<int>((c / block) % 2);
        const long long pos = c % block;
        independent[static_cast<std::size_t>(t)] = pos >= cfg.nFft && pos <= block - cfg.nFft;
    }
    for (int k = 0; k < 2; ++k) {
        std::vector<char> train(static_cast<std::size_t>(T)), test(static_cast<std::size_t>(T));
        int nTrain = 0, nTest = 0;
        for (int t = 0; t < T; ++t) {
            train[static_cast<std::size_t>(t)] = fold[static_cast<std::size_t>(t)] != k && independent[static_cast<std::size_t>(t)];
            test[static_cast<std::size_t>(t)] = fold[static_cast<std::size_t>(t)] == k && independent[static_cast<std::size_t>(t)];
            nTrain += train[static_cast<std::size_t>(t)];
            nTest += test[static_cast<std::size_t>(t)];
        }
        if (nTrain < 12 || nTest < 12) continue;
        const Spec xTr = selectFrames(X, train), yTr = selectFrames(Y, train), xTe = selectFrames(X, test), yTe = selectFrames(Y, test);
        const Transfer hCv = fitTransfer(xTr, yTr, cfg);
        const double linLoss = meanErrorPower(yTe, applyTransfer(xTe, hCv));
        const Transfer pr = priorFrom(hCv);
        const Transfer hnCv = fitTransfer(selectFrames(feat, train), yTr, cfg, &pr, &penalties);
        const double nlLoss = meanErrorPower(yTe, applyTransfer(selectFrames(feat, test), hnCv));
        cal.cvImprovement.push_back((linLoss - nlLoss) / std::max(linLoss, 1e-20));
    }
    cal.nonlinearAccepted = cal.cvImprovement.size() == 2 &&
        std::min(cal.cvImprovement[0], cal.cvImprovement[1]) > cfg.nonlinearMinCvImprovement;
    const Transfer pr = priorFrom(cal.linear);
    cal.nonlinear = fitTransfer(feat, Y, cfg, &pr, &penalties);
    cal.hasNonlinear = true;
    return cal;
}


namespace {

// 一般化相互相関。|相関| の最大を探すので極性が逆でもよい。戻り値は (遅れ, 信頼度)。
std::pair<double, double> gccDelay(const std::vector<double>& yIn, const std::vector<double>& xIn, long long maxLag,
                                   double beta, int oversample) {
    const std::size_t n = std::min(yIn.size(), xIn.size());
    if (n < 16) return {0.0, 0.0};
    std::vector<double> y(yIn.begin(), yIn.begin() + static_cast<std::ptrdiff_t>(n)), x(xIn.begin(), xIn.begin() + static_cast<std::ptrdiff_t>(n));
    double ny = 0, nx = 0;
    for (std::size_t i = 0; i < n; ++i) { ny += y[i] * y[i]; nx += x[i] * x[i]; }
    if (std::sqrt(ny) < 1e-12 || std::sqrt(nx) < 1e-12) return {0.0, 0.0};
    const double my = std::accumulate(y.begin(), y.end(), 0.0) / n, mx = std::accumulate(x.begin(), x.end(), 0.0) / n;
    for (std::size_t i = 0; i < n; ++i) { y[i] -= my; x[i] -= mx; }
    const std::size_t nfft = nextPow2(2 * n - 1);
    std::vector<cd> Y(nfft), X(nfft);
    for (std::size_t i = 0; i < n; ++i) { Y[i] = y[i]; X[i] = x[i]; }
    fftDouble(Y, false);
    fftDouble(X, false);
    const std::size_t half = nfft / 2;
    std::vector<cd> cross(half + 1);
    double maxAbs = 0;
    for (std::size_t k = 0; k <= half; ++k) {
        cross[k] = Y[k] * std::conj(X[k]);
        maxAbs = std::max(maxAbs, std::abs(cross[k]));
    }
    const double floor = std::max(maxAbs * 1e-7, 1e-20);
    for (std::size_t k = 0; k <= half; ++k) cross[k] /= std::pow(std::max(std::abs(cross[k]), floor), beta);
    cross[0] = 0;
    // 帯域制限の補間: 片側 spectrum を 0 詰めして長い irfft を取る。
    const std::size_t big = nfft * static_cast<std::size_t>(oversample);
    std::vector<cd> full(big, cd(0, 0));
    for (std::size_t k = 0; k <= half; ++k) {
        full[k] = cross[k];
        if (k > 0 && k < big / 2) full[big - k] = std::conj(cross[k]);
    }
    if (oversample == 1) full[half] = cd(cross[half].real(), 0);
    fftDouble(full, true);
    const long long lim = std::min<long long>(std::max<long long>(0, maxLag), static_cast<long long>(n / 2)) * oversample;
    long long bestK = 0;
    double bestV = -1;
    std::vector<double> vals(static_cast<std::size_t>(2 * lim + 1));
    for (long long g = -lim; g <= lim; ++g) {
        const std::size_t at = static_cast<std::size_t>(((g % static_cast<long long>(big)) + static_cast<long long>(big)) % static_cast<long long>(big));
        const double v = std::abs(full[at].real());
        vals[static_cast<std::size_t>(g + lim)] = v;
        if (v > bestV) { bestV = v; bestK = g + lim; }
    }
    double frac = 0.0;
    if (bestK > 0 && bestK < static_cast<long long>(vals.size()) - 1) {
        const double a = vals[static_cast<std::size_t>(bestK - 1)], b = vals[static_cast<std::size_t>(bestK)], c = vals[static_cast<std::size_t>(bestK + 1)];
        const double den = a - 2 * b + c;
        if (std::fabs(den) > 1e-30) frac = std::clamp(0.5 * (a - c) / den, -0.5, 0.5);
    }
    const double lag = (static_cast<double>(bestK - lim) + frac) / oversample;
    const long long d = std::llround(lag);
    double dot = 0, na = 0, nb = 0;
    for (long long i = 0; i < static_cast<long long>(n); ++i) {
        const long long j = i - d;
        if (j < 0 || j >= static_cast<long long>(n)) continue;
        dot += y[static_cast<std::size_t>(i)] * x[static_cast<std::size_t>(j)];
        na += y[static_cast<std::size_t>(i)] * y[static_cast<std::size_t>(i)];
        nb += x[static_cast<std::size_t>(j)] * x[static_cast<std::size_t>(j)];
    }
    const double score = std::fabs(dot) / (std::sqrt(na * nb) + 1e-20);
    return {lag, std::clamp(score, 0.0, 1.0)};
}

// Kaiser 窓 I0(beta sqrt(1 - r^2)) / I0(beta) を r^2 の表から線形補間で引く。
// 標本ごと・tap ごとに Bessel 級数を計算すると数十億回になるため。表の誤差は 1e-9 程度。
class KaiserTable {
public:
    explicit KaiserTable(double beta) : t_(kSize + 2) {
        const double i0b = besselI0(beta);
        for (int i = 0; i <= kSize + 1; ++i) {
            const double r2 = std::min(1.0, static_cast<double>(i) / kSize);
            t_[static_cast<std::size_t>(i)] = besselI0(beta * std::sqrt(1.0 - r2)) / i0b;
        }
    }
    double operator()(double r) const {
        const double x = std::min(1.0, r * r) * kSize;
        const int i = static_cast<int>(x);
        const double f = x - i;
        return t_[static_cast<std::size_t>(i)] + (t_[static_cast<std::size_t>(i) + 1] - t_[static_cast<std::size_t>(i)]) * f;
    }
private:
    static constexpr int kSize = 1 << 18;
    std::vector<double> t_;
};

std::vector<double> channelSlice(const Audio& a, int c, std::size_t from, std::size_t count) {
    std::vector<double> v(count);
    for (std::size_t i = 0; i < count; ++i) v[i] = from + i < a.frames() ? a.at(from + i, c) : 0.0;
    return v;
}

// 低い rate への単純な間引き。8 kHz 付近での粗い探索にだけ使うので、窓付き sinc の低域通過で十分。
std::vector<double> decimate(const std::vector<double>& x, int sr, int target) {
    if (sr == target) return x;
    const double ratio = static_cast<double>(sr) / target;
    const double fc = 0.45 / ratio;
    const int taps = 64;
    const std::size_t n = static_cast<std::size_t>(static_cast<double>(x.size()) / ratio);
    std::vector<double> y(n);
    static const KaiserTable kaiser(8.0);
    parallelFor(static_cast<long long>(n), 4096, [&](long long b, long long e) {
        for (long long i = b; i < e; ++i) {
            const double center = static_cast<double>(i) * ratio;
            const long long c0 = static_cast<long long>(std::floor(center));
            double acc = 0, wsum = 0;
            for (long long j = c0 - static_cast<long long>(taps * ratio); j <= c0 + static_cast<long long>(taps * ratio); ++j) {
                const double d = (static_cast<double>(j) - center);
                const double r = d / (taps * ratio);
                if (std::fabs(r) >= 1.0) continue;
                const double arg = 2 * fc * d;
                const double s = arg == 0 ? 1.0 : std::sin(kPi * arg) / (kPi * arg);
                const double w = 2 * fc * s * kaiser(r);
                wsum += w;
                if (j >= 0 && j < static_cast<long long>(x.size())) acc += x[static_cast<std::size_t>(j)] * w;
            }
            y[static_cast<std::size_t>(i)] = wsum != 0 ? acc / wsum : 0.0;
        }
    });
    return y;
}

}

Alignment estimateAlignment(const Audio& mix, const Audio& ref, int sr, const Config& cfg) {
    Alignment al;
    if (cfg.alignment == 0) return al;
    const std::size_t n = std::min(mix.frames(), ref.frames());
    if (n < static_cast<std::size_t>(std::max(64, static_cast<int>(0.1 * sr))))
        throw std::runtime_error("the audio is too short for automatic alignment");
    const int lowSr = std::min(8000, sr);
    const std::size_t coarseN = std::min<std::size_t>(n, static_cast<std::size_t>((60 + cfg.maxOffsetSeconds) * sr));
    double bestScore = -1, coarse = 0;
    int cy = 0, cx = 0;
    // 粗い探索: 低い rate に落として、全 channel の組み合わせで最も相関の高いものを使う。
    std::vector<std::vector<double>> ylow, xlow;
    for (int a = 0; a < mix.channels; ++a) ylow.push_back(decimate(channelSlice(mix, a, 0, coarseN), sr, lowSr));
    for (int b = 0; b < ref.channels; ++b) xlow.push_back(decimate(channelSlice(ref, b, 0, coarseN), sr, lowSr));
    for (int a = 0; a < mix.channels; ++a)
        for (int b = 0; b < ref.channels; ++b) {
            const auto [d, s] = gccDelay(ylow[static_cast<std::size_t>(a)], xlow[static_cast<std::size_t>(b)],
                                         static_cast<long long>(cfg.maxOffsetSeconds * lowSr), 0.25, 1);
            if (s > bestScore) { bestScore = s; coarse = d * sr / lowSr; cy = a; cx = b; }
        }
    const long long win = std::min<long long>(static_cast<long long>(cfg.alignmentWindowSeconds * sr), static_cast<long long>(n / 2));
    const long long half = win / 2;
    const long long start = std::max<long long>(half, static_cast<long long>(std::max(0.0, coarse)) + half);
    const long long stop = std::min<long long>(static_cast<long long>(mix.frames()) - half - 1,
                                               static_cast<long long>(ref.frames() + std::min(0.0, coarse)) - half - 1);
    std::vector<long long> centers;
    if (stop > start)
        for (int i = 0; i < cfg.alignmentAnchors; ++i)
            centers.push_back(static_cast<long long>(start + (stop - start) * static_cast<double>(i) / std::max(1, cfg.alignmentAnchors - 1)));
    else
        centers.push_back(static_cast<long long>(n / 2));
    long long localLimit = static_cast<long long>(sr * 0.004 + cfg.maxDriftPpm * 1e-6 * static_cast<double>(mix.frames()));
    localLimit = std::min(std::max<long long>(8, localLimit), win / 3);
    struct Anchor { double t, d, w; };
    std::vector<Anchor> anchors(centers.size(), {0, 0, -1});
    for (std::size_t i = 0; i < centers.size(); ++i) {
        const long long sy = centers[i] - half;
        const long long sx = std::llround(static_cast<double>(sy) - coarse);
        if (sy < 0 || sx < 0 || sy + win > static_cast<long long>(mix.frames()) || sx + win > static_cast<long long>(ref.frames())) continue;
        const auto [lag, score] = gccDelay(channelSlice(mix, cy, static_cast<std::size_t>(sy), static_cast<std::size_t>(win)),
                                           channelSlice(ref, cx, static_cast<std::size_t>(sx), static_cast<std::size_t>(win)), localLimit, 0.5, 8);
        anchors[i] = {static_cast<double>(centers[i]), static_cast<double>(sy - sx) + lag, score};
    }
    std::vector<Anchor> usable;
    for (const Anchor& a : anchors)
        if (a.w >= cfg.minAlignmentScore) usable.push_back(a);
    al.anchors = static_cast<int>(usable.size());
    if (usable.size() < 3) {
        al.mode = 2;
        al.offset = coarse;
        al.score = bestScore;
        return al;
    }
    double origin = 0;
    for (const Anchor& a : usable) origin += a.t;
    origin /= usable.size();
    std::vector<double> dvals;
    for (const Anchor& a : usable) dvals.push_back(a.d);
    double p0 = median(dvals), p1 = 0.0;
    const double limit = cfg.maxDriftPpm * 1e-6 * sr;
    if (cfg.alignment != 2 && limit > 0) {
        // Huber (f_scale 0.5) の重み付き直線当てはめを IRLS で解く。傾きは範囲内に制限する。
        for (int it = 0; it < 60; ++it) {
            double s0 = 0, s1 = 0, s2 = 0, r0 = 0, r1 = 0;
            for (const Anchor& a : usable) {
                const double ts = (a.t - origin) / sr;
                const double r = std::sqrt(a.w) * (p0 + p1 * ts - a.d);
                const double hw = std::fabs(r) <= 0.5 ? 1.0 : 0.5 / std::fabs(r);
                const double w = a.w * hw;
                s0 += w; s1 += w * ts; s2 += w * ts * ts; r0 += w * a.d; r1 += w * a.d * ts;
            }
            const double det = s0 * s2 - s1 * s1;
            double np1 = std::fabs(det) > 1e-30 ? (s0 * r1 - s1 * r0) / det : 0.0;
            np1 = std::clamp(np1, -limit, limit);
            const double np0 = (r0 - np1 * s1) / std::max(s0, 1e-30);
            const bool done = std::fabs(np0 - p0) < 1e-9 && std::fabs(np1 - p1) < 1e-12;
            p0 = np0;
            p1 = np1;
            if (done) break;
        }
        al.mode = 1;
    } else {
        al.mode = 2;
        p1 = 0.0;
    }
    al.slope = p1 / sr;
    al.offset = p0 - al.slope * origin;
    std::vector<double> res, dev;
    for (const Anchor& a : usable) res.push_back(a.d - al.delay(a.t));
    const double med = median(res);
    for (double r : res) dev.push_back(std::fabs(r - med));
    al.anchorMad = median(dev);
    std::vector<double> good;
    for (std::size_t i = 0; i < usable.size(); ++i)
        if (std::fabs(res[i]) <= std::max(1.5, 4.5 * al.anchorMad)) good.push_back(usable[i].w);
    al.score = good.empty() ? 0.0 : median(good);
    return al;
}

Audio warpReference(const Audio& ref, std::size_t length, const Alignment& a, int taps, std::vector<char>& valid) {
    Audio out;
    out.channels = ref.channels;
    out.v.assign(length * static_cast<std::size_t>(ref.channels), 0.0f);
    valid.assign(length, 0);
    const long long refLen = static_cast<long long>(ref.frames());
    // 整数の遅れだけなら補間せずにそのまま写す。
    if (std::fabs(a.slope) < 1e-12 && std::fabs(a.offset - std::round(a.offset)) < 1e-7) {
        const long long off = std::llround(a.offset);
        for (std::size_t i = 0; i < length; ++i) {
            const long long j = static_cast<long long>(i) - off;
            if (j < 0 || j >= refLen) continue;
            valid[i] = 1;
            for (int c = 0; c < ref.channels; ++c) out.at(i, c) = ref.at(static_cast<std::size_t>(j), c);
        }
        return out;
    }
    const double halfT = taps / 2.0;
    static const KaiserTable kaiser(10.0);
    // 参照を速く読む (rate > 1) ときだけ遮断周波数を下げて折り返しを防ぐ。
    const double rate = 1.0 - a.slope;
    const double cutoff = std::min(1.0, 1.0 / std::max(rate, 1e-9));
    parallelFor(static_cast<long long>(length), 4096, [&](long long b, long long e) {
        std::vector<double> w(static_cast<std::size_t>(taps));
        for (long long i = b; i < e; ++i) {
            const double p = static_cast<double>(i) - a.delay(static_cast<double>(i));
            if (p < 0 || p > static_cast<double>(refLen - 1)) continue;
            valid[static_cast<std::size_t>(i)] = 1;
            const long long base = static_cast<long long>(std::floor(p));
            double wsum = 0;
            for (int k = 0; k < taps; ++k) {
                const long long idx = base + (k - taps / 2 + 1);
                const double dist = p - static_cast<double>(idx);
                double v = 0;
                if (std::fabs(dist) < halfT) {
                    const double win = kaiser(dist / halfT);
                    const double arg = cutoff * dist;
                    const double s = arg == 0 ? 1.0 : std::sin(kPi * arg) / (kPi * arg);
                    v = cutoff * s * win;
                }
                w[static_cast<std::size_t>(k)] = v;
                wsum += v;
            }
            wsum = std::max(wsum, 1e-12);
            for (int c = 0; c < ref.channels; ++c) {
                double acc = 0;
                for (int k = 0; k < taps; ++k) {
                    const long long idx = base + (k - taps / 2 + 1);
                    if (idx >= 0 && idx < refLen) acc += ref.at(static_cast<std::size_t>(idx), c) * (w[static_cast<std::size_t>(k)] / wsum);
                }
                out.at(static_cast<std::size_t>(i), c) = static_cast<float>(acc);
            }
        }
    });
    return out;
}


namespace {

Audio slice(const Audio& a, std::size_t from, std::size_t to) {
    Audio s;
    s.channels = a.channels;
    s.v.assign(a.v.begin() + static_cast<std::ptrdiff_t>(from * a.channels), a.v.begin() + static_cast<std::ptrdiff_t>(to * a.channels));
    return s;
}

double rmsOf(const Audio& a) {
    double s = 0;
    for (float v : a.v) s += static_cast<double>(v) * v;
    return std::sqrt(s / std::max<std::size_t>(1, a.v.size()));
}

}

Result separate(const Audio& mix, const Audio& refIn, std::vector<char> valid, int sr,
                const Config& cfg, Method method, const Progress& progress) {
    Result res;
    const std::size_t len = mix.frames();
    res.estimate = mix;
    auto report = [&](double f) { return !progress || progress(f); };

    if (len <= static_cast<std::size_t>(cfg.nFft) * 2) {
        res.error = "the audio must be longer than two analysis windows";
        return res;
    }
    if (rmsOf(refIn) < 1e-12 || rmsOf(mix) < 1e-12) return res;   // 片方が無音なら原曲をそのまま返す

    Audio reference;
    if (cfg.alignment != 0) {
        res.alignment = estimateAlignment(mix, refIn, sr, cfg);
        if (res.alignment.score < cfg.minAlignmentScore) {
            char buf[200];
            std::snprintf(buf, sizeof buf, "the alignment confidence %.3f is below %.3f; the instrumental may not match this original",
                          res.alignment.score, cfg.minAlignmentScore);
            res.error = buf;
            return res;
        }
        log::detail("  cross-correlation: offset %.3f samples, drift %+.3f ppm, score %.3f, %d anchors (spread %.3f)",
                    -res.alignment.offset, -res.alignment.slope * 1e6, res.alignment.score, res.alignment.anchors, res.alignment.anchorMad);
        reference = warpReference(refIn, len, res.alignment, cfg.sincTaps, valid);
    } else {
        reference = refIn;
        if (valid.size() != len) valid.assign(len, 1);
    }
    if (!report(0.1)) return res;

    const bool needNl = method == Method::Hammerstein || method == Method::Ensemble;
    res.calibration = calibrate(mix, reference, valid, sr, cfg, needNl);
    const Calibration& cal = res.calibration;
    log::detail("  calibration: %d windows%s, coherence %.3f%s", cal.frames, cfg.fitSpans.empty() ? "" : " (vocal-free passages only)",
                cal.medianCoherence,
                cal.hasNonlinear ? (std::string(", nonlinear ") + (cal.nonlinearAccepted ? "accepted" : "rejected") +
                                    (cal.cvImprovement.size() == 2 ? " (held-out gain " + std::to_string(cal.cvImprovement[0] * 100).substr(0, 5) +
                                     "% / " + std::to_string(cal.cvImprovement[1] * 100).substr(0, 5) + "%)" : "")).c_str() : "");
    log::Eta eta;
    int block = 0;
    const long long totalBlocks = static_cast<long long>((len + std::max<std::size_t>(1, static_cast<std::size_t>(
        std::max<std::size_t>(static_cast<std::size_t>(cfg.nFft) * 4, static_cast<std::size_t>(cfg.blockSeconds * sr)) * 0.75)) - 1) /
        std::max<std::size_t>(1, static_cast<std::size_t>(std::max<std::size_t>(static_cast<std::size_t>(cfg.nFft) * 4,
                                                                             static_cast<std::size_t>(cfg.blockSeconds * sr)) * 0.75)));
    if (!report(0.3)) return res;

    const int C = mix.channels;
    std::vector<double> acc(len * static_cast<std::size_t>(C), 0.0), norm(len, 0.0);
    const bool keep = cfg.keepMembers && method == Method::Ensemble;
    std::vector<std::string> memberNames;
    std::vector<std::vector<double>> memberAcc;
    const std::size_t core = std::max<std::size_t>(static_cast<std::size_t>(cfg.nFft) * 4, static_cast<std::size_t>(cfg.blockSeconds * sr));
    const std::size_t context = std::max<std::size_t>(static_cast<std::size_t>(cfg.nFft), static_cast<std::size_t>(cfg.contextSeconds * sr));
    const std::size_t step = std::max<std::size_t>(1, static_cast<std::size_t>(core * 0.75));
    for (std::size_t a = 0; a < len; a += step) {
        const std::size_t b = std::min(a + core, len);
        const std::size_t left = a >= context ? a - context : 0, right = std::min(len, b + context);
        const Audio mixBlk = slice(mix, left, right), refBlk = slice(reference, left, right);
        const Spec Y = stft(mixBlk, cfg.nFft, cfg.hop);
        const Spec X = stft(refBlk, cfg.nFft, cfg.hop);
        const Spec baseline = applyTransfer(X, cal.linear);

        auto kalman = [&]() {
            std::vector<char> update;
            if (!cfg.fitSpans.empty()) {
                // 声の無い区間に完全に収まる window でだけ更新する。
                update.assign(static_cast<std::size_t>(Y.T), 0);
                const double halfWin = cfg.nFft / (2.0 * sr);
                for (int t = 0; t < Y.T; ++t) {
                    const double time = (static_cast<double>(left) + static_cast<double>(t) * cfg.hop) / sr;
                    for (const auto& s : cfg.fitSpans)
                        if (time - halfWin >= s.first && time + halfWin < s.second) update[static_cast<std::size_t>(t)] = 1;
                }
            }
            return kalmanBackground(Y, baseline, cfg, update);
        };
        auto hammerstein = [&]() {
            if (!cal.nonlinearAccepted) return baseline;
            const Spec feat = stft(cal.basis.transform(refBlk), cfg.nFft, cfg.hop);
            return hammersteinBackground(feat, cal.nonlinear, baseline, true, cfg.nonlinearMaxRelativeRms);
        };
        auto powerReference = [&]() {
            Spec p = X;
            for (int f = 0; f < p.F; ++f)
                for (int t = 0; t < p.T; ++t)
                    for (int c = 0; c < p.C; ++c) p.at(f, t, c) *= cal.powerGain[static_cast<std::size_t>(f) * C + c];
            return p;
        };

        Spec bg;
        std::vector<std::pair<const char*, Spec>> blockMembers;
        switch (method) {
        case Method::Robust: bg = baseline; break;
        case Method::Kalman: bg = kalman(); break;
        case Method::Hammerstein: bg = hammerstein(); break;
        case Method::Nmf: bg = nmfBackground(Y, powerReference(), cfg); break;
        case Method::Spatial: bg = spatialBackground(Y, powerReference(), cfg); break;
        case Method::Ensemble: {
            const Spec k = kalman();
            std::vector<const Spec*> cands{&baseline, &k};
            Spec hm;
            // 採用されなかった非線形は基準の複製なので、独立した票に数えない。
            if (cal.nonlinearAccepted) {
                hm = hammerstein();
                cands.push_back(&hm);
            }
            bg = consensusBackground(cands, baseline, cfg.ensembleStrength);
            if (keep) {
                blockMembers.emplace_back("Robust", baseline);
                blockMembers.emplace_back("Kalman", k);
                if (cal.nonlinearAccepted) blockMembers.emplace_back("Hammerstein", hm);
            }
            break;
        }
        }

        // 背景を波形に戻し、原曲から引く。block の重なりは正の cosine 窓で重みづけして足す。
        const std::size_t length = b - a;
        auto add = [&](const Spec& spec, std::vector<double>& into, bool weights) {
            const Audio removed = istft(spec, cfg.nFft, cfg.hop, right - left);
            for (std::size_t i = 0; i < length; ++i) {
                double w = std::pow(std::sin(kPi * (static_cast<double>(i) + 0.5) / length), 2);
                if (a == 0 && i < std::min(step / 2, length)) w = 1.0;
                if (b == len && i + step / 2 >= length) w = 1.0;
                if (weights) norm[a + i] += w;
                for (int c = 0; c < C; ++c) {
                    const float m = mix.at(a + i, c);
                    const float r = valid[a + i] ? m - removed.at(a + i - left, c) : m;
                    into[(a + i) * static_cast<std::size_t>(C) + c] += static_cast<double>(r) * w;
                }
            }
        };
        add(bg, acc, true);
        for (std::size_t m = 0; m < blockMembers.size(); ++m) {
            if (m >= memberAcc.size()) {
                memberNames.emplace_back(blockMembers[m].first);
                memberAcc.emplace_back(len * static_cast<std::size_t>(C), 0.0);
            }
            add(blockMembers[m].second, memberAcc[m], false);
        }
        // Python 版と同じく、開始位置は最後まで刻む (末尾の短い block も重ねて足す)。
        ++block;
        const double frac = static_cast<double>(b) / len;
        log::status(1, false, "  %s | block %d/%lld | %s elapsed | ~%s left", log::progressBar(frac).c_str(), block, totalBlocks,
                    log::clock(eta.elapsed()).c_str(), log::clock(eta.remaining(frac)).c_str());
        if (!report(0.3 + 0.7 * frac)) return res;
    }
    log::clearStatus(1);
    for (std::size_t i = 0; i < len; ++i)
        for (int c = 0; c < C; ++c)
            res.estimate.at(i, c) = static_cast<float>(acc[i * static_cast<std::size_t>(C) + c] / std::max(norm[i], 1e-12));
    for (std::size_t m = 0; m < memberAcc.size(); ++m) {
        Audio member = res.estimate;
        for (std::size_t i = 0; i < len; ++i)
            for (int c = 0; c < C; ++c)
                member.at(i, c) = static_cast<float>(memberAcc[m][i * static_cast<std::size_t>(C) + c] / std::max(norm[i], 1e-12));
        res.members.emplace_back(memberNames[m], std::move(member));
    }
    res.reference = reference;
    return res;
}

}
}
