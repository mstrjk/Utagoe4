#include "hp_grid.h"
#include "parallel.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace utagoe {
namespace hp {

Spec phaseSurface(const Spec& Y, const Spec& X, double sr, int nFft, int hop) {
    const CGrid ys = sideOf(Y), xs = sideOf(X);
    const int F = xs.F, T = xs.T;
    Grid power(F, T);
    for (std::size_t i = 0; i < power.v.size(); ++i) power.v[i] = std::norm(xs.v[i]);
    const int wide = std::max(4, static_cast<int>(2.0 * sr / hop));
    const int gap = std::max(2, static_cast<int>(0.12 * sr / hop));
    const double floorv = std::max(meanOf(power) * 1e-5, 1e-12);
    CGrid h(F, T, cd(1, 0));
    for (int it = 0; it < 3; ++it) {
        Grid errPow(F, T), wpow(F, T);
        CGrid wcross(F, T);
        std::vector<double> errAbs(power.v.size());
        for (std::size_t i = 0; i < power.v.size(); ++i) {
            const cd err = ys.v[i] - h.v[i] * xs.v[i];
            errAbs[i] = std::abs(err);
            errPow.v[i] = errAbs[i] * errAbs[i];
        }
        smooth(errPow, 2.0, std::max(2.0, 0.25 * sr / hop));
        for (std::size_t i = 0; i < power.v.size(); ++i) {
            const double scale = std::sqrt(errPow.v[i] + floorv);
            const double w = std::min(1.0, 1.5 * scale / (errAbs[i] + floorv));
            wpow.v[i] = w * power.v[i];
            wcross.v[i] = w * (ys.v[i] - xs.v[i]) * std::conj(xs.v[i]);
        }
        Grid p = donut(wpow, wide, gap);
        smooth(p, 1.2, 0);
        CGrid cross = donut(wcross, wide, gap);
        smooth(cross, 1.2, 0);
        const std::vector<double> pm = rowMean(p);
        for (int f = 0; f < F; ++f) {
            const double prior = 0.025 * std::max(pm[static_cast<std::size_t>(f)], floorv);
            for (int t = 0; t < T; ++t) {
                const cd dh = cross.at(f, t) / (p.at(f, t) + prior + floorv);
                h.at(f, t) = clipTransfer(1.0 + dh, 0.72, 1.35, 0.50);
            }
        }
    }
    Grid py(F, T);
    CGrid yx(F, T);
    for (std::size_t i = 0; i < power.v.size(); ++i) {
        py.v[i] = std::norm(ys.v[i]);
        yx.v[i] = ys.v[i] * std::conj(xs.v[i]);
    }
    Grid powerY = donut(py, wide, gap);
    smooth(powerY, 1.2, 0);
    Grid powerX = donut(power, wide, gap);
    smooth(powerX, 1.2, 0);
    CGrid cross = donut(yx, wide, gap);
    smooth(cross, 1.2, 0);
    const std::vector<double> pxm = rowMean(powerX);
    const std::vector<double> lb = lowBand(nFft, sr);
    for (int f = 0; f < F; ++f)
        for (int t = 0; t < T; ++t) {
            const double coh = std::clamp(std::norm(cross.at(f, t)) / (powerX.at(f, t) * powerY.at(f, t) + floorv), 0.0, 1.0);
            double rel = std::clamp((coh - 0.35) / 0.5, 0.0, 1.0);
            rel *= powerX.at(f, t) / (powerX.at(f, t) + 0.02 * pxm[static_cast<std::size_t>(f)] + floorv);
            rel *= lb[static_cast<std::size_t>(f)];
            h.at(f, t) = 1.0 + rel * (h.at(f, t) - 1.0);
        }
    return scaleBy(X, h);
}
}
}
