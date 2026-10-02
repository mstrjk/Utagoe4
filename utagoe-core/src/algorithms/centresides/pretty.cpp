#include "cs_internal.h"

#include <algorithm>
#include <cmath>

namespace utagoe {
namespace cs {

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
}
}
