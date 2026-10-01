#ifndef UTAGOE_CS_H
#define UTAGOE_CS_H

#include <functional>
#include <string>
#include <vector>

namespace utagoe {
namespace cs {

enum class Method { TrueMs = 0, Phantom = 1, Coherence = 2, Adress = 3, Duet = 4, Pca = 5, Pretty = 6 };

constexpr int kMethods = 7;

using Progress = std::function<bool(double)>;

struct Split {
    std::vector<double> center;
    double maskMean = 0.0;
    bool cancelled = false;
};

Split centerOf(const std::vector<float>& stereo, double sr, Method m, const Progress& progress = nullptr);
const char* methodName(Method m);

}
}

#endif
