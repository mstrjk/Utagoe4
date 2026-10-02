#include "cs_internal.h"

#include <algorithm>
#include <cmath>

namespace utagoe {
namespace cs {

std::vector<double> trueMs(const std::vector<float>& x) {
    const std::size_t len = x.size() / 2;
    std::vector<double> center(len);
    for (std::size_t i = 0; i < len; ++i) center[i] = 0.5 * (static_cast<double>(x[2 * i]) + x[2 * i + 1]);
    return center;
}

}
}
