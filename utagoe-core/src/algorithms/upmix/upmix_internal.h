#ifndef UTAGOE_UPMIX_INTERNAL_H
#define UTAGOE_UPMIX_INTERNAL_H

#include "upmix.h"

#include <complex>
#include <vector>

namespace utagoe {
namespace upmix {

using cd = std::complex<double>;
constexpr double kEps = 1e-12;
constexpr double kPi = 3.14159265358979323846;
constexpr double kDown = 0.70710678118654752440;

struct Field {
    int F = 0, T = 0;
    std::vector<double> v;
    Field() = default;
    Field(int f, int t, double fill = 0.0) : F(f), T(t), v(static_cast<std::size_t>(f) * t, fill) {}
    double& at(int f, int t) { return v[static_cast<std::size_t>(f) * T + t]; }
    double at(int f, int t) const { return v[static_cast<std::size_t>(f) * T + t]; }
};

struct CField {
    int F = 0, T = 0;
    std::vector<cd> v;
    CField() = default;
    CField(int f, int t) : F(f), T(t), v(static_cast<std::size_t>(f) * t) {}
    cd& at(int f, int t) { return v[static_cast<std::size_t>(f) * T + t]; }
    const cd& at(int f, int t) const { return v[static_cast<std::size_t>(f) * T + t]; }
};

struct StereoSpec {
    CField l, r;
};

std::vector<double> channelOf(const std::vector<double>& interleaved, int channels, int c);
CField stft(const std::vector<double>& x, int n, int hop);
std::vector<double> istft(const CField& z, int n, int hop, std::size_t length);
std::vector<double> resamplePoly(const std::vector<double>& x, int up, int down, double beta);
void boxSmooth(Field& a, int freqBins, int timeFrames);
void boxSmooth(CField& a, int freqBins, int timeFrames);
void gaussianNearest(Field& a, double sigmaF, double sigmaT);
double quantile(std::vector<double> v, double q);
double median(std::vector<double> v);
std::vector<double> phasePreservingLfe(const std::vector<double>& mid, int sr, double gainDb);
std::vector<double> butterLowpassLfe(const std::vector<double>& mono, int sr, double cutoffHz, double gainDb);

}
}

#endif
