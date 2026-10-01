#include "hp.h"
#include "../parallel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

namespace utagoe {
namespace hp {
namespace {

constexpr double kPi = 3.14159265358979323846;

std::size_t pow2At(std::size_t n) {
    std::size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

}

std::vector<double> side(const Audio& a) {
    if (a.channels != 2) throw Error("this protected implementation requires stereo inputs");
    const std::size_t n = a.frames();
    std::vector<double> s(n);
    for (std::size_t i = 0; i < n; ++i) s[i] = (static_cast<double>(a.at(i, 0)) - static_cast<double>(a.at(i, 1))) * 0.5;
    return s;
}

std::vector<double> boxSum(const std::vector<double>& x, int size, bool nearest) {
    const long long n = static_cast<long long>(x.size());
    std::vector<double> out(x.size(), 0.0);
    if (n == 0) return out;
    const long long left = size / 2, right = size - 1 - size / 2;
    auto at = [&](long long i) -> double {
        if (i < 0) return nearest ? x[0] : 0.0;
        if (i >= n) return nearest ? x[static_cast<std::size_t>(n - 1)] : 0.0;
        return x[static_cast<std::size_t>(i)];
    };
    double acc = 0.0;
    for (long long j = -left; j <= right; ++j) acc += at(j);
    out[0] = acc;
    for (long long i = 1; i < n; ++i) {
        acc += at(i + right) - at(i - 1 - left);
        out[static_cast<std::size_t>(i)] = acc;
    }
    return out;
}

void uniformFilterNearest(const double* in, double* out, long long n, int size) {
    if (n <= 0) return;
    const long long left = size / 2, right = size - 1 - size / 2;
    auto at = [&](long long i) -> double { return in[std::clamp<long long>(i, 0, n - 1)]; };
    double acc = 0.0;
    for (long long j = -left; j <= right; ++j) acc += at(j);
    out[0] = acc / size;
    for (long long i = 1; i < n; ++i) {
        acc += at(i + right) - at(i - 1 - left);
        out[i] = acc / size;
    }
}

void gaussianReflect(std::vector<double>& x, double sigma) {
    const long long n = static_cast<long long>(x.size());
    if (n == 0 || sigma <= 0) return;
    const int r = static_cast<int>(4.0 * sigma + 0.5);
    std::vector<double> k(static_cast<std::size_t>(2 * r + 1));
    double sum = 0.0;
    for (int i = -r; i <= r; ++i) {
        k[static_cast<std::size_t>(i + r)] = std::exp(-0.5 / (sigma * sigma) * i * i);
        sum += k[static_cast<std::size_t>(i + r)];
    }
    for (double& v : k) v /= sum;
    auto reflect = [n](long long i) {
        const long long period = 2 * n;
        i %= period;
        if (i < 0) i += period;
        return i < n ? i : period - 1 - i;
    };
    std::vector<double> src = x;
    for (long long i = 0; i < n; ++i) {
        double acc = 0.0;
        for (int j = -r; j <= r; ++j) acc += src[static_cast<std::size_t>(reflect(i + j))] * k[static_cast<std::size_t>(j + r)];
        x[static_cast<std::size_t>(i)] = acc;
    }
}

Sos butterBandpass(int order, double lo, double hi, double sr) {
    const double w1 = 4.0 * std::tan(kPi * (2.0 * lo / sr) / 2.0);
    const double w2 = 4.0 * std::tan(kPi * (2.0 * hi / sr) / 2.0);
    const double bw = w2 - w1, wo = std::sqrt(w1 * w2);
    std::vector<cd> pl;
    for (int m = -order + 1; m < order; m += 2) pl.push_back(-std::exp(cd(0, kPi * m / (2.0 * order))));
    std::vector<cd> pb;
    for (const cd& p : pl) {
        const cd ps = p * (bw / 2.0);
        const cd root = std::sqrt(ps * ps - wo * wo);
        pb.push_back(ps + root);
        pb.push_back(ps - root);
    }
    double kbp = std::pow(bw, order);
    cd num(1, 0), den(1, 0);
    for (int i = 0; i < order; ++i) num *= cd(4.0, 0);
    std::vector<cd> pz;
    for (const cd& p : pb) {
        den *= (4.0 - p);
        pz.push_back((4.0 + p) / (4.0 - p));
    }
    const double kz = kbp * (num / den).real();
    std::vector<cd> left;
    for (const cd& p : pz)
        if (p.imag() >= -1e-12 * std::abs(p)) left.push_back(std::abs(p.imag()) <= 1e-12 * std::abs(p) ? cd(p.real(), 0.0) : p);
    int pos = order, neg = order;
    auto takeZero = [&](const cd& p) {
        double z;
        if (pos == 0) z = -1.0;
        else if (neg == 0) z = 1.0;
        else z = std::abs(p - 1.0) <= std::abs(p + 1.0) ? 1.0 : -1.0;
        (z > 0 ? pos : neg)--;
        return z;
    };
    std::vector<std::array<double, 6>> rev;
    while (!left.empty()) {
        std::size_t i1 = 0;
        for (std::size_t i = 1; i < left.size(); ++i)
            if (1.0 - std::abs(left[i]) < 1.0 - std::abs(left[i1])) i1 = i;
        const cd p1 = left[i1];
        left.erase(left.begin() + static_cast<std::ptrdiff_t>(i1));
        double a1, a2;
        if (p1.imag() != 0.0) {
            a1 = -2.0 * p1.real();
            a2 = std::norm(p1);
        } else {
            std::ptrdiff_t i2 = -1;
            for (std::size_t i = 0; i < left.size(); ++i)
                if (left[i].imag() == 0.0 && (i2 < 0 || std::abs(left[i] - p1) < std::abs(left[static_cast<std::size_t>(i2)] - p1)))
                    i2 = static_cast<std::ptrdiff_t>(i);
            if (i2 >= 0) {
                const double p2 = left[static_cast<std::size_t>(i2)].real();
                left.erase(left.begin() + i2);
                a1 = -(p1.real() + p2);
                a2 = p1.real() * p2;
            } else {
                a1 = -p1.real();
                a2 = 0.0;
            }
        }
        const double z1 = takeZero(p1);
        const double z2 = (pos + neg > 0 && a2 != 0.0) ? takeZero(p1) : 0.0;
        if (a2 == 0.0) rev.push_back({1.0, -z1, 0.0, 1.0, a1, 0.0});
        else rev.push_back({1.0, -(z1 + z2), z1 * z2, 1.0, a1, a2});
    }
    Sos s;
    s.s.assign(rev.rbegin(), rev.rend());
    for (int k = 0; k < 3; ++k) s.s.front()[k] *= kz;
    return s;
}

namespace {

std::array<double, 2> lfilterZi(const std::array<double, 6>& sec) {
    const double b0 = sec[0], b1 = sec[1], b2 = sec[2], a1 = sec[4], a2 = sec[5];
    const double B0 = b1 - a1 * b0, B1 = b2 - a2 * b0;
    const double m00 = 1 + a1, m01 = -1, m10 = a2, m11 = 1;
    const double det = m00 * m11 - m01 * m10;
    return {(B0 * m11 - m01 * B1) / det, (m00 * B1 - m10 * B0) / det};
}

void sosfilt(const Sos& sos, std::vector<double>& x, std::vector<std::array<double, 2>> zi) {
    for (std::size_t k = 0; k < sos.s.size(); ++k) {
        const auto& c = sos.s[k];
        double z0 = zi[k][0], z1 = zi[k][1];
        for (double& v : x) {
            const double in = v;
            const double y = c[0] * in + z0;
            z0 = c[1] * in - c[4] * y + z1;
            z1 = c[2] * in - c[5] * y;
            v = y;
        }
    }
}

}

std::vector<double> sosfiltfilt(const Sos& sos, const std::vector<double>& x) {
    const long long n = static_cast<long long>(x.size());
    int ntaps = static_cast<int>(2 * sos.s.size() + 1);
    int zb = 0, za = 0;
    for (const auto& c : sos.s) { zb += c[2] == 0.0; za += c[5] == 0.0; }
    ntaps -= std::min(zb, za);
    const long long edge = std::min<long long>(3LL * ntaps, n - 1);
    std::vector<double> ext;
    ext.reserve(static_cast<std::size_t>(n + 2 * edge));
    for (long long i = edge; i >= 1; --i) ext.push_back(2 * x[0] - x[static_cast<std::size_t>(i)]);
    ext.insert(ext.end(), x.begin(), x.end());
    for (long long i = 1; i <= edge; ++i) ext.push_back(2 * x[static_cast<std::size_t>(n - 1)] - x[static_cast<std::size_t>(n - 1 - i)]);
    std::vector<std::array<double, 2>> zi;
    double scale = 1.0;
    for (const auto& c : sos.s) {
        auto z = lfilterZi(c);
        zi.push_back({z[0] * scale, z[1] * scale});
        scale *= (c[0] + c[1] + c[2]) / (c[3] + c[4] + c[5]);
    }
    auto zx = zi;
    for (auto& z : zx) { z[0] *= ext.front(); z[1] *= ext.front(); }
    sosfilt(sos, ext, zx);
    std::reverse(ext.begin(), ext.end());
    auto zy = zi;
    for (auto& z : zy) { z[0] *= ext.front(); z[1] *= ext.front(); }
    sosfilt(sos, ext, zy);
    std::reverse(ext.begin(), ext.end());
    return std::vector<double>(ext.begin() + edge, ext.begin() + edge + n);
}

std::vector<double> resampleDown(const std::vector<double>& x, int factor) {
    if (factor <= 1) return x;
    const long long nIn = static_cast<long long>(x.size());
    const int halfLen = 10 * factor;
    const int taps = 2 * halfLen + 1;
    const double fc = 1.0 / factor;
    const double alpha = (taps - 1) / 2.0;
    std::vector<double> h(static_cast<std::size_t>(taps));
    double s = 0.0;
    const double i0b = rc::besselI0(5.0);
    for (int i = 0; i < taps; ++i) {
        const double m = i - alpha;
        const double v = fc * m * kPi;
        const double sinc = m == 0 ? 1.0 : std::sin(v) / v;
        const double r = (i - alpha) / alpha;
        const double w = rc::besselI0(5.0 * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b;
        h[static_cast<std::size_t>(i)] = fc * sinc * w;
        s += h[static_cast<std::size_t>(i)];
    }
    for (double& v : h) v /= s;
    const int down = factor;
    const long long nOut = nIn / down + (nIn % down != 0);
    const int nPre = down - halfLen % down;
    const long long nPreRemove = (halfLen + nPre) / down;
    std::vector<double> hp(static_cast<std::size_t>(nPre), 0.0);
    hp.insert(hp.end(), h.begin(), h.end());
    std::vector<double> y(static_cast<std::size_t>(nOut));
    parallelFor(nOut, 4096, [&](long long b, long long e) {
        for (long long k = b; k < e; ++k) {
            const long long m = (k + nPreRemove) * down;
            double acc = 0.0;
            const long long jlo = std::max<long long>(0, m - (nIn - 1));
            const long long jhi = std::min<long long>(static_cast<long long>(hp.size()) - 1, m);
            for (long long j = jlo; j <= jhi; ++j) acc += hp[static_cast<std::size_t>(j)] * x[static_cast<std::size_t>(m - j)];
            y[static_cast<std::size_t>(k)] = acc;
        }
    });
    return y;
}

std::vector<double> correlateValid(const std::vector<double>& r, const std::vector<double>& q) {
    const std::size_t nr = r.size(), nq = q.size();
    if (nq > nr || nq == 0) return {};
    const std::size_t n = pow2At(nr + nq - 1);
    std::vector<cd> a(n, cd(0, 0)), b(n, cd(0, 0));
    for (std::size_t i = 0; i < nr; ++i) a[i] = cd(r[i], 0);
    for (std::size_t i = 0; i < nq; ++i) b[i] = cd(q[nq - 1 - i], 0);
    rc::fftDouble(a, false);
    rc::fftDouble(b, false);
    for (std::size_t i = 0; i < n; ++i) a[i] *= b[i];
    rc::fftDouble(a, true);
    std::vector<double> out(nr - nq + 1);
    for (std::size_t k = 0; k < out.size(); ++k) out[k] = a[k + nq - 1].real();
    return out;
}

std::vector<double> windowEnergyValid(const std::vector<double>& r, int win) {
    if (static_cast<std::size_t>(win) > r.size()) return {};
    std::vector<double> out(r.size() - static_cast<std::size_t>(win) + 1);
    double acc = 0.0;
    for (int i = 0; i < win; ++i) acc += r[static_cast<std::size_t>(i)] * r[static_cast<std::size_t>(i)];
    out[0] = acc;
    for (std::size_t k = 1; k < out.size(); ++k) {
        acc += r[k + static_cast<std::size_t>(win) - 1] * r[k + static_cast<std::size_t>(win) - 1] - r[k - 1] * r[k - 1];
        out[k] = acc;
    }
    return out;
}

std::vector<double> hann(int n) {
    std::vector<double> w(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) w[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / n);
    return w;
}

std::size_t nextFastLen(std::size_t n) {
    for (std::size_t m = n;; ++m) {
        std::size_t r = m;
        for (std::size_t p : {2, 3, 5, 7, 11})
            while (r % p == 0) r /= p;
        if (r == 1) return m;
    }
}

std::vector<cd> rfftAny(const std::vector<double>& x, std::size_t n) {
    const std::size_t half = n / 2 + 1;
    std::vector<cd> out(half);
    if ((n & (n - 1)) == 0) {
        std::vector<cd> a(n, cd(0, 0));
        for (std::size_t i = 0; i < std::min(n, x.size()); ++i) a[i] = cd(x[i], 0);
        rc::fftDouble(a, false);
        std::copy(a.begin(), a.begin() + static_cast<std::ptrdiff_t>(half), out.begin());
        return out;
    }
    const std::size_t m = pow2At(2 * n - 1);
    std::vector<cd> chirp(n);
    for (std::size_t j = 0; j < n; ++j) {
        const unsigned long long jj = (static_cast<unsigned long long>(j) * j) % (2ULL * n);
        chirp[j] = std::polar(1.0, -kPi * static_cast<double>(jj) / static_cast<double>(n));
    }
    std::vector<cd> a(m, cd(0, 0)), b(m, cd(0, 0));
    for (std::size_t j = 0; j < std::min(n, x.size()); ++j) a[j] = x[j] * chirp[j];
    b[0] = std::conj(chirp[0]);
    for (std::size_t j = 1; j < n; ++j) b[j] = b[m - j] = std::conj(chirp[j]);
    rc::fftDouble(a, false);
    rc::fftDouble(b, false);
    for (std::size_t i = 0; i < m; ++i) a[i] *= b[i];
    rc::fftDouble(a, true);
    for (std::size_t k = 0; k < half; ++k) out[k] = a[k] * chirp[k];
    return out;
}

std::vector<double> convolveFull(const std::vector<double>& x, const std::vector<double>& h, std::size_t from, std::size_t count) {
    std::vector<double> out(count, 0.0);
    const long long nx = static_cast<long long>(x.size()), nh = static_cast<long long>(h.size());
    if (nh <= 256) {
        parallelFor(static_cast<long long>(count), 8192, [&](long long b, long long e) {
            for (long long i = b; i < e; ++i) {
                const long long m = static_cast<long long>(from) + i;
                const long long jlo = std::max<long long>(0, m - (nx - 1)), jhi = std::min<long long>(nh - 1, m);
                double acc = 0.0;
                for (long long j = jlo; j <= jhi; ++j) acc += h[static_cast<std::size_t>(j)] * x[static_cast<std::size_t>(m - j)];
                out[static_cast<std::size_t>(i)] = acc;
            }
        });
        return out;
    }
    const std::size_t fftN = pow2At(static_cast<std::size_t>(nh) * 4);
    const long long block = static_cast<long long>(fftN) - nh + 1;
    std::vector<cd> H(fftN, cd(0, 0));
    for (long long j = 0; j < nh; ++j) H[static_cast<std::size_t>(j)] = cd(h[static_cast<std::size_t>(j)], 0);
    rc::fftDouble(H, false);
    const long long first = static_cast<long long>(from), last = static_cast<long long>(from + count);
    const long long nBlocks = (nx + block - 1) / block;
    std::vector<std::vector<double>> parts(static_cast<std::size_t>(nBlocks));
    parallelFor(nBlocks, 1, [&](long long b, long long e) {
        for (long long bi = b; bi < e; ++bi) {
            const long long start = bi * block;
            std::vector<cd> a(fftN, cd(0, 0));
            for (long long i = 0; i < block && start + i < nx; ++i) a[static_cast<std::size_t>(i)] = cd(x[static_cast<std::size_t>(start + i)], 0);
            rc::fftDouble(a, false);
            for (std::size_t i = 0; i < fftN; ++i) a[i] *= H[i];
            rc::fftDouble(a, true);
            auto& p = parts[static_cast<std::size_t>(bi)];
            p.resize(fftN);
            for (std::size_t i = 0; i < fftN; ++i) p[i] = a[i].real();
        }
    });
    for (long long bi = 0; bi < nBlocks; ++bi) {
        const long long start = bi * block;
        const auto& p = parts[static_cast<std::size_t>(bi)];
        const long long lo = std::max(first, start), hi = std::min(last, start + static_cast<long long>(fftN));
        for (long long m = lo; m < hi; ++m) out[static_cast<std::size_t>(m - first)] += p[static_cast<std::size_t>(m - start)];
    }
    return out;
}

std::vector<double> solveDense(std::vector<double> a, std::vector<double> b, int n) {
    for (int c = 0; c < n; ++c) {
        int piv = c;
        for (int r = c + 1; r < n; ++r)
            if (std::abs(a[static_cast<std::size_t>(r) * n + c]) > std::abs(a[static_cast<std::size_t>(piv) * n + c])) piv = r;
        if (piv != c) {
            for (int k = 0; k < n; ++k) std::swap(a[static_cast<std::size_t>(c) * n + k], a[static_cast<std::size_t>(piv) * n + k]);
            std::swap(b[static_cast<std::size_t>(c)], b[static_cast<std::size_t>(piv)]);
        }
        const double d = a[static_cast<std::size_t>(c) * n + c];
        if (d == 0.0) continue;
        for (int r = c + 1; r < n; ++r) {
            const double f = a[static_cast<std::size_t>(r) * n + c] / d;
            if (f == 0.0) continue;
            for (int k = c; k < n; ++k) a[static_cast<std::size_t>(r) * n + k] -= f * a[static_cast<std::size_t>(c) * n + k];
            b[static_cast<std::size_t>(r)] -= f * b[static_cast<std::size_t>(c)];
        }
    }
    std::vector<double> x(static_cast<std::size_t>(n));
    for (int r = n - 1; r >= 0; --r) {
        double acc = b[static_cast<std::size_t>(r)];
        for (int k = r + 1; k < n; ++k) acc -= a[static_cast<std::size_t>(r) * n + k] * x[static_cast<std::size_t>(k)];
        const double d = a[static_cast<std::size_t>(r) * n + r];
        x[static_cast<std::size_t>(r)] = d != 0.0 ? acc / d : 0.0;
    }
    return x;
}

namespace {

void tqli(std::vector<double>& d, std::vector<double>& e, int n, std::vector<double>& z) {
    for (int i = 1; i < n; ++i) e[static_cast<std::size_t>(i - 1)] = e[static_cast<std::size_t>(i)];
    if (n > 0) e[static_cast<std::size_t>(n - 1)] = 0.0;
    for (int l = 0; l < n; ++l) {
        int iter = 0, m;
        do {
            for (m = l; m < n - 1; ++m) {
                const double dd = std::abs(d[static_cast<std::size_t>(m)]) + std::abs(d[static_cast<std::size_t>(m + 1)]);
                if (std::abs(e[static_cast<std::size_t>(m)]) <= std::numeric_limits<double>::epsilon() * dd) break;
            }
            if (m != l) {
                if (iter++ == 60) break;
                double g = (d[static_cast<std::size_t>(l + 1)] - d[static_cast<std::size_t>(l)]) / (2.0 * e[static_cast<std::size_t>(l)]);
                double r = std::hypot(g, 1.0);
                g = d[static_cast<std::size_t>(m)] - d[static_cast<std::size_t>(l)] + e[static_cast<std::size_t>(l)] / (g + (g >= 0 ? std::abs(r) : -std::abs(r)));
                double s = 1.0, c = 1.0, p = 0.0;
                int i;
                for (i = m - 1; i >= l; --i) {
                    double f = s * e[static_cast<std::size_t>(i)];
                    const double b = c * e[static_cast<std::size_t>(i)];
                    r = std::hypot(f, g);
                    e[static_cast<std::size_t>(i + 1)] = r;
                    if (r == 0.0) {
                        d[static_cast<std::size_t>(i + 1)] -= p;
                        e[static_cast<std::size_t>(m)] = 0.0;
                        break;
                    }
                    s = f / r;
                    c = g / r;
                    g = d[static_cast<std::size_t>(i + 1)] - p;
                    r = (d[static_cast<std::size_t>(i)] - g) * s + 2.0 * c * b;
                    p = s * r;
                    d[static_cast<std::size_t>(i + 1)] = g + p;
                    g = c * r - b;
                    for (int k = 0; k < n; ++k) {
                        f = z[static_cast<std::size_t>(k) * n + i + 1];
                        z[static_cast<std::size_t>(k) * n + i + 1] = s * z[static_cast<std::size_t>(k) * n + i] + c * f;
                        z[static_cast<std::size_t>(k) * n + i] = c * z[static_cast<std::size_t>(k) * n + i] - s * f;
                    }
                }
                if (r == 0.0 && i >= l) continue;
                d[static_cast<std::size_t>(l)] -= p;
                e[static_cast<std::size_t>(l)] = g;
                e[static_cast<std::size_t>(m)] = 0.0;
            }
        } while (m != l);
    }
}

}

void hermitianTopEigen(std::vector<cd>& g, int n, int k, std::vector<double>& values, std::vector<cd>& vectors) {
    auto A = [&](int r, int c) -> cd& { return g[static_cast<std::size_t>(r) * n + c]; };
    std::vector<cd> q(static_cast<std::size_t>(n) * n, cd(0, 0));
    for (int i = 0; i < n; ++i) q[static_cast<std::size_t>(i) * n + i] = cd(1, 0);
    std::vector<cd> v(static_cast<std::size_t>(n)), p(static_cast<std::size_t>(n)), w(static_cast<std::size_t>(n));
    std::vector<cd> sub(static_cast<std::size_t>(std::max(0, n - 1)), cd(0, 0));
    for (int j = 0; j + 2 < n; ++j) {
        double norm = 0.0;
        for (int r = j + 1; r < n; ++r) norm += std::norm(A(r, j));
        norm = std::sqrt(norm);
        const cd x0 = A(j + 1, j);
        if (norm == 0.0) { sub[static_cast<std::size_t>(j)] = cd(0, 0); continue; }
        const cd phase = std::abs(x0) > 0 ? x0 / std::abs(x0) : cd(1, 0);
        const cd alpha = -phase * norm;
        for (int r = 0; r < n; ++r) v[static_cast<std::size_t>(r)] = cd(0, 0);
        for (int r = j + 1; r < n; ++r) v[static_cast<std::size_t>(r)] = A(r, j);
        v[static_cast<std::size_t>(j + 1)] -= alpha;
        double vn = 0.0;
        for (int r = j + 1; r < n; ++r) vn += std::norm(v[static_cast<std::size_t>(r)]);
        vn = std::sqrt(vn);
        if (vn == 0.0) { sub[static_cast<std::size_t>(j)] = x0; continue; }
        for (int r = j + 1; r < n; ++r) v[static_cast<std::size_t>(r)] /= vn;
        for (int r = j + 1; r < n; ++r) {
            cd acc(0, 0);
            for (int c = j + 1; c < n; ++c) acc += A(r, c) * v[static_cast<std::size_t>(c)];
            p[static_cast<std::size_t>(r)] = acc;
        }
        cd K(0, 0);
        for (int r = j + 1; r < n; ++r) K += std::conj(v[static_cast<std::size_t>(r)]) * p[static_cast<std::size_t>(r)];
        for (int r = j + 1; r < n; ++r) w[static_cast<std::size_t>(r)] = p[static_cast<std::size_t>(r)] - K.real() * v[static_cast<std::size_t>(r)];
        for (int r = j + 1; r < n; ++r)
            for (int c = j + 1; c < n; ++c)
                A(r, c) -= 2.0 * (v[static_cast<std::size_t>(r)] * std::conj(w[static_cast<std::size_t>(c)]) +
                                  w[static_cast<std::size_t>(r)] * std::conj(v[static_cast<std::size_t>(c)]));
        A(j + 1, j) = alpha;
        A(j, j + 1) = std::conj(alpha);
        for (int r = j + 2; r < n; ++r) { A(r, j) = cd(0, 0); A(j, r) = cd(0, 0); }
        sub[static_cast<std::size_t>(j)] = alpha;
        for (int r = 0; r < n; ++r) {
            cd acc(0, 0);
            for (int c = j + 1; c < n; ++c) acc += q[static_cast<std::size_t>(r) * n + c] * v[static_cast<std::size_t>(c)];
            for (int c = j + 1; c < n; ++c) q[static_cast<std::size_t>(r) * n + c] -= 2.0 * acc * std::conj(v[static_cast<std::size_t>(c)]);
        }
    }
    if (n >= 2) sub[static_cast<std::size_t>(n - 2)] = A(n - 1, n - 2);
    std::vector<cd> dphase(static_cast<std::size_t>(n), cd(1, 0));
    std::vector<double> diag(static_cast<std::size_t>(n)), off(static_cast<std::size_t>(n), 0.0);
    for (int i = 0; i < n; ++i) diag[static_cast<std::size_t>(i)] = A(i, i).real();
    for (int i = 0; i + 1 < n; ++i) {
        const cd b = sub[static_cast<std::size_t>(i)];
        const double ab = std::abs(b);
        dphase[static_cast<std::size_t>(i + 1)] = ab > 0 ? dphase[static_cast<std::size_t>(i)] * (b / ab) : dphase[static_cast<std::size_t>(i)];
        off[static_cast<std::size_t>(i + 1)] = ab;
    }
    std::vector<double> z(static_cast<std::size_t>(n) * n, 0.0);
    for (int i = 0; i < n; ++i) z[static_cast<std::size_t>(i) * n + i] = 1.0;
    tqli(diag, off, n, z);
    std::vector<int> order(static_cast<std::size_t>(n));
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return diag[static_cast<std::size_t>(a)] > diag[static_cast<std::size_t>(b)]; });
    k = std::min(k, n);
    values.assign(static_cast<std::size_t>(k), 0.0);
    vectors.assign(static_cast<std::size_t>(n) * k, cd(0, 0));
    for (int s = 0; s < k; ++s) {
        const int idx = order[static_cast<std::size_t>(s)];
        values[static_cast<std::size_t>(s)] = diag[static_cast<std::size_t>(idx)];
        for (int r = 0; r < n; ++r) {
            cd acc(0, 0);
            for (int c = 0; c < n; ++c)
                acc += q[static_cast<std::size_t>(r) * n + c] * dphase[static_cast<std::size_t>(c)] * z[static_cast<std::size_t>(c) * n + idx];
            vectors[static_cast<std::size_t>(r) * k + s] = acc;
        }
    }
}

double gradientAt(const std::vector<double>& x, const std::vector<double>& f, std::size_t i) {
    const std::size_t n = f.size();
    if (n < 2) return 0.0;
    if (i == 0) return (f[1] - f[0]) / (x[1] - x[0]);
    if (i == n - 1) return (f[n - 1] - f[n - 2]) / (x[n - 1] - x[n - 2]);
    const double hd = x[i] - x[i - 1], hs = x[i + 1] - x[i];
    return (hs * hs * f[i + 1] + (hd * hd - hs * hs) * f[i] - hd * hd * f[i - 1]) / (hs * hd * (hd + hs));
}

double quantileOf(std::vector<double> v, double q) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double pos = q * static_cast<double>(v.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(std::floor(pos));
    const std::size_t hi = std::min(lo + 1, v.size() - 1);
    const double t = pos - static_cast<double>(lo);
    return v[lo] + (v[hi] - v[lo]) * t;
}

double medianOf(std::vector<double> v) { return quantileOf(std::move(v), 0.5); }

double interp(double x, const std::vector<double>& xp, const std::vector<double>& fp) {
    if (xp.empty()) return 0.0;
    if (x <= xp.front()) return fp.front();
    if (x >= xp.back()) return fp.back();
    const auto it = std::upper_bound(xp.begin(), xp.end(), x);
    const std::size_t j = static_cast<std::size_t>(it - xp.begin());
    const double x0 = xp[j - 1], x1 = xp[j];
    return fp[j - 1] + (fp[j] - fp[j - 1]) * (x - x0) / (x1 - x0);
}

}
}
