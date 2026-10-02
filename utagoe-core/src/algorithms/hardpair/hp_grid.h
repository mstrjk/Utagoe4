#ifndef UTAGOE_HP_GRID_H
#define UTAGOE_HP_GRID_H

#include "hp.h"

#include <vector>

namespace utagoe {
namespace hp {

constexpr double kPi = 3.14159265358979323846;

struct Grid {
    int F = 0, T = 0;
    std::vector<double> v;
    Grid() = default;
    Grid(int f, int t, double fill = 0.0) : F(f), T(t), v(static_cast<std::size_t>(f) * t, fill) {}
    double& at(int f, int t) { return v[static_cast<std::size_t>(f) * T + t]; }
    double at(int f, int t) const { return v[static_cast<std::size_t>(f) * T + t]; }
};

struct CGrid {
    int F = 0, T = 0;
    std::vector<cd> v;
    CGrid() = default;
    CGrid(int f, int t, cd fill = cd(0, 0)) : F(f), T(t), v(static_cast<std::size_t>(f) * t, fill) {}
    cd& at(int f, int t) { return v[static_cast<std::size_t>(f) * T + t]; }
    cd at(int f, int t) const { return v[static_cast<std::size_t>(f) * T + t]; }
};

CGrid sideOf(const Spec& z);
void smooth(Grid& g, double sf, double st);
void smooth(CGrid& g, double sf, double st);
Grid donut(const Grid& a, int wide, int gap);
CGrid donut(const CGrid& a, int wide, int gap);
cd clipTransfer(cd h, double gmin, double gmax, double pmax);
std::vector<double> lowBand(int nFft, double sr);
std::vector<double> rowMean(const Grid& g);
double meanOf(const Grid& g);
Spec scaleBy(const Spec& X, const CGrid& h);

}
}

#endif
