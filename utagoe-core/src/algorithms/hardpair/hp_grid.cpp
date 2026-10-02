#include "hp_grid.h"
#include "parallel.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace utagoe {
namespace hp {

CGrid sideOf(const Spec& z) {
    CGrid s(z.F, z.T);
    for (int f = 0; f < z.F; ++f)
        for (int t = 0; t < z.T; ++t) s.at(f, t) = (cd(z.at(f, t, 0)) - cd(z.at(f, t, 1))) * 0.5;
    return s;
}

void smooth(Grid& g, double sf, double st) {
    if (sf > 0) rc::gaussian(g.v.data(), g.F, g.T, 1, 0, sf);
    if (st > 0) rc::gaussian(g.v.data(), g.F, g.T, 1, 1, st);
}

void smooth(CGrid& g, double sf, double st) {
    if (sf > 0) rc::gaussianComplex(g.v.data(), g.F, g.T, 1, 0, sf);
    if (st > 0) rc::gaussianComplex(g.v.data(), g.F, g.T, 1, 1, st);
}

Grid donut(const Grid& a, int wide, int gap) {
    Grid out(a.F, a.T);
    parallelFor(a.F, 16, [&](long long b, long long e) {
        std::vector<double> row(static_cast<std::size_t>(a.T));
        for (long long f = b; f < e; ++f) {
            for (int t = 0; t < a.T; ++t) row[static_cast<std::size_t>(t)] = a.at(static_cast<int>(f), t);
            const std::vector<double> big = boxSum(row, 2 * wide + 1, false), small = boxSum(row, 2 * gap + 1, false);
            for (int t = 0; t < a.T; ++t) out.at(static_cast<int>(f), t) = big[static_cast<std::size_t>(t)] - small[static_cast<std::size_t>(t)];
        }
    });
    return out;
}

CGrid donut(const CGrid& a, int wide, int gap) {
    Grid re(a.F, a.T), im(a.F, a.T);
    for (std::size_t i = 0; i < a.v.size(); ++i) { re.v[i] = a.v[i].real(); im.v[i] = a.v[i].imag(); }
    const Grid r = donut(re, wide, gap), m = donut(im, wide, gap);
    CGrid out(a.F, a.T);
    for (std::size_t i = 0; i < a.v.size(); ++i) out.v[i] = cd(r.v[i], m.v[i]);
    return out;
}

cd clipTransfer(cd h, double gmin, double gmax, double pmax) {
    return std::polar(std::clamp(std::abs(h), gmin, gmax), std::clamp(std::arg(h), -pmax, pmax));
}

std::vector<double> lowBand(int nFft, double sr) {
    std::vector<double> w(static_cast<std::size_t>(nFft / 2 + 1));
    for (std::size_t k = 0; k < w.size(); ++k) w[k] = std::clamp((static_cast<double>(k) * sr / nFft - 60.0) / 100.0, 0.0, 1.0);
    return w;
}

std::vector<double> rowMean(const Grid& g) {
    std::vector<double> m(static_cast<std::size_t>(g.F), 0.0);
    for (int f = 0; f < g.F; ++f) {
        double s = 0;
        for (int t = 0; t < g.T; ++t) s += g.at(f, t);
        m[static_cast<std::size_t>(f)] = s / g.T;
    }
    return m;
}

double meanOf(const Grid& g) {
    double s = 0;
    for (double v : g.v) s += v;
    return s / static_cast<double>(g.v.size());
}

Spec scaleBy(const Spec& X, const CGrid& h) {
    Spec out = X;
    for (int f = 0; f < X.F; ++f)
        for (int t = 0; t < X.T; ++t)
            for (int c = 0; c < X.C; ++c) out.at(f, t, c) = cf(cd(X.at(f, t, c)) * h.at(f, t));
    return out;
}
}
}
