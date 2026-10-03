#include "fw_internal.h"
#include "parallel.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace utagoe {
namespace fw {

double Rng::normal() {
    if (have_) { have_ = false; return spare_; }
    double u, v, s;
    do {
        u = uniform() * 2.0 - 1.0;
        v = uniform() * 2.0 - 1.0;
        s = u * u + v * v;
    } while (s >= 1.0 || s == 0.0);
    const double m = std::sqrt(-2.0 * std::log(s) / s);
    spare_ = v * m;
    have_ = true;
    return u * m;
}

std::uint32_t crc32(const std::string& s) {
    std::uint32_t c = 0xFFFFFFFFu;
    for (unsigned char ch : s) {
        c ^= ch;
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return c ^ 0xFFFFFFFFu;
}

namespace {

std::size_t pow2(std::size_t n) {
    std::size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

void fftPow2(std::vector<cd>& a, bool inverse) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2 * kPi / static_cast<double>(len) * (inverse ? 1 : -1);
        const cd wl(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) {
            cd w(1, 0);
            for (std::size_t j = 0; j < len / 2; ++j) {
                const cd u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse)
        for (cd& x : a) x /= static_cast<double>(n);
}

}

void fft(std::vector<cd>& a, bool inverse) {
    const std::size_t n = a.size();
    if (n <= 1) return;
    if ((n & (n - 1)) == 0) { fftPow2(a, inverse); return; }
    const std::size_t m = pow2(2 * n - 1);
    const double sgn = inverse ? 1.0 : -1.0;
    std::vector<cd> w(n), A(m, cd(0, 0)), B(m, cd(0, 0));
    for (std::size_t k = 0; k < n; ++k) {
        const double ang = sgn * kPi * static_cast<double>((static_cast<unsigned long long>(k) * k) % (2 * n)) / static_cast<double>(n);
        w[k] = cd(std::cos(ang), std::sin(ang));
    }
    for (std::size_t k = 0; k < n; ++k) A[k] = a[k] * w[k];
    B[0] = std::conj(w[0]);
    for (std::size_t k = 1; k < n; ++k) B[k] = B[m - k] = std::conj(w[k]);
    fftPow2(A, false);
    fftPow2(B, false);
    for (std::size_t k = 0; k < m; ++k) A[k] *= B[k];
    fftPow2(A, true);
    for (std::size_t k = 0; k < n; ++k) a[k] = A[k] * w[k] / (inverse ? static_cast<double>(n) : 1.0);
}

std::vector<cd> rfft(const std::vector<double>& x, std::size_t n) {
    std::vector<cd> a(n, cd(0, 0));
    for (std::size_t i = 0; i < std::min(n, x.size()); ++i) a[i] = cd(x[i], 0);
    fft(a, false);
    a.resize(n / 2 + 1);
    return a;
}

std::vector<double> irfft(const std::vector<cd>& X, std::size_t n) {
    std::vector<cd> a(n, cd(0, 0));
    for (std::size_t k = 0; k < X.size() && k < n; ++k) a[k] = X[k];
    for (std::size_t k = 1; k < (n + 1) / 2 && k < X.size(); ++k) a[n - k] = std::conj(X[k]);
    if (n % 2 == 0 && X.size() > n / 2) a[n / 2] = cd(X[n / 2].real(), 0);
    a[0] = cd(a[0].real(), 0);
    fft(a, true);
    std::vector<double> out(n);
    for (std::size_t i = 0; i < n; ++i) out[i] = a[i].real();
    return out;
}

std::vector<double> convolveFull(const std::vector<double>& x, const std::vector<double>& h) {
    if (x.empty() || h.empty()) return {};
    const std::size_t n = x.size() + h.size() - 1;
    std::vector<double> out(n, 0.0);
    if (std::min(x.size(), h.size()) <= 64 || static_cast<double>(x.size()) * static_cast<double>(h.size()) < 2e6) {
        for (std::size_t i = 0; i < x.size(); ++i)
            for (std::size_t j = 0; j < h.size(); ++j) out[i + j] += x[i] * h[j];
        return out;
    }
    const std::size_t m = pow2(n);
    std::vector<cd> a(m, cd(0, 0)), b(m, cd(0, 0));
    for (std::size_t i = 0; i < x.size(); ++i) a[i] = cd(x[i], 0);
    for (std::size_t i = 0; i < h.size(); ++i) b[i] = cd(h[i], 0);
    fftPow2(a, false);
    fftPow2(b, false);
    for (std::size_t i = 0; i < m; ++i) a[i] *= b[i];
    fftPow2(a, true);
    for (std::size_t i = 0; i < n; ++i) out[i] = a[i].real();
    return out;
}

std::vector<double> convolveSame(const std::vector<double>& x, const std::vector<double>& h) {
    const std::vector<double> full = convolveFull(x, h);
    const std::size_t start = (h.size() - 1) / 2;
    return std::vector<double>(full.begin() + static_cast<std::ptrdiff_t>(start), full.begin() + static_cast<std::ptrdiff_t>(start + x.size()));
}

std::vector<double> correlateValid(const std::vector<double>& r, const std::vector<double>& q) {
    if (q.empty() || q.size() > r.size()) return {};
    std::vector<double> rev(q.rbegin(), q.rend());
    const std::vector<double> full = convolveFull(r, rev);
    return std::vector<double>(full.begin() + static_cast<std::ptrdiff_t>(q.size() - 1), full.begin() + static_cast<std::ptrdiff_t>(r.size()));
}

std::vector<double> hannPeriodic(int n) {
    std::vector<double> w(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) w[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / n);
    return w;
}

double quantile(std::vector<double> v, double q) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double pos = q * static_cast<double>(v.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(std::floor(pos));
    const std::size_t hi = std::min(lo + 1, v.size() - 1);
    return v[lo] + (v[hi] - v[lo]) * (pos - static_cast<double>(lo));
}

double median(std::vector<double> v) { return quantile(std::move(v), 0.5); }

double mean(const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    double s = 0;
    for (double x : v) s += x;
    return s / static_cast<double>(v.size());
}

double stdev(const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    const double m = mean(v);
    double s = 0;
    for (double x : v) s += (x - m) * (x - m);
    return std::sqrt(s / static_cast<double>(v.size()));
}

std::vector<long long> findPeaksDistance(const std::vector<double>& x, long long distance) {
    std::vector<long long> peaks;
    const long long n = static_cast<long long>(x.size()), iMax = n - 1;
    long long i = 1;
    while (i < iMax) {
        if (x[static_cast<std::size_t>(i - 1)] < x[static_cast<std::size_t>(i)]) {
            long long ahead = i + 1;
            while (ahead < iMax && x[static_cast<std::size_t>(ahead)] == x[static_cast<std::size_t>(i)]) ++ahead;
            if (x[static_cast<std::size_t>(ahead)] < x[static_cast<std::size_t>(i)]) {
                peaks.push_back((i + ahead - 1) / 2);
                i = ahead;
            }
        }
        ++i;
    }
    const std::size_t np = peaks.size();
    std::vector<char> keep(np, 1);
    std::vector<std::size_t> order(np);
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return x[static_cast<std::size_t>(peaks[a])] < x[static_cast<std::size_t>(peaks[b])];
    });
    for (std::size_t r = np; r-- > 0;) {
        const std::size_t j = order[r];
        if (!keep[j]) continue;
        for (long long k = static_cast<long long>(j) - 1; k >= 0 && peaks[j] - peaks[static_cast<std::size_t>(k)] < distance; --k)
            keep[static_cast<std::size_t>(k)] = 0;
        for (std::size_t k = j + 1; k < np && peaks[k] - peaks[j] < distance; ++k) keep[k] = 0;
    }
    std::vector<long long> out;
    for (std::size_t j = 0; j < np; ++j)
        if (keep[j]) out.push_back(peaks[j]);
    return out;
}

double minimizeBounded(const std::function<double(double)>& func, double lo, double hi, double xatol, int maxiter, double& fval) {
    const double sqrtEps = std::sqrt(2.2e-16);
    const double golden = 0.5 * (3.0 - std::sqrt(5.0));
    double a = lo, b = hi;
    double fulc = a + golden * (b - a);
    double nfc = fulc, xf = fulc;
    double rat = 0, e = 0;
    double x = xf;
    double fx = func(x);
    int num = 1;
    double fu;
    double ffulc = fx, fnfc = fx;
    double xm = 0.5 * (a + b);
    double tol1 = sqrtEps * std::abs(xf) + xatol / 3.0;
    double tol2 = 2.0 * tol1;
    while (std::abs(xf - xm) > (tol2 - 0.5 * (b - a))) {
        bool gold = true;
        if (std::abs(e) > tol1) {
            gold = false;
            double r = (xf - nfc) * (fx - ffulc);
            double q = (xf - fulc) * (fx - fnfc);
            double p = (xf - fulc) * q - (xf - nfc) * r;
            q = 2.0 * (q - r);
            if (q > 0.0) p = -p;
            q = std::abs(q);
            r = e;
            e = rat;
            if (std::abs(p) < std::abs(0.5 * q * r) && p > q * (a - xf) && p < q * (b - xf)) {
                rat = (p + 0.0) / q;
                x = xf + rat;
                if ((x - a) < tol2 || (b - x) < tol2) {
                    const double si = (xm - xf > 0 ? 1.0 : (xm - xf < 0 ? -1.0 : 0.0)) + ((xm - xf) == 0 ? 1.0 : 0.0);
                    rat = tol1 * si;
                }
            } else {
                gold = true;
            }
        }
        if (gold) {
            e = xf >= xm ? a - xf : b - xf;
            rat = golden * e;
        }
        const double si = (rat > 0 ? 1.0 : (rat < 0 ? -1.0 : 0.0)) + (rat == 0 ? 1.0 : 0.0);
        x = xf + si * std::max(std::abs(rat), tol1);
        fu = func(x);
        ++num;
        if (fu <= fx) {
            if (x >= xf) a = xf;
            else b = xf;
            fulc = nfc; ffulc = fnfc;
            nfc = xf; fnfc = fx;
            xf = x; fx = fu;
        } else {
            if (x < xf) a = x;
            else b = x;
            if (fu <= fnfc || nfc == xf) {
                fulc = nfc; ffulc = fnfc;
                nfc = x; fnfc = fu;
            } else if (fu <= ffulc || fulc == xf || fulc == nfc) {
                fulc = x; ffulc = fu;
            }
        }
        xm = 0.5 * (a + b);
        tol1 = sqrtEps * std::abs(xf) + xatol / 3.0;
        tol2 = 2.0 * tol1;
        if (num >= maxiter) break;
    }
    fval = fx;
    return xf;
}

Mat matmulT(const Mat& a, const Mat& bt) {
    Mat out(a.rows, bt.rows);
    parallelFor(a.rows, 16, [&](long long r0, long long r1) {
        for (long long r = r0; r < r1; ++r) {
            const double* ar = a.row(static_cast<int>(r));
            for (int c = 0; c < bt.rows; ++c) {
                const double* br = bt.row(c);
                double s = 0;
                for (int k = 0; k < a.cols; ++k) s += ar[k] * br[k];
                out.at(static_cast<int>(r), c) = s;
            }
        }
    });
    return out;
}

void rowsUnit(Mat& x, bool center) {
    for (int r = 0; r < x.rows; ++r) {
        double* p = x.row(r);
        if (center) {
            double m = 0;
            for (int c = 0; c < x.cols; ++c) m += p[c];
            m /= std::max(1, x.cols);
            for (int c = 0; c < x.cols; ++c) p[c] -= m;
        }
        double n = 0;
        for (int c = 0; c < x.cols; ++c) n += p[c] * p[c];
        n = std::max(std::sqrt(n), kEps);
        for (int c = 0; c < x.cols; ++c) p[c] /= n;
    }
}

void gaussian1d(Mat& x, double sigma, int order) {
    const int r = static_cast<int>(4.0 * sigma + 0.5);
    std::vector<double> k(static_cast<std::size_t>(2 * r + 1));
    double sum = 0;
    for (int d = -r; d <= r; ++d) {
        k[static_cast<std::size_t>(d + r)] = std::exp(-0.5 / (sigma * sigma) * d * d);
        sum += k[static_cast<std::size_t>(d + r)];
    }
    for (int d = -r; d <= r; ++d) {
        double v = k[static_cast<std::size_t>(d + r)] / sum;
        if (order == 1) v *= static_cast<double>(d) / (sigma * sigma);
        k[static_cast<std::size_t>(d + r)] = v;
    }
    const long long n = x.rows;
    auto reflect = [n](long long i) {
        const long long period = 2 * n;
        i %= period;
        if (i < 0) i += period;
        return i < n ? i : period - 1 - i;
    };
    Mat src = x;
    for (int c = 0; c < x.cols; ++c)
        for (long long i = 0; i < n; ++i) {
            double acc = 0;
            for (int d = -r; d <= r; ++d) acc += k[static_cast<std::size_t>(d + r)] * src.at(static_cast<int>(reflect(i + d)), c);
            x.at(static_cast<int>(i), c) = acc;
        }
}

void medianFilterAxis(const Mat& in, Mat& out, int size, int axis) {
    out = Mat(in.rows, in.cols);
    const int half = size / 2;
    parallelFor(axis == 0 ? in.cols : in.rows, 8, [&](long long b, long long e) {
        std::vector<double> win(static_cast<std::size_t>(size));
        for (long long o = b; o < e; ++o) {
            const int len = axis == 0 ? in.rows : in.cols;
            for (int i = 0; i < len; ++i) {
                for (int d = 0; d < size; ++d) {
                    const int j = std::clamp(i + d - half, 0, len - 1);
                    win[static_cast<std::size_t>(d)] = axis == 0 ? in.at(j, static_cast<int>(o)) : in.at(static_cast<int>(o), j);
                }
                std::nth_element(win.begin(), win.begin() + half, win.end());
                if (axis == 0) out.at(i, static_cast<int>(o)) = win[static_cast<std::size_t>(half)];
                else out.at(static_cast<int>(o), i) = win[static_cast<std::size_t>(half)];
            }
        }
    });
}

Mat maxFilter2d(const Mat& in, int sizeRows, int sizeCols) {
    Mat tmp(in.rows, in.cols), out(in.rows, in.cols);
    const int hr = sizeRows / 2, hc = sizeCols / 2;
    for (int r = 0; r < in.rows; ++r)
        for (int c = 0; c < in.cols; ++c) {
            double m = -1e300;
            for (int d = -hr; d < sizeRows - hr; ++d) m = std::max(m, in.at(std::clamp(r + d, 0, in.rows - 1), c));
            tmp.at(r, c) = m;
        }
    for (int r = 0; r < in.rows; ++r)
        for (int c = 0; c < in.cols; ++c) {
            double m = -1e300;
            for (int d = -hc; d < sizeCols - hc; ++d) m = std::max(m, tmp.at(r, std::clamp(c + d, 0, in.cols - 1)));
            out.at(r, c) = m;
        }
    return out;
}

Audio sincSample(const Audio& a, const std::vector<double>& pos, int radius) {
    Audio out;
    out.channels = a.channels;
    out.v.assign(pos.size() * static_cast<std::size_t>(a.channels), 0.0);
    const long long len = static_cast<long long>(a.frames());
    parallelFor(static_cast<long long>(pos.size()), 4096, [&](long long b, long long e) {
        for (long long n = b; n < e; ++n) {
            const double p = pos[static_cast<std::size_t>(n)];
            const long long nearest = static_cast<long long>(std::nearbyint(p));
            if (std::abs(p - static_cast<double>(nearest)) < 1e-9) {
                if (nearest >= 0 && nearest < len)
                    for (int c = 0; c < a.channels; ++c) out.at(static_cast<std::size_t>(n), c) = a.at(static_cast<std::size_t>(nearest), c);
                continue;
            }
            const long long base = static_cast<long long>(std::floor(p));
            double den = 0;
            for (long long j = base - radius + 1; j <= base + radius; ++j) {
                const double d = p - static_cast<double>(j);
                if (std::abs(d) >= radius) continue;
                const double w = std::abs(d) < 1e-12 ? 1.0 : std::sin(kPi * d) / (kPi * d) * (0.5 + 0.5 * std::cos(kPi * d / radius));
                den += w;
                if (j >= 0 && j < len)
                    for (int c = 0; c < a.channels; ++c) out.at(static_cast<std::size_t>(n), c) += w * a.at(static_cast<std::size_t>(j), c);
            }
            if (std::abs(den) > 1e-12)
                for (int c = 0; c < a.channels; ++c) out.at(static_cast<std::size_t>(n), c) /= den;
        }
    });
    return out;
}

Audio slicePad(const Audio& a, long long start, long long length) {
    Audio out;
    out.channels = a.channels;
    out.v.assign(static_cast<std::size_t>(std::max<long long>(0, length)) * static_cast<std::size_t>(a.channels), 0.0);
    const long long lo = std::max<long long>(0, start), hi = std::min<long long>(static_cast<long long>(a.frames()), start + length);
    for (long long i = lo; i < hi; ++i)
        for (int c = 0; c < a.channels; ++c) out.at(static_cast<std::size_t>(i - start), c) = a.at(static_cast<std::size_t>(i), c);
    return out;
}

Audio guideAudio(const Audio& a, const std::string& mode) {
    if (mode != "side") return a;
    if (a.channels != 2) throw Error("side guide requires stereo input");
    Audio s;
    s.channels = 1;
    s.v.resize(a.frames());
    for (std::size_t i = 0; i < a.frames(); ++i) s.v[i] = (a.at(i, 0) - a.at(i, 1)) * 0.5;
    return s;
}

void corrPeak(const Audio& r, const Audio& yIn, double& peak, double& quality) {
    peak = 0;
    quality = 0;
    const long long m = static_cast<long long>(yIn.frames());
    const long long n = static_cast<long long>(r.frames()) - m + 1;
    if (n < 1 || m < 1) return;
    std::vector<double> num(static_cast<std::size_t>(n), 0.0), power(static_cast<std::size_t>(n), 0.0);
    double yy = 0;
    for (int c = 0; c < r.channels; ++c) {
        std::vector<double> rc(r.frames()), yc(static_cast<std::size_t>(m));
        for (std::size_t i = 0; i < r.frames(); ++i) rc[i] = r.at(i, c);
        double ym = 0;
        for (long long i = 0; i < m; ++i) ym += yIn.at(static_cast<std::size_t>(i), c);
        ym /= static_cast<double>(m);
        for (long long i = 0; i < m; ++i) {
            yc[static_cast<std::size_t>(i)] = yIn.at(static_cast<std::size_t>(i), c) - ym;
            yy += yc[static_cast<std::size_t>(i)] * yc[static_cast<std::size_t>(i)];
        }
        const std::vector<double> cr = correlateValid(rc, yc);
        std::vector<double> cs(rc.size() + 1, 0.0), cs2(rc.size() + 1, 0.0);
        for (std::size_t i = 0; i < rc.size(); ++i) {
            cs[i + 1] = cs[i] + rc[i];
            cs2[i + 1] = cs2[i] + rc[i] * rc[i];
        }
        for (long long k = 0; k < n; ++k) {
            num[static_cast<std::size_t>(k)] += cr[static_cast<std::size_t>(k)];
            const double s = cs[static_cast<std::size_t>(k + m)] - cs[static_cast<std::size_t>(k)];
            const double s2 = cs2[static_cast<std::size_t>(k + m)] - cs2[static_cast<std::size_t>(k)];
            power[static_cast<std::size_t>(k)] += std::max(s2 - s * s / static_cast<double>(m), 0.0);
        }
    }
    std::vector<double> corr(static_cast<std::size_t>(n));
    std::size_t best = 0;
    for (std::size_t k = 0; k < corr.size(); ++k) {
        corr[k] = num[k] / std::sqrt(std::max(power[k] * yy, 1e-20));
        if (std::abs(corr[k]) > std::abs(corr[best])) best = k;
    }
    double delta = 0;
    if (best > 0 && best + 1 < corr.size()) {
        const double v0 = std::abs(corr[best - 1]), v1 = std::abs(corr[best]), v2 = std::abs(corr[best + 1]);
        const double den = v0 - 2 * v1 + v2;
        if (std::abs(den) > 1e-15) delta = std::clamp(0.5 * (v0 - v2) / den, -0.5, 0.5);
    }
    peak = static_cast<double>(best) + delta;
    quality = std::clamp(std::abs(corr[best]), 0.0, 1.0);
}

std::vector<double> resamplePoly(const std::vector<double>& x, int up, int down) {
    const int g = std::gcd(up, down);
    up /= g;
    down /= g;
    if (up == 1 && down == 1) return x;
    const long long nIn = static_cast<long long>(x.size());
    const int maxRate = std::max(up, down);
    const int halfLen = 10 * maxRate;
    const int taps = 2 * halfLen + 1;
    const double fc = 1.0 / maxRate;
    const double alpha = (taps - 1) / 2.0;
    auto besselI0 = [](double v) {
        double s = 1, t = 1;
        for (int k = 1; k < 60; ++k) {
            t *= (v / (2.0 * k)) * (v / (2.0 * k));
            s += t;
            if (t < 1e-17 * s) break;
        }
        return s;
    };
    const double i0b = besselI0(5.0);
    std::vector<double> h(static_cast<std::size_t>(taps));
    double sum = 0;
    for (int i = 0; i < taps; ++i) {
        const double m = i - alpha;
        const double v = fc * m * kPi;
        const double sinc = m == 0 ? 1.0 : std::sin(v) / v;
        const double r = m / alpha;
        h[static_cast<std::size_t>(i)] = fc * sinc * besselI0(5.0 * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b;
        sum += h[static_cast<std::size_t>(i)];
    }
    for (double& v : h) v = v / sum * up;
    const long long nOut = (nIn * up) / down + ((nIn * up) % down != 0);
    const int nPre = down - halfLen % down;
    const long long nPreRemove = (halfLen + nPre) / down;
    std::vector<double> hp(static_cast<std::size_t>(nPre), 0.0);
    hp.insert(hp.end(), h.begin(), h.end());
    const long long hl = static_cast<long long>(hp.size());
    std::vector<double> y(static_cast<std::size_t>(nOut));
    parallelFor(nOut, 2048, [&](long long b, long long e) {
        for (long long k = b; k < e; ++k) {
            const long long m = (k + nPreRemove) * down;
            double acc = 0;
            long long j = m % up;
            for (; j < hl && j <= m; j += up) {
                const long long src = (m - j) / up;
                if (src < nIn) acc += hp[static_cast<std::size_t>(j)] * x[static_cast<std::size_t>(src)];
            }
            y[static_cast<std::size_t>(k)] = acc;
        }
    });
    return y;
}

namespace {

Mat triangularBank(const std::vector<double>& freq, const std::vector<double>& edges) {
    const int nb = static_cast<int>(edges.size()) - 2, nf = static_cast<int>(freq.size());
    Mat w(nb, nf);
    for (int b = 0; b < nb; ++b) {
        double s = 0;
        for (int k = 0; k < nf; ++k) {
            const double up = (freq[static_cast<std::size_t>(k)] - edges[static_cast<std::size_t>(b)]) /
                              std::max(edges[static_cast<std::size_t>(b + 1)] - edges[static_cast<std::size_t>(b)], kEps);
            const double down = (edges[static_cast<std::size_t>(b + 2)] - freq[static_cast<std::size_t>(k)]) /
                                std::max(edges[static_cast<std::size_t>(b + 2)] - edges[static_cast<std::size_t>(b + 1)], kEps);
            const double v = std::max(0.0, std::min(up, down));
            w.at(b, k) = v;
            s += v;
        }
        for (int k = 0; k < nf; ++k) w.at(b, k) /= std::max(s, kEps);
    }
    return w;
}

Mat logScaled(const Mat& x, double scale) {
    Mat o = x;
    for (double& v : o.v) v = std::log1p(v / std::max(scale, kEps));
    return o;
}

}

Bank buildBank(const Audio& audio, int sr, const Config& cfg) {
    const int g = std::gcd(sr, cfg.analysisRate);
    const int C = audio.channels;
    std::vector<std::vector<double>> chans(static_cast<std::size_t>(C));
    for (int c = 0; c < C; ++c) {
        std::vector<double> x(audio.frames());
        for (std::size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(audio.at(i, c));
        chans[static_cast<std::size_t>(c)] = resamplePoly(x, cfg.analysisRate / g, sr / g);
        for (double& v : chans[static_cast<std::size_t>(c)]) v = static_cast<float>(v);
    }
    const int hop = std::max(16, static_cast<int>(std::nearbyint(cfg.analysisRate * cfg.featureStep)));
    const int nfft = 1 << static_cast<int>(std::ceil(std::log2(std::max({256.0, hop * 2.0, cfg.analysisRate * 0.128}))));
    for (auto& ch : chans)
        if (static_cast<int>(ch.size()) < nfft) ch.resize(static_cast<std::size_t>(nfft), 0.0);
    const long long len = static_cast<long long>(chans[0].size());
    const int half = nfft / 2;
    const long long padded = len + 2LL * half;
    const long long extra = ((-(padded - nfft)) % hop + hop) % hop;
    const int T = static_cast<int>((padded + extra - nfft) / hop + 1);
    const int F = nfft / 2 + 1;
    const std::vector<double> win = hannPeriodic(nfft);
    double wsum = 0;
    for (double v : win) wsum += v;
    std::vector<std::vector<cd>> z(static_cast<std::size_t>(C), std::vector<cd>(static_cast<std::size_t>(T) * F));
    parallelFor(T, 8, [&](long long b, long long e) {
        std::vector<cd> buf(static_cast<std::size_t>(nfft));
        for (long long t = b; t < e; ++t)
            for (int c = 0; c < C; ++c) {
                for (int j = 0; j < nfft; ++j) {
                    const long long i = t * hop - half + j;
                    const double v = (i >= 0 && i < len) ? chans[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)] : 0.0;
                    buf[static_cast<std::size_t>(j)] = cd(v * win[static_cast<std::size_t>(j)], 0);
                }
                std::vector<cd> a = buf;
                fft(a, false);
                for (int f = 0; f < F; ++f) z[static_cast<std::size_t>(c)][static_cast<std::size_t>(t) * F + f] = a[static_cast<std::size_t>(f)] / wsum;
            }
    });
    Bank bank;
    Mat mag(T, F), sideMag(T, F);
    double sideE = 0, midE = 0;
    for (int t = 0; t < T; ++t)
        for (int f = 0; f < F; ++f) {
            double p = 0;
            for (int c = 0; c < C; ++c) p += std::norm(z[static_cast<std::size_t>(c)][static_cast<std::size_t>(t) * F + f]);
            mag.at(t, f) = std::sqrt(p / C);
            if (C == 2) {
                const cd l = z[0][static_cast<std::size_t>(t) * F + f], r = z[1][static_cast<std::size_t>(t) * F + f];
                const cd mid = (l + r) * 0.5, side = (l - r) * 0.5;
                sideMag.at(t, f) = std::abs(side);
                sideE += std::norm(side);
                midE += std::norm(mid);
            }
        }
    bank.sideFraction = C == 2 ? sideE / (midE + sideE + kEps) : 0.0;
    std::vector<double> freq(static_cast<std::size_t>(F));
    for (int f = 0; f < F; ++f) freq[static_cast<std::size_t>(f)] = static_cast<double>(f) * cfg.analysisRate / nfft;
    std::vector<double> melEdges(34), logEdges(50);
    const double top = std::log1p(freq.back() / 700.0);
    for (int i = 0; i < 34; ++i) melEdges[static_cast<std::size_t>(i)] = 700.0 * std::expm1(top * i / 33.0);
    for (int i = 0; i < 50; ++i) logEdges[static_cast<std::size_t>(i)] = std::exp(std::log(35.0) + (std::log(freq.back()) - std::log(35.0)) * i / 49.0);
    const Mat mel = triangularBank(freq, melEdges), logbank = triangularBank(freq, logEdges);
    Mat mag2 = mag;
    for (double& v : mag2.v) v *= v;
    Mat band = matmulT(mag2, mel);
    for (double& v : band.v) v = std::sqrt(std::max(v, 0.0));
    std::vector<double> pos;
    for (double v : band.v)
        if (v > kEps) pos.push_back(v);
    const double scale = pos.empty() ? 1.0 : median(pos);
    const Mat spec = logScaled(band, scale);
    const int NB = spec.cols;
    Mat dct(T, NB);
    for (int t = 0; t < T; ++t)
        for (int k = 0; k < NB; ++k) {
            double s = 0;
            for (int n = 0; n < NB; ++n) s += spec.at(t, n) * std::cos(kPi * k * (2 * n + 1) / (2.0 * NB));
            dct.at(t, k) = 2 * s * (k == 0 ? std::sqrt(1.0 / (4 * NB)) : std::sqrt(1.0 / (2 * NB)));
        }
    Mat mfcc(T, 26), mfd(T, 13);
    for (int t = 0; t < T; ++t)
        for (int k = 0; k < 13; ++k) mfd.at(t, k) = dct.at(t, k + 1);
    Mat deriv = mfd;
    gaussian1d(deriv, 1.0, 1);
    for (int t = 0; t < T; ++t)
        for (int k = 0; k < 13; ++k) {
            mfcc.at(t, k) = mfd.at(t, k);
            mfcc.at(t, 13 + k) = deriv.at(t, k);
        }
    Mat harmMed, percMed;
    medianFilterAxis(mag, harmMed, 7, 0);
    medianFilterAxis(mag, percMed, 17, 1);
    Mat harm(T, F), perc(T, F);
    for (std::size_t i = 0; i < mag.v.size(); ++i) {
        const double h2 = harmMed.v[i] * harmMed.v[i], p2 = percMed.v[i] * percMed.v[i];
        const double hm = h2 / (h2 + p2 + kEps);
        harm.v[i] = mag.v[i] * hm;
        perc.v[i] = mag.v[i] * (1 - hm);
    }
    Mat chroma(T, 12);
    for (int f = 0; f < F; ++f) {
        const double fr = freq[static_cast<std::size_t>(f)];
        if (!(fr >= 65 && fr < 3500)) continue;
        const double pitch = 69 + 12 * std::log2(fr / 440.0);
        const int lo = static_cast<int>(std::floor(pitch));
        const double frac = pitch - lo;
        for (int t = 0; t < T; ++t) {
            chroma.at(t, ((lo % 12) + 12) % 12) += harm.at(t, f) * (1 - frac);
            chroma.at(t, (((lo + 1) % 12) + 12) % 12) += harm.at(t, f) * frac;
        }
    }
    Mat cens(T, 12);
    for (int t = 0; t < T; ++t) {
        double s = 0;
        for (int k = 0; k < 12; ++k) s += chroma.at(t, k);
        s = std::max(s, kEps);
        for (int k = 0; k < 12; ++k) {
            const double v = chroma.at(t, k) / s;
            double q = 0;
            for (double th : {0.4, 0.2, 0.1, 0.05}) q += v > th ? 1.0 : 0.0;
            cens.at(t, k) = q;
        }
    }
    gaussian1d(cens, 1.5, 0);
    const Mat harmonic = logScaled(matmulT(harm, logbank), scale);
    const Mat percb = logScaled(matmulT(perc, mel), scale);
    Mat onset(T, NB);
    for (int t = 0; t < T; ++t)
        for (int k = 0; k < NB; ++k) onset.at(t, k) = std::max(spec.at(t, k) - spec.at(t > 0 ? t - 1 : 0, k), 0.0);
    Mat onset6(T, 6);
    {
        int start = 0;
        for (int s = 0; s < 6; ++s) {
            const int size = NB / 6 + (s < NB % 6 ? 1 : 0);
            for (int t = 0; t < T; ++t) {
                double m = 0;
                for (int k = start; k < start + size; ++k) m += onset.at(t, k);
                onset6.at(t, s) = m / size;
            }
            start += size;
        }
    }
    Mat bass(T, 24);
    for (int t = 0; t < T; ++t) {
        double s = 0;
        for (int k = 0; k < 24; ++k) s += harmonic.at(t, k);
        s = std::max(s, kEps);
        for (int k = 0; k < 24; ++k) bass.at(t, k) = harmonic.at(t, k) / s;
    }
    const Mat spatial = logScaled(matmulT(sideMag, mel), scale);
    Mat order(T, NB);
    for (int t = 0; t < T; ++t) {
        std::vector<int> ix(static_cast<std::size_t>(NB));
        std::iota(ix.begin(), ix.end(), 0);
        std::stable_sort(ix.begin(), ix.end(), [&](int a, int b) { return spec.at(t, a) < spec.at(t, b); });
        for (int r = 0; r < NB; ++r) order.at(t, ix[static_cast<std::size_t>(r)]) = r / 31.0;
    }
    const int half16 = (NB + 1) / 2;
    Mat modulation(T, half16 * 3);
    int col = 0;
    for (int h : {2, 4, 8}) {
        for (int k = 0; k < half16; ++k) {
            for (int t = 0; t < T; ++t) {
                double acc = 0;
                for (int j = 0; j < 2 * h; ++j) {
                    const int src = t - j;
                    if (src < 0) break;
                    acc += (j < h ? 1.0 : -1.0) / h * spec.at(src, 2 * k);
                }
                modulation.at(t, col + k) = acc;
            }
        }
        col += half16;
    }
    const Mat hmax = maxFilter2d(harmonic, 1, 3);
    Mat ridges = harmonic;
    for (std::size_t i = 0; i < ridges.v.size(); ++i)
        if (!(harmonic.v[i] >= hmax.v[i])) ridges.v[i] = 0;
    std::vector<double> energy(static_cast<std::size_t>(T), 0.0);
    for (int t = 0; t < T; ++t)
        for (int f = 0; f < F; ++f) energy[static_cast<std::size_t>(t)] += mag.at(t, f) * mag.at(t, f);
    const double emed = median(energy);
    Mat env(T, 3);
    for (int t = 0; t < T; ++t) {
        env.at(t, 0) = std::log1p(energy[static_cast<std::size_t>(t)] / (emed + kEps));
        double s = 0;
        for (int k = 0; k < 6; ++k) s += onset6.at(t, k);
        env.at(t, 1) = s;
        double low = 0;
        for (int f = 0; f < F; ++f)
            if (freq[static_cast<std::size_t>(f)] < 250) low += mag.at(t, f) * mag.at(t, f);
        env.at(t, 2) = std::log1p(low / (emed + kEps));
    }
    bank.features["spectral"] = spec;
    bank.features["mfcc"] = mfcc;
    bank.features["chroma"] = chroma;
    bank.features["cens"] = cens;
    bank.features["harmonic"] = harmonic;
    bank.features["harmonic_ridges"] = ridges;
    bank.features["percussion"] = percb;
    bank.features["onset"] = onset6;
    bank.features["bass"] = bass;
    bank.features["side"] = spatial;
    bank.features["rank"] = order;
    bank.features["modulation"] = modulation;
    bank.features["envelope"] = env;
    bank.magnitude = mag;
    bank.frequency = freq;
    bank.energy = energy;
    bank.dt = static_cast<double>(hop) / cfg.analysisRate;
    bank.duration = static_cast<double>(audio.frames()) / sr;
    return bank;
}

void patches(const Mat& x, double dt, double context, int stride, int bins, bool center, Mat& p, std::vector<int>& idx) {
    const int half = std::max(2, static_cast<int>(std::nearbyint(context / dt / 2)));
    idx.clear();
    for (int i = half; i < x.rows - half; i += std::max(1, stride)) idx.push_back(i);
    p = Mat(static_cast<int>(idx.size()), x.cols * bins);
    if (idx.empty()) return;
    std::vector<int> edges(static_cast<std::size_t>(bins + 1));
    for (int k = 0; k <= bins; ++k) {
        const double v = -half + (2.0 * half + 1.0) * k / bins;
        edges[static_cast<std::size_t>(k)] = static_cast<int>(v);
    }
    Mat cs(x.rows + 1, x.cols);
    for (int t = 0; t < x.rows; ++t)
        for (int c = 0; c < x.cols; ++c) cs.at(t + 1, c) = cs.at(t, c) + x.at(t, c);
    for (std::size_t n = 0; n < idx.size(); ++n)
        for (int k = 0; k < bins; ++k) {
            const int a = idx[n] + edges[static_cast<std::size_t>(k)], b = idx[n] + edges[static_cast<std::size_t>(k + 1)];
            const int w = std::max(1, edges[static_cast<std::size_t>(k + 1)] - edges[static_cast<std::size_t>(k)]);
            for (int c = 0; c < x.cols; ++c) p.at(static_cast<int>(n), k * x.cols + c) = (cs.at(b, c) - cs.at(a, c)) / w;
        }
    rowsUnit(p, center);
}

}
}
