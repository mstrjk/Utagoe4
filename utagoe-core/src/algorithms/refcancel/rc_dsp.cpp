// 共通の DSP 部品 (Python 版 dsp.py)。
// STFT は平方根 Hann 窓、逆変換は窓の二乗和で正規化する。gaussian は scipy.ndimage.gaussian_filter1d と同じ係数と端の扱い。

#include "rc.h"
#include "parallel.h"
#include "fft.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace utagoe {
namespace rc {
namespace {

constexpr double kPi = 3.14159265358979323846;

// gaussian_filter1d の係数。半径は int(4 sigma + 0.5)。
std::vector<double> gaussianKernel(double sigma) {
    const int r = static_cast<int>(4.0 * sigma + 0.5);
    std::vector<double> k(static_cast<std::size_t>(2 * r + 1));
    double sum = 0.0;
    for (int i = -r; i <= r; ++i) {
        const double v = std::exp(-0.5 / (sigma * sigma) * i * i);
        k[static_cast<std::size_t>(i + r)] = v;
        sum += v;
    }
    for (double& v : k) v /= sum;
    return k;
}

template <class T, class Acc>
void gaussianImpl(T* data, int n0, int n1, int n2, int axis, double sigma) {
    if (sigma <= 0) return;
    const std::vector<double> k = gaussianKernel(sigma);
    const int r = static_cast<int>(k.size() / 2);
    const int dims[3] = {n0, n1, n2};
    const long long stride[3] = {static_cast<long long>(n1) * n2, n2, 1};
    const int len = dims[axis];
    const long long lines = static_cast<long long>(n0) * n1 * n2 / std::max(1, len);
    parallelFor(lines, 64, [&](long long b, long long e) {
        std::vector<T> line(static_cast<std::size_t>(len));
        for (long long li = b; li < e; ++li) {
            // li を axis 以外の 2 つの index に分解する。
            long long base;
            if (axis == 0) base = li;
            else if (axis == 1) base = (li / n2) * stride[0] + (li % n2);
            else base = li * n2;
            T* p = data + base;
            const long long s = stride[axis];
            for (int i = 0; i < len; ++i) line[static_cast<std::size_t>(i)] = p[i * s];
            for (int i = 0; i < len; ++i) {
                Acc acc = Acc(0);
                for (int j = -r; j <= r; ++j) {
                    int at = std::clamp(i + j, 0, len - 1);
                    acc += Acc(line[static_cast<std::size_t>(at)]) * k[static_cast<std::size_t>(j + r)];
                }
                p[i * s] = static_cast<T>(acc);
            }
        }
    });
}

}

std::vector<float> analysisWindow(int n) {
    std::vector<float> w(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) w[static_cast<std::size_t>(i)] = static_cast<float>(std::sqrt(0.5 - 0.5 * std::cos(2.0 * kPi * i / n)));
    return w;
}

void gaussian(float* d, int a, int b, int c, int axis, double s) { gaussianImpl<float, double>(d, a, b, c, axis, s); }
void gaussian(double* d, int a, int b, int c, int axis, double s) { gaussianImpl<double, double>(d, a, b, c, axis, s); }
void gaussianComplex(cf* d, int a, int b, int c, int axis, double s) { gaussianImpl<cf, cd>(d, a, b, c, axis, s); }
void gaussianComplex(cd* d, int a, int b, int c, int axis, double s) { gaussianImpl<cd, cd>(d, a, b, c, axis, s); }

Spec stft(const Audio& x, int nFft, int hop) {
    const long long len = static_cast<long long>(x.frames());
    const int half = nFft / 2;
    const long long tail = ((-len) % hop + hop) % hop;
    const int frames = static_cast<int>((len + tail) / hop + 1);
    const int C = x.channels;
    Spec z;
    z.resize(nFft / 2 + 1, frames, C);
    const std::vector<float> w = analysisWindow(nFft);
    FftTables tables(static_cast<std::size_t>(nFft));
    parallelFor(frames, 16, [&](long long b, long long e) {
        std::vector<float> buf(static_cast<std::size_t>(2 * nFft));
        for (long long t = b; t < e; ++t) {
            for (int c = 0; c < C; ++c) {
                for (int j = 0; j < nFft; ++j) {
                    const long long src = t * hop + j - half;
                    const float v = (src >= 0 && src < len) ? x.at(static_cast<std::size_t>(src), c) : 0.0f;
                    buf[static_cast<std::size_t>(2 * j)] = v * w[static_cast<std::size_t>(j)];
                    buf[static_cast<std::size_t>(2 * j + 1)] = 0.0f;
                }
                fft_forward(buf.data(), static_cast<std::size_t>(nFft), tables);
                for (int f = 0; f <= nFft / 2; ++f)
                    z.at(f, static_cast<int>(t), c) = cf(buf[static_cast<std::size_t>(2 * f)], buf[static_cast<std::size_t>(2 * f + 1)]);
            }
        }
    });
    return z;
}

Spec stftAt(const Audio& x, const std::vector<long long>& centers, int nFft) {
    const long long len = static_cast<long long>(x.frames());
    const int half = nFft / 2;
    const int C = x.channels;
    Spec z;
    z.resize(nFft / 2 + 1, static_cast<int>(centers.size()), C);
    const std::vector<float> w = analysisWindow(nFft);
    FftTables tables(static_cast<std::size_t>(nFft));
    parallelFor(static_cast<long long>(centers.size()), 16, [&](long long b, long long e) {
        std::vector<float> buf(static_cast<std::size_t>(2 * nFft));
        for (long long t = b; t < e; ++t) {
            for (int c = 0; c < C; ++c) {
                for (int j = 0; j < nFft; ++j) {
                    const long long src = centers[static_cast<std::size_t>(t)] + j - half;
                    const float v = (src >= 0 && src < len) ? x.at(static_cast<std::size_t>(src), c) : 0.0f;
                    buf[static_cast<std::size_t>(2 * j)] = v * w[static_cast<std::size_t>(j)];
                    buf[static_cast<std::size_t>(2 * j + 1)] = 0.0f;
                }
                fft_forward(buf.data(), static_cast<std::size_t>(nFft), tables);
                for (int f = 0; f <= nFft / 2; ++f)
                    z.at(f, static_cast<int>(t), c) = cf(buf[static_cast<std::size_t>(2 * f)], buf[static_cast<std::size_t>(2 * f + 1)]);
            }
        }
    });
    return z;
}

Audio istft(const Spec& z, int nFft, int hop, std::size_t length) {
    const int count = z.T, C = z.C;
    const long long total = static_cast<long long>(count - 1) * hop + nFft;
    std::vector<double> out(static_cast<std::size_t>(total) * C, 0.0), norm(static_cast<std::size_t>(total), 0.0);
    const std::vector<float> wf = analysisWindow(nFft);
    FftTables tables(static_cast<std::size_t>(nFft));
    // irfft は並列に計算し、overlap-add は frame 順に行う。
    constexpr int kBatch = 256;
    std::vector<float> frames(static_cast<std::size_t>(kBatch) * C * nFft);
    for (int t0 = 0; t0 < count; t0 += kBatch) {
        const int tn = std::min(kBatch, count - t0);
        parallelFor(tn, 8, [&](long long b, long long e) {
            std::vector<float> buf(static_cast<std::size_t>(2 * nFft));
            for (long long tt = b; tt < e; ++tt) {
                const int t = t0 + static_cast<int>(tt);
                for (int c = 0; c < C; ++c) {
                    // Hermitian 対称に広げる。DC と Nyquist の虚部は irfft と同じく無視する。
                    for (int f = 0; f <= nFft / 2; ++f) {
                        cf v = z.at(f, t, c);
                        if (f == 0 || f == nFft / 2) v = cf(v.real(), 0.0f);
                        buf[static_cast<std::size_t>(2 * f)] = v.real();
                        buf[static_cast<std::size_t>(2 * f + 1)] = v.imag();
                        if (f > 0 && f < nFft / 2) {
                            buf[static_cast<std::size_t>(2 * (nFft - f))] = v.real();
                            buf[static_cast<std::size_t>(2 * (nFft - f) + 1)] = -v.imag();
                        }
                    }
                    fft_inverse(buf.data(), static_cast<std::size_t>(nFft), tables);
                    float* dst = frames.data() + (static_cast<std::size_t>(tt) * C + c) * nFft;
                    for (int j = 0; j < nFft; ++j) dst[j] = buf[static_cast<std::size_t>(2 * j)];
                }
            }
        });
        for (int tt = 0; tt < tn; ++tt) {
            const long long offset = static_cast<long long>(t0 + tt) * hop;
            for (int j = 0; j < nFft; ++j) {
                const double w = wf[static_cast<std::size_t>(j)];
                for (int c = 0; c < C; ++c)
                    out[static_cast<std::size_t>(offset + j) * C + c] += frames[(static_cast<std::size_t>(tt) * C + c) * nFft + j] * w;
                norm[static_cast<std::size_t>(offset + j)] += w * w;
            }
        }
    }
    Audio y;
    y.channels = C;
    y.v.assign(length * static_cast<std::size_t>(C), 0.0f);
    const int half = nFft / 2;
    for (std::size_t i = 0; i < length; ++i) {
        const long long at = half + static_cast<long long>(i);
        if (at >= total) break;
        const double nv = std::max(norm[static_cast<std::size_t>(at)], 1e-12);
        for (int c = 0; c < C; ++c) y.at(i, c) = static_cast<float>(out[static_cast<std::size_t>(at) * C + c] / nv);
    }
    return y;
}

Spec applyTransfer(const Spec& x, const Transfer& h) {
    Spec y;
    y.resize(x.F, x.T, h.O);
    parallelFor(x.F, 8, [&](long long b, long long e) {
        for (long long f = b; f < e; ++f)
            for (int t = 0; t < x.T; ++t)
                for (int o = 0; o < h.O; ++o) {
                    cf acc(0, 0);
                    for (int d = 0; d < x.C; ++d) acc += x.at(static_cast<int>(f), t, d) * h.at(static_cast<int>(f), d, o);
                    y.at(static_cast<int>(f), t, o) = acc;
                }
    });
    return y;
}

double quantile(std::vector<double> v, double q) {
    v.erase(std::remove_if(v.begin(), v.end(), [](double x) { return std::isnan(x); }), v.end());
    if (v.empty()) return std::numeric_limits<double>::quiet_NaN();
    std::sort(v.begin(), v.end());
    const double pos = q * static_cast<double>(v.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(std::floor(pos));
    const std::size_t hi = std::min(lo + 1, v.size() - 1);
    const double t = pos - static_cast<double>(lo);
    // numpy の _lerp と同じ形 (t >= 0.5 では上側から引く)。
    return t >= 0.5 ? v[hi] - (v[hi] - v[lo]) * (1.0 - t) : v[lo] + (v[hi] - v[lo]) * t;
}

double median(std::vector<double> v) { return quantile(std::move(v), 0.5); }

double besselI0(double x) {
    double sum = 1.0, term = 1.0;
    const double q = 0.25 * x * x;
    for (int k = 1; k < 500; ++k) {
        term *= q / (static_cast<double>(k) * k);
        sum += term;
        if (term < sum * 1e-17) break;
    }
    return sum;
}

void fftDouble(std::vector<cd>& a, bool inverse) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = (inverse ? 2.0 : -2.0) * kPi / static_cast<double>(len);
        const std::size_t halfLen = len / 2;
        std::vector<cd> tw(halfLen);
        for (std::size_t k = 0; k < halfLen; ++k) tw[k] = std::polar(1.0, ang * static_cast<double>(k));
        parallelFor(static_cast<long long>(n / len), std::max<long long>(1, 16384 / static_cast<long long>(len)),
                    [&](long long b, long long e) {
            for (long long blk = b; blk < e; ++blk) {
                const std::size_t base = static_cast<std::size_t>(blk) * len;
                for (std::size_t k = 0; k < halfLen; ++k) {
                    const cd u = a[base + k];
                    const cd v = a[base + k + halfLen] * tw[k];
                    a[base + k] = u + v;
                    a[base + k + halfLen] = u - v;
                }
            }
        });
    }
    if (inverse)
        for (cd& v : a) v /= static_cast<double>(n);
}

}
}
