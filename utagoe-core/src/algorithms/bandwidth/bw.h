#ifndef UTAGOE_BW_H
#define UTAGOE_BW_H

#include <cstddef>
#include <vector>

namespace utagoe {
namespace bw {

struct Profile {
    double sr = 0;
    int nFft = 0, hop = 0;
    std::vector<double> cutoffHz;
    std::vector<double> levelDb;
    std::vector<char> active;
    double cliffShare = 0;
    double evidenceShare = 1;
    double typicalHz = 0;
};

enum class Worse { None = 0, Original = 1, Instrumental = 2 };

struct Source {
    bool lossy = false;
    int kbps = 0;
};

struct Report {
    Worse worse = Worse::None;
    double originalHz = 0, instrumentalHz = 0;
    double originalShare = 0, instrumentalShare = 0;
    double lagSeconds = 0, lagScore = 0;
    double curveMinHz = 0, curveMaxHz = 0;
    double removedDb = -300;
};

Profile analyse(const std::vector<float>& interleaved, int channels, double sr);
long long estimateLag(const Profile& original, const Profile& instrumental, double& score);
Report decide(const Profile& original, const Profile& instrumental, const Source& originalSource = {}, const Source& instrumentalSource = {});
std::vector<double> envelope(const Profile& worse, double windowSeconds);
std::vector<double> curveFor(const Profile& worse, const Profile& better, Worse which, long long lag);
double lowpassToCurve(std::vector<float>& interleaved, int channels, double sr, const std::vector<double>& curveHz, int nFft, int hop);
Report match(std::vector<float>& original, std::vector<float>& instrumental, int channels, double sr);

}
}

#endif
