#include "cs_internal.h"

#include <algorithm>
#include <cmath>

namespace utagoe {
namespace cs {

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
}
}
