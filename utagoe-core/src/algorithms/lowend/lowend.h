#ifndef UTAGOE_LOWEND_H
#define UTAGOE_LOWEND_H

#include <functional>
#include <vector>

namespace utagoe {
namespace lowend {

struct Report {
    bool applied = false;
    int segments = 0;
    double lagSamples = 0, driftPpm = 0;
    double phase30 = 0, phase60 = 0, phase100 = 0, phase200 = 0;
    double gain30Db = 0, gain60Db = 0, gain100Db = 0;
    double predictedLeakDb = -300;
    bool rode = false;
    double ride5Db = 0, ride95Db = 0;
    const char* reason = "";
    bool cancelled = false;
};

Report match(const std::vector<float>& mix, const std::vector<float>& inst, int channels, int sr, std::vector<float>& out,
             const std::function<bool(double)>& progress = nullptr);
void subsonicCut(std::vector<float>& x, int channels, int sr);

}
}

#endif
