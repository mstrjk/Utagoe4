#ifndef UTAGOE_UPMIX_H
#define UTAGOE_UPMIX_H

#include <functional>
#include <string>
#include <vector>

namespace utagoe {
namespace upmix {

enum class Method { Angle = 0, Slice = 1, Hybrid = 2, FoldExact = 3, GuidedScene = 4, SceneVector = 5, ReferenceScene = 6, ContrastCov = 7, ModulationLock = 8, VocalAnchor = 9 };
constexpr int kMethods = 10;

const char* methodName(Method m);
bool needsReference(Method m);
bool supportsSevenOne(Method m);

struct Multi {
    int channels = 0;
    std::vector<double> v;
    std::vector<std::string> names;
};

using Progress = std::function<bool(double)>;

Multi specField(const std::vector<double>& stereo, int sr, Method m, const std::vector<double>* alignedReference, bool lfe,
                const Progress& progress, bool& cancelled, const std::vector<double>* vocal = nullptr);
Multi sceneVector(const std::vector<double>& stereo, int sr, bool sevenOne, double widthDeg, double ambience, bool lfe,
                  const Progress& progress, bool& cancelled);
Multi referenceScene(const std::vector<double>& bed, const std::vector<double>& novel, int sr, bool sevenOne, bool lfe,
                     const Progress& progress, bool& cancelled);

}
}

#endif
