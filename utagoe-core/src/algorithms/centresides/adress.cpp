#include "cs_internal.h"

#include <algorithm>
#include <cmath>

namespace utagoe {
namespace cs {

CGrid adress(const CGrid& L, const CGrid& R, Grid& maskOut) {
    Grid mask(L.F, L.T);
    for (std::size_t i = 0; i < mask.v.size(); ++i) {
        const cd l = L.v[i], r = R.v[i];
        double k = (l * std::conj(r)).real() / (std::norm(r) + kEps);
        k = std::clamp(k, 1.0 / 64.0, 64.0);
        const double residual = std::abs(l - k * r) / (std::abs(l) + k * std::abs(r) + kEps);
        const double pan = std::exp(-0.5 * std::pow(std::abs(std::log2(k)) / 0.42, 2));
        const double null = std::exp(-0.5 * std::pow(residual / 0.24, 2));
        mask.v[i] = std::clamp(pan * null, 0.0, 1.0);
    }
    smooth(mask, 0.8, 1.2);
    maskOut = mask;
    return maskedMid(L, R, mask);
}
}
}
