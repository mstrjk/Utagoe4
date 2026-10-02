#include "cs_internal.h"

#include <algorithm>
#include <cmath>

namespace utagoe {
namespace cs {

CGrid pca(const CGrid& L, const CGrid& R, Grid& maskOut) {
    const int F = L.F, T = L.T;
    Grid a(F, T), d(F, T), re(F, T), im(F, T);
    for (std::size_t i = 0; i < a.v.size(); ++i) {
        a.v[i] = std::norm(L.v[i]);
        d.v[i] = std::norm(R.v[i]);
        const cd x = L.v[i] * std::conj(R.v[i]);
        re.v[i] = x.real();
        im.v[i] = x.imag();
    }
    smooth(a, 1.2, 3.0);
    smooth(d, 1.2, 3.0);
    smooth(re, 1.2, 3.0);
    smooth(im, 1.2, 3.0);
    Grid mask(F, T);
    for (std::size_t i = 0; i < mask.v.size(); ++i) {
        const cd b(re.v[i], im.v[i]);
        const double tr = a.v[i] + d.v[i];
        const double disc = std::sqrt(std::max((a.v[i] - d.v[i]) * (a.v[i] - d.v[i]) + 4.0 * std::norm(b), 0.0));
        const double lam1 = 0.5 * (tr + disc), lam2 = 0.5 * (tr - disc);
        cd v0 = b, v1 = cd(lam1 - a.v[i], 0.0);
        const double nrm = std::sqrt(std::norm(v0) + std::norm(v1) + kEps);
        v0 /= nrm;
        v1 /= nrm;
        if (nrm < 1e-8) { v0 = cd(1.0, 0.0); v1 = cd(0.0, 0.0); }
        const double magBalance = std::clamp(2.0 * std::abs(v0) * std::abs(v1), 0.0, 1.0);
        const double orientation = std::clamp(magBalance * phaseAgreement(std::arg(v0 * std::conj(v1)), 2.0), 0.0, 1.0);
        const double dominance = std::clamp((lam1 - lam2) / (lam1 + lam2 + kEps), 0.0, 1.0);
        mask.v[i] = std::clamp(std::pow(orientation, 2.2) * std::pow(dominance, 0.65), 0.0, 1.0);
    }
    smooth(mask, 0.6, 1.0);
    maskOut = mask;
    return maskedMid(L, R, mask);
}
}
}
