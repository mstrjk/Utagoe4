#include "fw_internal.h"

#include <algorithm>
#include <cmath>

namespace utagoe {
namespace fw {
namespace {

constexpr double kTiny = 1e-20;

double energy(const Audio& a) {
    double s = 0;
    for (double v : a.v) s += v * v;
    return s;
}

Audio downsample(const Audio& a, int factor) {
    Audio out;
    out.channels = a.channels;
    std::vector<std::vector<double>> ch(static_cast<std::size_t>(a.channels));
    for (int c = 0; c < a.channels; ++c) {
        std::vector<double> v(a.frames());
        for (std::size_t i = 0; i < v.size(); ++i) v[i] = a.at(i, c);
        ch[static_cast<std::size_t>(c)] = resamplePoly(v, 1, factor);
    }
    out.v.resize(ch[0].size() * static_cast<std::size_t>(a.channels));
    for (std::size_t i = 0; i < ch[0].size(); ++i)
        for (int c = 0; c < a.channels; ++c) out.at(i, c) = ch[static_cast<std::size_t>(c)][i];
    return out;
}

std::string chooseGuide(const Audio& ref, const std::string& requested) {
    if (requested != "auto") {
        if (requested == "side" && ref.channels != 2) throw Error("side guide requires stereo");
        return requested;
    }
    if (ref.channels == 2) {
        double mm = 0, ss = 0;
        for (std::size_t i = 0; i < ref.frames(); ++i) {
            const double mid = (ref.at(i, 0) + ref.at(i, 1)) * .5, side = (ref.at(i, 0) - ref.at(i, 1)) * .5;
            mm += mid * mid;
            ss += side * side;
        }
        if (ss / (mm + ss + kTiny) > .02) return "side";
    }
    return "full";
}

void findDelay(const Audio& ref, const Audio& tar, long long source, long long target, long long length, long long margin, int sr,
               double& delay, double& quality) {
    delay = 0;
    quality = 0;
    const int factor = std::max(1, sr / 6000);
    margin = static_cast<long long>(std::ceil(static_cast<double>(margin) / factor)) * factor;
    const Audio y = slicePad(tar, target, length);
    const Audio r = slicePad(ref, source - margin, length + 2 * margin);
    if (energy(y) < kTiny || energy(r) < kTiny) return;
    double d, q;
    if (factor > 1) corrPeak(downsample(r, factor), downsample(y, factor), d, q);
    else corrPeak(r, y, d, q);
    const long long coarse = static_cast<long long>(std::nearbyint(d * factor - static_cast<double>(margin)));
    const long long width = std::max(5, 2 * factor + 3);
    const Audio rr = slicePad(ref, source + coarse - width, length + 2 * width);
    corrPeak(rr, y, d, q);
    delay = static_cast<double>(coarse - width) + d;
    quality = q;
    if (q <= .25) return;
    const long long count = std::min<long long>(length, 2048);
    const double hi = static_cast<double>(std::max<long long>(24, length - 25));
    std::vector<long long> ix;
    for (long long k = 0; k < count; ++k) {
        const long long v = static_cast<long long>(count == 1 ? 24.0 : 24.0 + (hi - 24.0) * static_cast<double>(k) / static_cast<double>(count - 1));
        if (ix.empty() || ix.back() != v) ix.push_back(v);
    }
    const int C = y.channels;
    Audio iy;
    iy.channels = C;
    iy.v.resize(ix.size() * static_cast<std::size_t>(C));
    for (int c = 0; c < C; ++c) {
        double m = 0;
        for (std::size_t k = 0; k < ix.size(); ++k) m += y.at(static_cast<std::size_t>(ix[k]), c);
        m /= static_cast<double>(ix.size());
        for (std::size_t k = 0; k < ix.size(); ++k) iy.at(k, c) = y.at(static_cast<std::size_t>(ix[k]), c) - m;
    }
    const double iyy = energy(iy);
    const long long center = static_cast<long long>(std::nearbyint(delay));
    const Audio raw = slicePad(ref, source + center - 2, length + 5);
    std::vector<double> pos(ix.size());
    auto loss = [&](double frac) {
        for (std::size_t k = 0; k < ix.size(); ++k) pos[k] = 2.0 + static_cast<double>(ix[k]) + frac;
        Audio pr = sincSample(raw, pos, 16);
        double num = 0, pp = 0;
        for (int c = 0; c < C; ++c) {
            double m = 0;
            for (std::size_t k = 0; k < ix.size(); ++k) m += pr.at(k, c);
            m /= static_cast<double>(ix.size());
            for (std::size_t k = 0; k < ix.size(); ++k) {
                const double v = pr.at(k, c) - m;
                num += v * iy.at(k, c);
                pp += v * v;
            }
        }
        return -std::abs(num) / std::sqrt(std::max(kTiny, pp * iyy));
    };
    double fval;
    const double x = minimizeBounded(loss, -.65, .65, 2e-5, 20, fval);
    delay = static_cast<double>(center) + x;
    quality = std::clamp(-fval, 0.0, 1.0);
}

bool solve2(double a00, double a01, double a11, double b0, double b1, double& x0, double& x1) {
    const double det = a00 * a11 - a01 * a01;
    if (std::abs(det) < 1e-300 * std::max(1.0, std::abs(a00 * a11))) return false;
    x0 = (a11 * b0 - a01 * b1) / det;
    x1 = (a00 * b1 - a01 * b0) / det;
    return true;
}

}

void alignRegion(const Audio& audio, int sr, const Match& m, const Config& cfg, Audio& ref, Audio& y,
                 std::vector<char>& valid, std::string& guide, double& delay, double& ratePpm) {
    const long long source = static_cast<long long>(std::nearbyint(m.source * sr));
    const long long target = static_cast<long long>(std::nearbyint(m.target * sr));
    const long long n = std::min(static_cast<long long>(std::nearbyint(m.duration * sr)), static_cast<long long>(audio.frames()) - target);
    if (n < std::max(128, static_cast<int>(.15 * sr)) || source < 0 || target <= source || source + n > target)
        throw Error("invalid or overlapping region order");
    y = slicePad(audio, target, n);
    const Audio initial = slicePad(audio, source, n);
    guide = chooseGuide(initial, cfg.guide);
    if (cfg.guide == "auto" && (m.preferredGuide == "full" || m.preferredGuide == "side")) guide = m.preferredGuide;
    const Audio history = slicePad(audio, 0, target);
    const Audio rg = guideAudio(history, guide), yg = guideAudio(y, guide);
    const long long length = std::min<long long>(static_cast<long long>(sr * .45), std::max<long long>(128, n / 4));
    const double c0 = static_cast<double>(length / 2 + 25), c1 = static_cast<double>(n - length / 2 - 25);
    std::vector<long long> centers(7);
    for (int k = 0; k < 7; ++k) centers[static_cast<std::size_t>(k)] = static_cast<long long>(c0 + (c1 - c0) * k / 6.0);
    std::vector<double> offsets(7), quality(7);
    parallelFor(7, 1, [&](long long b, long long e) {
        for (long long k = b; k < e; ++k) {
            const long long begin = centers[static_cast<std::size_t>(k)] - length / 2;
            findDelay(rg, yg, source + begin, begin, length, static_cast<long long>(cfg.maxLag * sr), sr, offsets[static_cast<std::size_t>(k)],
                      quality[static_cast<std::size_t>(k)]);
        }
    });
    const double qmax = *std::max_element(quality.begin(), quality.end());
    std::vector<char> good(7);
    int ng = 0;
    for (int k = 0; k < 7; ++k) ng += good[static_cast<std::size_t>(k)] = quality[static_cast<std::size_t>(k)] >= std::max(.18, qmax * .55);
    if (ng < 2) {
        std::vector<double> sq = quality;
        std::sort(sq.begin(), sq.end());
        const double second = sq[5];
        for (int k = 0; k < 7; ++k) good[static_cast<std::size_t>(k)] = quality[static_cast<std::size_t>(k)] >= second;
    }
    std::vector<double> u, v, q;
    for (int k = 0; k < 7; ++k)
        if (good[static_cast<std::size_t>(k)]) {
            u.push_back(static_cast<double>(centers[static_cast<std::size_t>(k)]) - static_cast<double>(n) / 2);
            v.push_back(offsets[static_cast<std::size_t>(k)]);
            q.push_back(quality[static_cast<std::size_t>(k)]);
        }
    std::vector<double> w(q.size());
    for (std::size_t k = 0; k < q.size(); ++k) w[k] = std::pow(q[k], 4) + 1e-8;
    double c0f = median(v), c1f = 0;
    for (int it = 0; it < 5; ++it) {
        double a00 = 0, a01 = 0, a11 = 0, b0 = 0, b1 = 0;
        for (std::size_t k = 0; k < u.size(); ++k) {
            a00 += w[k];
            a01 += w[k] * u[k];
            a11 += w[k] * u[k] * u[k];
            b0 += w[k] * v[k];
            b1 += w[k] * u[k] * v[k];
        }
        double x0, x1;
        if (!solve2(a00, a01, a11, b0, b1, x0, x1)) {
            x0 = b0 / std::max(a00, kTiny);
            x1 = 0;
        }
        c0f = x0;
        c1f = x1;
        std::vector<double> e(u.size());
        for (std::size_t k = 0; k < u.size(); ++k) e[k] = v[k] - (c0f + c1f * u[k]);
        const double me = median(e);
        std::vector<double> dev(e.size());
        for (std::size_t k = 0; k < e.size(); ++k) dev[k] = std::abs(e[k] - me);
        const double scale = std::max(.12, 1.4826 * median(dev));
        for (std::size_t k = 0; k < u.size(); ++k) w[k] = (std::pow(q[k], 4) + 1e-8) * std::min(1.0, 1.5 * scale / (std::abs(e[k]) + 1e-9));
    }
    double rate = c1f;
    const bool limited = std::abs(rate) > cfg.maxRatePpm * 1e-6;
    int strong = 0;
    for (double x : quality) strong += x > .4;
    if (strong < 4) {
        c0f = offsets[static_cast<std::size_t>(std::max_element(quality.begin(), quality.end()) - quality.begin())];
        c1f = 0;
        rate = 0;
    }
    if (limited) {
        c0f = median(v);
        c1f = 0;
        rate = 0;
    }
    const double vmed = median(v);
    if (*std::max_element(v.begin(), v.end()) - *std::min_element(v.begin(), v.end()) < 1e-3 && std::abs(vmed - std::nearbyint(vmed)) < 1e-3) {
        c0f = std::nearbyint(vmed);
        c1f = 0;
        rate = 0;
    }
    std::vector<double> pos(static_cast<std::size_t>(n));
    valid.assign(static_cast<std::size_t>(n), 0);
    const double hlen = static_cast<double>(history.frames());
    for (long long i = 0; i < n; ++i) {
        const double p = static_cast<double>(source + i) + c0f + c1f * (static_cast<double>(i) - static_cast<double>(n) / 2);
        pos[static_cast<std::size_t>(i)] = p;
        valid[static_cast<std::size_t>(i)] = p >= 24 && p < hlen - 25;
        if (i > 0 && p <= pos[static_cast<std::size_t>(i - 1)]) throw Error("non-monotone reference map");
    }
    ref = sincSample(history, pos, 24);
    delay = c0f;
    ratePpm = rate * 1e6;
}

}
}
