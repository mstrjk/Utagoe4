#ifndef UTAGOE_CS_INTERNAL_H
#define UTAGOE_CS_INTERNAL_H

#include "cs.h"

#include <complex>
#include <vector>

namespace utagoe {
namespace cs {

using cd = std::complex<double>;
constexpr double kEps = 1e-12;
constexpr double kPi = 3.14159265358979323846;

struct Grid {
    int F = 0, T = 0;
    std::vector<double> v;
    Grid() = default;
    Grid(int f, int t) : F(f), T(t), v(static_cast<std::size_t>(f) * t, 0.0) {}
    double& at(int f, int t) { return v[static_cast<std::size_t>(f) * T + t]; }
    double at(int f, int t) const { return v[static_cast<std::size_t>(f) * T + t]; }
};

struct CGrid {
    int F = 0, T = 0;
    std::vector<cd> v;
    CGrid() = default;
    CGrid(int f, int t) : F(f), T(t), v(static_cast<std::size_t>(f) * t) {}
    cd& at(int f, int t) { return v[static_cast<std::size_t>(f) * T + t]; }
    const cd& at(int f, int t) const { return v[static_cast<std::size_t>(f) * T + t]; }
};

std::vector<double> kernel(double sigma);
void smooth(Grid& g, double sf, double st);
Grid smoothed(Grid g, double sf, double st);
double phaseAgreement(double ipd, double power);
Grid softCenterMask(const CGrid& L, const CGrid& R, double ildSigma, double phasePower, double cohPower, double sf, double st);
CGrid maskedMid(const CGrid& L, const CGrid& R, const Grid& mask);

std::vector<double> trueMs(const std::vector<float>& stereo);
CGrid phantom(const CGrid& L, const CGrid& R, Grid& maskOut);
CGrid coherence(const CGrid& L, const CGrid& R, Grid& maskOut);
CGrid adress(const CGrid& L, const CGrid& R, Grid& maskOut);
CGrid duet(const CGrid& L, const CGrid& R, double sr, int n, Grid& maskOut);
CGrid pca(const CGrid& L, const CGrid& R, Grid& maskOut);
CGrid prettyOne(const CGrid& L, const CGrid& R, Grid& maskOut);

}
}

#endif
