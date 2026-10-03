#include "upmix_internal.h"
#include "parallel.h"

#include <algorithm>
#include <cmath>

namespace utagoe {
namespace upmix {
namespace {

struct Layout {
    std::vector<std::string> names;
    std::vector<double> azimuth;
    std::vector<char> speaker;
};

Layout layoutOf(bool sevenOne) {
    if (sevenOne) return {{"FL", "FR", "C", "LFE", "BL", "BR", "SL", "SR"}, {-30, 30, 0, 0, -150, 150, -90, 90}, {1, 1, 1, 0, 1, 1, 1, 1}};
    return {{"FL", "FR", "C", "LFE", "SL", "SR"}, {-30, 30, 0, 0, -110, 110}, {1, 1, 1, 0, 1, 1}};
}

int indexOf(const Layout& lay, const char* name) {
    for (std::size_t i = 0; i < lay.names.size(); ++i)
        if (lay.names[i] == name) return static_cast<int>(i);
    return -1;
}

double circularDistance(double a, double b) {
    double v = std::fmod(a - b + 180.0, 360.0);
    if (v < 0) v += 360.0;
    return std::abs(v - 180.0);
}

std::vector<double> monoOf(const std::vector<double>& x) {
    const std::size_t n = x.size() / 2;
    std::vector<double> m(n);
    for (std::size_t i = 0; i < n; ++i) m[i] = 0.5 * (x[i * 2] + x[i * 2 + 1]);
    return m;
}

Multi sceneImpl(const std::vector<double>& xIn, int sr, const Layout& lay, double width, double ambience, bool renderLfe, bool lfe) {
    const int n = 4096, hop = 1024;
    const std::size_t len = xIn.size() / 2;
    std::vector<double> l = channelOf(xIn, 2, 0), r = channelOf(xIn, 2, 1);
    if (static_cast<int>(l.size()) < n) {
        l.resize(static_cast<std::size_t>(n), 0.0);
        r.resize(static_cast<std::size_t>(n), 0.0);
    }
    const CField L = stft(l, n, hop), R = stft(r, n, hop);
    const int F = L.F, T = L.T;
    const std::size_t cells = L.v.size();
    Field pll(F, T), prr(F, T), cre(F, T), cim(F, T);
    for (std::size_t i = 0; i < cells; ++i) {
        pll.v[i] = std::norm(L.v[i]);
        prr.v[i] = std::norm(R.v[i]);
        const cd c = L.v[i] * std::conj(R.v[i]);
        cre.v[i] = c.real();
        cim.v[i] = c.imag();
    }
    gaussianNearest(pll, 1.0, 2.8);
    gaussianNearest(prr, 1.0, 2.8);
    gaussianNearest(cre, 1.0, 2.8);
    gaussianNearest(cim, 1.0, 2.8);
    Field energy(F, T), flux(F, T);
    for (std::size_t i = 0; i < cells; ++i) energy.v[i] = std::log(std::sqrt(std::norm(L.v[i]) + std::norm(R.v[i])) + 1e-8);
    std::vector<double> positive;
    for (int f = 0; f < F; ++f)
        for (int t = 0; t < T; ++t) {
            const double d = t > 0 ? std::max(energy.at(f, t) - energy.at(f, t - 1), 0.0) : 0.0;
            flux.at(f, t) = d;
            if (d > 0) positive.push_back(d);
        }
    const double q95 = positive.empty() ? 1.0 : quantile(std::move(positive), 0.95);
    for (double& v : flux.v) v = std::clamp(v / (q95 + 1e-8), 0.0, 1.0);
    gaussianNearest(flux, 0.8, 1.2);
    const int C = static_cast<int>(lay.names.size());
    std::vector<CField> outz(static_cast<std::size_t>(C), CField(F, T));
    const int iFL = indexOf(lay, "FL"), iFR = indexOf(lay, "FR"), iSL = indexOf(lay, "SL"), iSR = indexOf(lay, "SR");
    const int iBL = indexOf(lay, "BL"), iBR = indexOf(lay, "BR");
    const double amb = std::clamp(ambience, 0.0, 1.0);
    parallelFor(static_cast<long long>(cells), 8192, [&](long long b, long long e) {
        std::vector<double> w(static_cast<std::size_t>(C));
        for (long long ii = b; ii < e; ++ii) {
            const std::size_t i = static_cast<std::size_t>(ii);
            const cd lv = L.v[i], rv = R.v[i];
            const double ml = std::abs(lv), mr = std::abs(rv);
            const double theta = std::atan2(mr + kEps, ml + kEps);
            const double angle = std::clamp(4.0 * theta / kPi - 1.0, -1.0, 1.0) * (width * 0.5);
            const double coh = std::clamp(std::sqrt(cre.v[i] * cre.v[i] + cim.v[i] * cim.v[i]) / std::sqrt(pll.v[i] * prr.v[i] + kEps), 0.0, 1.0);
            const double ipd = std::atan2(cim.v[i], cre.v[i]);
            const double agree = std::pow(std::clamp(std::cos(0.5 * ipd), 0.0, 1.0), 2);
            double stable = std::clamp(std::pow(coh, 1.2) * agree, 0.0, 1.0);
            stable = std::max(stable, 0.72 * flux.v[i]);
            double ss = 0;
            for (int c = 0; c < C; ++c) {
                if (!lay.speaker[static_cast<std::size_t>(c)]) { w[static_cast<std::size_t>(c)] = 0; continue; }
                const double d = circularDistance(angle, lay.azimuth[static_cast<std::size_t>(c)]);
                const double rr = std::clamp(std::cos(std::min(d, 90.0) * kPi / 180.0), 0.0, 1.0);
                w[static_cast<std::size_t>(c)] = std::pow(rr, 2.8) + 1e-9;
                ss += w[static_cast<std::size_t>(c)] * w[static_cast<std::size_t>(c)];
            }
            ss = std::sqrt(ss + 1e-15);
            const cd mid = 0.5 * (lv + rv), dominant = ml >= mr ? lv : rv;
            const cd carrier = stable * mid + (1.0 - stable) * dominant;
            for (int c = 0; c < C; ++c)
                if (lay.speaker[static_cast<std::size_t>(c)]) outz[static_cast<std::size_t>(c)].v[i] += (w[static_cast<std::size_t>(c)] / ss) * stable * carrier;
            const double diffuse = std::clamp(1.0 - stable, 0.0, 1.0) * amb;
            const cd side = 0.5 * (lv - rv);
            if (iSL >= 0) {
                outz[static_cast<std::size_t>(iSL)].v[i] += diffuse * side;
                outz[static_cast<std::size_t>(iSR)].v[i] -= diffuse * side;
            }
            if (iBL >= 0) {
                outz[static_cast<std::size_t>(iBL)].v[i] += 0.72 * diffuse * side;
                outz[static_cast<std::size_t>(iBR)].v[i] -= 0.72 * diffuse * side;
            }
            const double safety = 0.18 * (1.0 - stable);
            outz[static_cast<std::size_t>(iFL)].v[i] += safety * lv;
            outz[static_cast<std::size_t>(iFR)].v[i] += safety * rv;
        }
    });
    Multi m;
    m.channels = C;
    m.names = lay.names;
    m.v.assign(len * static_cast<std::size_t>(C), 0.0);
    for (int c = 0; c < C; ++c) {
        if (!lay.speaker[static_cast<std::size_t>(c)]) continue;
        const std::vector<double> y = istft(outz[static_cast<std::size_t>(c)], n, hop, len);
        for (std::size_t i = 0; i < len; ++i) m.v[i * static_cast<std::size_t>(C) + static_cast<std::size_t>(c)] = y[i];
    }
    if (renderLfe && lfe) {
        const std::vector<double> lf = butterLowpassLfe(monoOf(xIn), sr, 90.0, -10.0);
        const int iL = indexOf(lay, "LFE");
        for (std::size_t i = 0; i < len; ++i) m.v[i * static_cast<std::size_t>(C) + static_cast<std::size_t>(iL)] = lf[i];
    }
    return m;
}

}

Multi sceneVector(const std::vector<double>& x, int sr, bool sevenOne, double width, double ambience, bool lfe, const Progress& progress, bool& cancelled) {
    cancelled = false;
    const Layout lay = layoutOf(sevenOne);
    const std::size_t len = x.size() / 2;
    const std::size_t blockN = static_cast<std::size_t>(12.0 * sr), context = static_cast<std::size_t>(1.5 * sr);
    if (len <= blockN) {
        Multi m = sceneImpl(x, sr, lay, width, ambience, true, lfe);
        if (progress) progress(1.0);
        return m;
    }
    const int C = static_cast<int>(lay.names.size());
    Multi out;
    out.channels = C;
    out.names = lay.names;
    out.v.assign(len * static_cast<std::size_t>(C), 0.0);
    const std::size_t blocks = (len + blockN - 1) / blockN;
    for (std::size_t bi = 0; bi < blocks; ++bi) {
        if (progress && !progress(static_cast<double>(bi) / blocks)) {
            cancelled = true;
            return {};
        }
        const std::size_t coreStart = bi * blockN, coreEnd = std::min(len, coreStart + blockN);
        const std::size_t segStart = coreStart > context ? coreStart - context : 0, segEnd = std::min(len, coreEnd + context);
        const std::vector<double> seg(x.begin() + static_cast<std::ptrdiff_t>(segStart * 2), x.begin() + static_cast<std::ptrdiff_t>(segEnd * 2));
        const Multi r = sceneImpl(seg, sr, lay, width, ambience, false, lfe);
        for (std::size_t i = coreStart; i < coreEnd; ++i)
            for (int c = 0; c < C; ++c)
                out.v[i * static_cast<std::size_t>(C) + static_cast<std::size_t>(c)] = r.v[(i - segStart) * static_cast<std::size_t>(C) + static_cast<std::size_t>(c)];
    }
    if (lfe) {
        const std::vector<double> lf = butterLowpassLfe(monoOf(x), sr, 90.0, -10.0);
        const int iL = indexOf(lay, "LFE");
        for (std::size_t i = 0; i < len; ++i) out.v[i * static_cast<std::size_t>(C) + static_cast<std::size_t>(iL)] = lf[i];
    }
    if (progress) progress(1.0);
    return out;
}

Multi referenceScene(const std::vector<double>& bed, const std::vector<double>& novel, int sr, bool sevenOne, bool lfe, const Progress& progress,
                     bool& cancelled) {
    Multi out = sceneVector(bed, sr, sevenOne, 310.0, 0.52, lfe, progress, cancelled);
    if (cancelled) return out;
    const Layout lay = layoutOf(sevenOne);
    const int C = out.channels;
    const int iFL = indexOf(lay, "FL"), iFR = indexOf(lay, "FR"), iC = indexOf(lay, "C"), iSL = indexOf(lay, "SL"), iSR = indexOf(lay, "SR");
    const int iBL = indexOf(lay, "BL"), iBR = indexOf(lay, "BR"), iL = indexOf(lay, "LFE");
    const std::size_t len = std::min(out.v.size() / static_cast<std::size_t>(C), novel.size() / 2);
    const std::vector<double> lf = lfe ? butterLowpassLfe(monoOf(novel), sr, 90.0, -10.0) : std::vector<double>(len, 0.0);
    for (std::size_t i = 0; i < len; ++i) {
        const double l = novel[i * 2], r = novel[i * 2 + 1];
        const double m = 0.5 * (l + r), s = 0.5 * (l - r);
        const bool agree = l * r > 0;
        const double sgn = m > 0 ? 1.0 : (m < 0 ? -1.0 : 0.0);
        const double c = 0.82 * (agree ? std::min(std::abs(l), std::abs(r)) * sgn : 0.0);
        double* d = &out.v[i * static_cast<std::size_t>(C)];
        d[iC] += c;
        d[iFL] += l - c;
        d[iFR] += r - c;
        const double amb = 0.06 * s;
        d[iSL] += amb;
        d[iSR] -= amb;
        if (iBL >= 0) {
            d[iBL] += 0.7 * amb;
            d[iBR] -= 0.7 * amb;
        }
        d[iL] += lf[i];
    }
    return out;
}

}
}
