#ifndef UTAGOE_HP_H
#define UTAGOE_HP_H

#include "../refcancel/rc.h"

#include <array>
#include <complex>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace utagoe {
namespace hp {

using rc::Audio;
using rc::Spec;
using rc::cd;
using rc::cf;

enum class Method { Rational = 0, Surface = 1, Trend = 2, Ctf = 3, LowRank = 4 };

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct SplineStage {
    std::vector<double> knots;
    std::vector<double> coef;
    double start = 0, end = 0;
};

struct TimeMap {
    std::vector<SplineStage> stages;
    int polarity = 1;
    std::vector<std::array<double, 3>> coarse;
    std::vector<std::vector<std::array<double, 3>>> anchors;
    double positionAt(double sample, double sr) const;
};

struct PhaseModel {
    int order = 2;
    double breakHz = 25.0;
    double delay = 0.0;
    double gain = 1.0;
    double fitRms = 0.0;
};

struct Sos {
    std::vector<std::array<double, 6>> s;
};

std::vector<double> side(const Audio& a);
std::vector<double> boxSum(const std::vector<double>& x, int size, bool nearest);
void uniformFilterNearest(const double* in, double* out, long long n, int size);
void gaussianReflect(std::vector<double>& x, double sigma);
Sos butterBandpass(int order, double lo, double hi, double sr);
std::vector<double> sosfiltfilt(const Sos& sos, const std::vector<double>& x);
std::vector<double> resampleDown(const std::vector<double>& x, int factor);
std::vector<double> correlateValid(const std::vector<double>& r, const std::vector<double>& q);
std::vector<double> windowEnergyValid(const std::vector<double>& r, int win);
std::vector<double> hann(int n);
std::size_t nextFastLen(std::size_t n);
std::vector<cd> rfftAny(const std::vector<double>& x, std::size_t n);
std::vector<double> convolveFull(const std::vector<double>& x, const std::vector<double>& h, std::size_t from, std::size_t count);
std::vector<double> solveDense(std::vector<double> a, std::vector<double> b, int n);
void hermitianTopEigen(std::vector<cd>& g, int n, int k, std::vector<double>& values, std::vector<cd>& vectors);
double gradientAt(const std::vector<double>& x, const std::vector<double>& f, std::size_t i);
double medianOf(std::vector<double> v);
double quantileOf(std::vector<double> v, double q);
double interp(double x, const std::vector<double>& xp, const std::vector<double>& fp);

SplineStage fitSpline(const std::vector<double>& t, const std::vector<double>& d, const std::vector<double>& quality,
                      double duration, double spacing, double huber, double smooth);
double evalSpline(const SplineStage& s, double seconds);

TimeMap initialMap(const Audio& mix, const Audio& ref, double sr);
Audio warpReference(const Audio& ref, std::size_t length, const TimeMap& map, double sr, int taps = 96);
void refineMap(const Audio& mix, const Audio& warped, TimeMap& map, double sr, double spacing);
std::vector<double> sincTable(int taps, int phases, double cutoff);

struct PhaseSpectra {
    std::vector<double> f, coh, xx;
    std::vector<cd> h;
};
PhaseSpectra phaseSpectra(const Audio& mix, const Audio& ref, double sr, int nFft);
PhaseModel fitPhase(const Audio& mix, const Audio& ref, double sr);
Audio applyPhase(const Audio& ref, double sr, const PhaseModel& model);

Spec phaseSurface(const Spec& Y, const Spec& X, double sr, int nFft, int hop);
Spec ctf(const Spec& Y, const Spec& X, double sr, int nFft, int hop);
Spec lowrankField(const Spec& Y, const Spec& X, double sr, int nFft, int hop);
Audio trendDemaster(const Audio& mix, const Audio& base, double sr);

using Progress = std::function<bool(double, const std::string&)>;

struct Result {
    std::vector<std::pair<Method, Audio>> residuals;
    Audio aligned;
    Audio base;
    TimeMap map;
    PhaseModel phase;
    bool cancelled = false;
};

Audio prealignedReference(const Audio& mix, const Audio& instrumental);
Audio alignDense(const Audio& mix, const Audio& instrumental, double sr, TimeMap& map, const Progress& progress, bool& cancelled);
Result separate(const Audio& mix, const Audio& instrumental, double sr, const std::vector<Method>& methods,
                bool aligned, const Progress& progress);
std::vector<std::pair<Method, Audio>> render(const Audio& mix, const Audio& base, double sr,
                                             const std::vector<Method>& methods, const Progress& progress, bool& cancelled);

const char* methodName(Method m);

struct KickDuck {
    std::vector<float> gainDb;
    int kicks = 0;
    double depthDb = 0, onsetMs = 0, attackMs = 0, releaseMs = 0, strength = 0, changeDb = 0;
};
KickDuck kickDuck(const Audio& mix, const Audio& reference, double sr, double maxDepthDb = 3.0);
void applyGainDb(Audio& a, const std::vector<float>& gainDb);

}
}

#endif
