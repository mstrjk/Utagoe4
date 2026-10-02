#include "cs_internal.h"

#include <algorithm>
#include <cmath>

namespace utagoe {
namespace cs {

CGrid coherence(const CGrid& L, const CGrid& R, Grid& maskOut) {
    maskOut = softCenterMask(L, R, 4.5, 2.0, 1.5, 1.0, 3.0);
    return maskedMid(L, R, maskOut);
}
}
}
