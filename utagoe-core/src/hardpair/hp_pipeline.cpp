#include "hp.h"
#include "../parallel.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>

namespace utagoe {
namespace hp {

const char* methodName(Method m) {
    switch (m) {
    case Method::Rational: return "Rational";
    case Method::Surface: return "Surface";
    case Method::Trend: return "Trend";
    case Method::Ctf: return "CTF";
    case Method::LowRank: return "Low-rank";
    }
    return "?";
}

namespace {

constexpr double kPi = 3.14159265358979323846;

Audio slice(const Audio& a, std::size_t lo, std::size_t hi) {
    Audio s;
    s.channels = a.channels;
    s.v.assign(a.v.begin() + static_cast<std::ptrdiff_t>(lo * a.channels), a.v.begin() + static_cast<std::ptrdiff_t>(hi * a.channels));
    return s;
}

Audio subtract(const Audio& a, const Audio& b) {
    Audio r = a;
    for (std::size_t i = 0; i < r.v.size(); ++i) r.v[i] = a.v[i] - b.v[i];
    return r;
}

}

std::vector<std::pair<Method, Audio>> render(const Audio& mix, const Audio& base, double sr,
                                             const std::vector<Method>& methods, const Progress& progress, bool& cancelled) {
    std::vector<std::pair<Method, Audio>> out;
    const std::size_t len = mix.frames();
    int N = 1 << static_cast<int>(std::nearbyint(std::log2(2048.0 * sr / 44100.0)));
    N = std::max(256, N);
    const int hop = N / 4;
    auto want = [&](Method m) { return std::find(methods.begin(), methods.end(), m) != methods.end(); };
    if (want(Method::Rational)) out.emplace_back(Method::Rational, subtract(mix, base));
    if (want(Method::Trend)) {
        if (progress && !progress(0.0, "sparse-curvature gain trend")) { cancelled = true; return out; }
        out.emplace_back(Method::Trend, subtract(mix, trendDemaster(mix, base, sr)));
    }
    std::vector<Method> spectral;
    for (Method m : {Method::Surface, Method::Ctf, Method::LowRank})
        if (want(m)) spectral.push_back(m);
    if (spectral.empty()) return out;
    const std::size_t core = std::max<std::size_t>(static_cast<std::size_t>(N) * 8, static_cast<std::size_t>(12.0 * sr));
    const std::size_t step = std::max<std::size_t>(1, static_cast<std::size_t>(static_cast<double>(core) * 2 / 3));
    const std::size_t context = std::max<std::size_t>(static_cast<std::size_t>(N) * 2, static_cast<std::size_t>(3.0 * sr));
    std::vector<std::size_t> starts;
    for (std::size_t a = 0; a < len; a += step) starts.push_back(a);
    const int nb = static_cast<int>(starts.size());
    std::vector<std::vector<Audio>> preds(spectral.size(), std::vector<Audio>(static_cast<std::size_t>(nb)));
    std::atomic<int> done{0};
    std::atomic<bool> stop{false};
    std::mutex progressLock;
    parallelFor(nb, 1, [&](long long b0, long long e0) {
        for (long long bi = b0; bi < e0 && !stop; ++bi) {
            const std::size_t a = starts[static_cast<std::size_t>(bi)];
            const std::size_t b = std::min(a + core, len);
            const std::size_t lo = a >= context ? a - context : 0, hi = std::min(len, b + context);
            const Audio xb = slice(base, lo, hi), yb = slice(mix, lo, hi);
            const Spec X = rc::stft(xb, N, hop), Y = rc::stft(yb, N, hop);
            for (std::size_t mi = 0; mi < spectral.size(); ++mi) {
                Spec B;
                switch (spectral[mi]) {
                case Method::Surface: B = phaseSurface(Y, X, sr, N, hop); break;
                case Method::Ctf: B = ctf(Y, X, sr, N, hop); break;
                default: B = lowrankField(Y, X, sr, N, hop); break;
                }
                const Audio p = rc::istft(B, N, hop, hi - lo);
                preds[mi][static_cast<std::size_t>(bi)] = slice(p, a - lo, b - lo);
            }
            const int finished = ++done;
            if (progress) {
                std::lock_guard<std::mutex> g(progressLock);
                if (!progress(static_cast<double>(finished) / nb, "block " + std::to_string(finished) + "/" + std::to_string(nb))) stop = true;
            }
        }
    });
    if (stop) { cancelled = true; return out; }
    for (std::size_t mi = 0; mi < spectral.size(); ++mi) {
        Audio accum;
        accum.channels = mix.channels;
        accum.v.assign(mix.v.size(), 0.0f);
        std::vector<float> norm(len, 0.0f);
        for (int bi = 0; bi < nb; ++bi) {
            const std::size_t a = starts[static_cast<std::size_t>(bi)];
            const std::size_t b = std::min(a + core, len);
            const std::size_t width = b - a;
            std::vector<float> w(width);
            for (std::size_t i = 0; i < width; ++i) {
                const double s = std::sin(kPi * (static_cast<double>(i) + 0.5) / static_cast<double>(width));
                w[i] = static_cast<float>(s * s);
            }
            if (a == 0) for (std::size_t i = 0; i < std::min(step / 2, width); ++i) w[i] = 1.0f;
            if (b == len) for (std::size_t i = (width > step / 2 ? width - step / 2 : 0); i < width; ++i) w[i] = 1.0f;
            const Audio& p = preds[mi][static_cast<std::size_t>(bi)];
            for (std::size_t i = 0; i < width; ++i) {
                norm[a + i] += w[i];
                for (int c = 0; c < mix.channels; ++c) accum.at(a + i, c) += p.at(i, c) * w[i];
            }
        }
        for (std::size_t i = 0; i < len; ++i) {
            const float d = std::max(norm[i], 1e-12f);
            for (int c = 0; c < mix.channels; ++c) accum.at(i, c) /= d;
        }
        out.emplace_back(spectral[mi], subtract(mix, accum));
    }
    std::vector<std::pair<Method, Audio>> ordered;
    for (Method m : methods)
        for (auto& r : out)
            if (r.first == m) ordered.push_back(std::move(r));
    return ordered;
}

Audio prealignedReference(const Audio& mix, const Audio& instrumental) {
    Audio ref;
    ref.channels = mix.channels;
    ref.v.assign(mix.v.size(), 0.0f);
    const std::size_t n = std::min(mix.frames(), instrumental.frames());
    for (std::size_t i = 0; i < n; ++i)
        for (int c = 0; c < mix.channels; ++c) ref.at(i, c) = instrumental.at(i, c);
    return ref;
}

Audio alignDense(const Audio& mix, const Audio& instrumental, double sr, TimeMap& map, const Progress& progress, bool& cancelled) {
    if (mix.channels != 2 || instrumental.channels != 2) throw Error("the dense time map needs stereo input (it follows the side channel)");
    if (sr < 8000 || sr > 192000) throw Error("supported sample rates are 8000 to 192000 Hz");
    auto report = [&](double f, const std::string& what) {
        if (progress && !progress(f, what)) cancelled = true;
        return !cancelled;
    };
    if (!report(0.0, "dense side-guided time map")) return {};
    map = initialMap(mix, instrumental, sr);
    Audio aligned = warpReference(instrumental, mix.frames(), map, sr);
    for (int k = 0; k < 2; ++k) {
        if (!report(0.4 + 0.3 * k, "phase/time refinement " + std::to_string(k + 1) + "/2")) return {};
        const PhaseModel model = fitPhase(mix, aligned, sr);
        const Audio provisional = applyPhase(aligned, sr, model);
        refineMap(mix, provisional, map, sr, k == 0 ? 2.0 : 1.0);
        aligned = warpReference(instrumental, mix.frames(), map, sr);
    }
    report(1.0, "time map done");
    return aligned;
}

Result separate(const Audio& mix, const Audio& instrumental, double sr, const std::vector<Method>& methods,
                bool aligned, const Progress& progress) {
    if (mix.channels != 2 || instrumental.channels != 2) throw Error("these algorithms need stereo input (they use the side channel to protect the centre)");
    if (sr < 8000 || sr > 192000) throw Error("supported sample rates are 8000 to 192000 Hz");
    Result res;
    auto report = [&](double f, const std::string& what) { return !progress || progress(f, what); };
    if (aligned) {
        res.aligned = prealignedReference(mix, instrumental);
    } else {
        bool cancelled = false;
        Progress inner;
        if (progress) inner = [&](double f, const std::string& what) { return progress(0.2 * f, what); };
        res.aligned = alignDense(mix, instrumental, sr, res.map, inner, cancelled);
        if (cancelled) { res.cancelled = true; return res; }
    }
    if (!report(0.2, "rational phase fit")) { res.cancelled = true; return res; }
    res.phase = fitPhase(mix, res.aligned, sr);
    res.base = applyPhase(res.aligned, sr, res.phase);
    bool cancelled = false;
    Progress inner;
    if (progress) inner = [&](double f, const std::string& what) { return progress(0.25 + 0.75 * f, what); };
    res.residuals = render(mix, res.base, sr, methods, inner, cancelled);
    res.cancelled = cancelled;
    return res;
}

}
}
