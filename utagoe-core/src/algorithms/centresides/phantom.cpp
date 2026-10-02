#include "cs_internal.h"

#include <algorithm>
#include <cmath>

namespace utagoe {
namespace cs {

CGrid phantom(const CGrid& L, const CGrid& R, Grid& maskOut) {
    Grid agree(L.F, L.T);
    for (std::size_t i = 0; i < agree.v.size(); ++i) agree.v[i] = phaseAgreement(std::arg(L.v[i] * std::conj(R.v[i])), 4.0);
    smooth(agree, 0.7, 1.5);
    CGrid c(L.F, L.T);
    for (std::size_t i = 0; i < c.v.size(); ++i) {
        const double mag = std::min(std::abs(L.v[i]), std::abs(R.v[i])) * agree.v[i];
        c.v[i] = std::polar(mag, std::arg(L.v[i] + R.v[i] + kEps));
    }
    maskOut = std::move(agree);
    return c;
}
}
}
