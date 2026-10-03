#include "upmix_internal.h"
#include "../refcancel/rc.h"
#include "../hardpair/hp.h"
#include "parallel.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace utagoe {
namespace upmix {
namespace {

std::vector<double> hannPeriodic(int n) {
    std::vector<double> w(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) w[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / n);
    return w;
}

double besselI0(double v) {
    double s = 1, t = 1;
    for (int k = 1; k < 80; ++k) {
        t *= (v / (2.0 * k)) * (v / (2.0 * k));
        s += t;
        if (t < 1e-17 * s) break;
    }
    return s;
}

std::size_t pow2(std::size_t n) {
    std::size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

}

const char* methodName(Method m) {
    switch (m) {
    case Method::Angle: return "Angle";
    case Method::Slice: return "Slice";
    case Method::Hybrid: return "Hybrid";
    case Method::FoldExact: return "Fold exact";
    case Method::GuidedScene: return "Guided scene";
    case Method::SceneVector: return "Scene vector";
    case Method::ReferenceScene: return "Reference scene";
    case Method::ContrastCov: return "Contrast covariance";
    case Method::ModulationLock: return "Modulation lock";
    case Method::VocalAnchor: return "Vocal anchor";
    }
    return "?";
}

bool needsReference(Method m) {
    return m == Method::GuidedScene || m == Method::ReferenceScene || m == Method::ContrastCov || m == Method::ModulationLock || m == Method::VocalAnchor;
}

bool supportsSevenOne(Method m) { return m == Method::SceneVector || m == Method::ReferenceScene; }

std::vector<double> channelOf(const std::vector<double>& x, int channels, int c) {
    const std::size_t n = x.size() / static_cast<std::size_t>(channels);
    std::vector<double> out(n);
    for (std::size_t i = 0; i < n; ++i) out[i] = x[i * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)];
    return out;
}

CField stft(const std::vector<double>& x, int n, int hop) {
    const long long len = static_cast<long long>(x.size());
    const int half = n / 2;
    const long long padded = len + 2LL * half;
    const long long extra = ((-(padded - n)) % hop + hop) % hop;
    const int T = static_cast<int>((padded + extra - n) / hop + 1);
    const int F = n / 2 + 1;
    const std::vector<double> w = hannPeriodic(n);
    const double wsum = std::accumulate(w.begin(), w.end(), 0.0);
    CField z(F, T);
    parallelFor(T, 4, [&](long long b, long long e) {
        std::vector<cd> buf(static_cast<std::size_t>(n));
        for (long long t = b; t < e; ++t) {
            for (int j = 0; j < n; ++j) {
                const long long i = t * hop - half + j;
                buf[static_cast<std::size_t>(j)] = cd((i >= 0 && i < len) ? x[static_cast<std::size_t>(i)] * w[static_cast<std::size_t>(j)] : 0.0, 0.0);
            }
            rc::fftDouble(buf, false);
            for (int f = 0; f < F; ++f) z.at(f, static_cast<int>(t)) = buf[static_cast<std::size_t>(f)] / wsum;
        }
    });
    return z;
}

std::vector<double> istft(const CField& z, int n, int hop, std::size_t length) {
    const int T = z.T, F = z.F, half = n / 2;
    const std::vector<double> w = hannPeriodic(n);
    const double wsum = std::accumulate(w.begin(), w.end(), 0.0);
    std::vector<double> frames(static_cast<std::size_t>(T) * static_cast<std::size_t>(n));
    parallelFor(T, 4, [&](long long b, long long e) {
        std::vector<cd> buf(static_cast<std::size_t>(n));
        for (long long t = b; t < e; ++t) {
            std::fill(buf.begin(), buf.end(), cd(0, 0));
            for (int f = 0; f < F && f <= n / 2; ++f) buf[static_cast<std::size_t>(f)] = z.at(f, static_cast<int>(t));
            buf[0] = cd(buf[0].real(), 0);
            buf[static_cast<std::size_t>(n / 2)] = cd(buf[static_cast<std::size_t>(n / 2)].real(), 0);
            for (int f = 1; f < n / 2; ++f) buf[static_cast<std::size_t>(n - f)] = std::conj(buf[static_cast<std::size_t>(f)]);
            rc::fftDouble(buf, true);
            double* out = frames.data() + static_cast<std::size_t>(t) * static_cast<std::size_t>(n);
            for (int j = 0; j < n; ++j) out[j] = buf[static_cast<std::size_t>(j)].real() * wsum * w[static_cast<std::size_t>(j)];
        }
    });
    const std::size_t total = static_cast<std::size_t>(T - 1) * static_cast<std::size_t>(hop) + static_cast<std::size_t>(n);
    std::vector<double> y(total, 0.0), norm(total, 0.0);
    for (int t = 0; t < T; ++t) {
        const double* in = frames.data() + static_cast<std::size_t>(t) * static_cast<std::size_t>(n);
        const std::size_t o = static_cast<std::size_t>(t) * static_cast<std::size_t>(hop);
        for (int j = 0; j < n; ++j) {
            y[o + static_cast<std::size_t>(j)] += in[j];
            norm[o + static_cast<std::size_t>(j)] += w[static_cast<std::size_t>(j)] * w[static_cast<std::size_t>(j)];
        }
    }
    for (std::size_t i = 0; i < total; ++i)
        if (norm[i] > 1e-10) y[i] /= norm[i];
    std::vector<double> out(length, 0.0);
    for (std::size_t i = 0; i < length && i + static_cast<std::size_t>(half) + static_cast<std::size_t>(half) <= total; ++i)
        if (i + static_cast<std::size_t>(half) < total - static_cast<std::size_t>(half)) out[i] = y[i + static_cast<std::size_t>(half)];
    return out;
}

std::vector<double> resamplePoly(const std::vector<double>& x, int up, int down, double beta) {
    const int g = std::gcd(up, down);
    up /= g;
    down /= g;
    if (up == 1 && down == 1) return x;
    const long long nIn = static_cast<long long>(x.size());
    const int maxRate = std::max(up, down);
    const int halfLen = 10 * maxRate;
    const int taps = 2 * halfLen + 1;
    const double fc = 1.0 / maxRate, alpha = (taps - 1) / 2.0, i0b = besselI0(beta);
    std::vector<double> h(static_cast<std::size_t>(taps));
    double sum = 0;
    for (int i = 0; i < taps; ++i) {
        const double m = i - alpha, v = fc * m * kPi;
        const double sinc = m == 0 ? 1.0 : std::sin(v) / v;
        const double r = m / alpha;
        h[static_cast<std::size_t>(i)] = fc * sinc * besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b;
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
    parallelFor(nOut, 4096, [&](long long b, long long e) {
        for (long long k = b; k < e; ++k) {
            const long long m = (k + nPreRemove) * down;
            double acc = 0;
            for (long long j = m % up; j < hl && j <= m; j += up) {
                const long long src = (m - j) / up;
                if (src < nIn) acc += hp[static_cast<std::size_t>(j)] * x[static_cast<std::size_t>(src)];
            }
            y[static_cast<std::size_t>(k)] = acc;
        }
    });
    return y;
}

namespace {

template <class T>
void boxSmoothImpl(std::vector<T>& v, int F, int Tn, int freqBins, int timeFrames) {
    if (freqBins > 1) {
        const int lo = (freqBins - 1) / 2, hi = freqBins / 2;
        std::vector<T> src = v;
        parallelFor(Tn, 64, [&](long long b, long long e) {
            for (long long t = b; t < e; ++t)
                for (int f = 0; f < F; ++f) {
                    T s{};
                    for (int d = -lo; d <= hi; ++d) {
                        const int g = f + d;
                        if (g >= 0 && g < F) s += src[static_cast<std::size_t>(g) * Tn + static_cast<std::size_t>(t)];
                    }
                    v[static_cast<std::size_t>(f) * Tn + static_cast<std::size_t>(t)] = s / static_cast<double>(freqBins);
                }
        });
    }
    if (timeFrames > 1) {
        const int lo = (timeFrames - 1) / 2, hi = timeFrames / 2;
        std::vector<T> src = v;
        parallelFor(F, 16, [&](long long b, long long e) {
            for (long long f = b; f < e; ++f)
                for (int t = 0; t < Tn; ++t) {
                    T s{};
                    for (int d = -lo; d <= hi; ++d) {
                        const int g = t + d;
                        if (g >= 0 && g < Tn) s += src[static_cast<std::size_t>(f) * Tn + static_cast<std::size_t>(g)];
                    }
                    v[static_cast<std::size_t>(f) * Tn + static_cast<std::size_t>(t)] = s / static_cast<double>(timeFrames);
                }
        });
    }
}

std::vector<double> gaussKernel(double sigma) {
    const int r = static_cast<int>(4.0 * sigma + 0.5);
    std::vector<double> k(static_cast<std::size_t>(2 * r + 1));
    double s = 0;
    for (int i = -r; i <= r; ++i) {
        k[static_cast<std::size_t>(i + r)] = std::exp(-0.5 * i * i / (sigma * sigma));
        s += k[static_cast<std::size_t>(i + r)];
    }
    for (double& v : k) v /= s;
    return k;
}

}

void boxSmooth(Field& a, int freqBins, int timeFrames) { boxSmoothImpl(a.v, a.F, a.T, freqBins, timeFrames); }
void boxSmooth(CField& a, int freqBins, int timeFrames) { boxSmoothImpl(a.v, a.F, a.T, freqBins, timeFrames); }

void gaussianNearest(Field& a, double sigmaF, double sigmaT) {
    const int F = a.F, T = a.T;
    if (sigmaF > 0) {
        const std::vector<double> k = gaussKernel(sigmaF);
        const int r = static_cast<int>(k.size() / 2);
        const std::vector<double> src = a.v;
        parallelFor(T, 64, [&](long long b, long long e) {
            for (long long t = b; t < e; ++t)
                for (int f = 0; f < F; ++f) {
                    double s = 0;
                    for (int d = -r; d <= r; ++d) s += k[static_cast<std::size_t>(d + r)] * src[static_cast<std::size_t>(std::clamp(f + d, 0, F - 1)) * T + static_cast<std::size_t>(t)];
                    a.v[static_cast<std::size_t>(f) * T + static_cast<std::size_t>(t)] = s;
                }
        });
    }
    if (sigmaT > 0) {
        const std::vector<double> k = gaussKernel(sigmaT);
        const int r = static_cast<int>(k.size() / 2);
        const std::vector<double> src = a.v;
        parallelFor(F, 16, [&](long long b, long long e) {
            for (long long f = b; f < e; ++f)
                for (int t = 0; t < T; ++t) {
                    double s = 0;
                    for (int d = -r; d <= r; ++d) s += k[static_cast<std::size_t>(d + r)] * src[static_cast<std::size_t>(f) * T + static_cast<std::size_t>(std::clamp(t + d, 0, T - 1))];
                    a.v[static_cast<std::size_t>(f) * T + static_cast<std::size_t>(t)] = s;
                }
        });
    }
}

double quantile(std::vector<double> v, double q) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double pos = q * static_cast<double>(v.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(pos), hi = std::min(lo + 1, v.size() - 1);
    return v[lo] + (v[hi] - v[lo]) * (pos - static_cast<double>(lo));
}

double median(std::vector<double> v) { return quantile(std::move(v), 0.5); }

std::vector<double> phasePreservingLfe(const std::vector<double>& mid, int sr, double gainDb) {
    const std::size_t n = mid.size();
    if (gainDb <= -100.0 || n == 0) return std::vector<double>(n, 0.0);
    const std::size_t N = pow2(static_cast<std::size_t>(3.0 * sr));
    std::vector<cd> spec(N, cd(0, 0));
    for (std::size_t k = 0; k <= N / 2; ++k) {
        const double f = static_cast<double>(k) * sr / static_cast<double>(N);
        double m = 0;
        if (f >= 20.0 && f <= 90.0) m = 1;
        else if (f >= 15.0 && f < 20.0) m = 0.5 - 0.5 * std::cos(kPi * (f - 15.0) / 5.0);
        else if (f > 90.0 && f <= 100.0) m = 0.5 + 0.5 * std::cos(kPi * (f - 90.0) / 10.0);
        spec[k] = cd(m, 0);
        if (k > 0 && k < N / 2) spec[N - k] = cd(m, 0);
    }
    rc::fftDouble(spec, true);
    std::vector<double> kernel(N);
    for (std::size_t i = 0; i < N; ++i) kernel[(i + N / 2) % N] = spec[i].real();
    const std::size_t block = N, M = 2 * N;
    std::vector<cd> K(M, cd(0, 0));
    for (std::size_t i = 0; i < N; ++i) K[i] = cd(kernel[i], 0);
    rc::fftDouble(K, false);
    std::vector<double> full(n + N, 0.0);
    for (std::size_t start = 0; start < n; start += block) {
        std::vector<cd> buf(M, cd(0, 0));
        for (std::size_t i = 0; i < block && start + i < n; ++i) buf[i] = cd(mid[start + i], 0);
        rc::fftDouble(buf, false);
        for (std::size_t i = 0; i < M; ++i) buf[i] *= K[i];
        rc::fftDouble(buf, true);
        for (std::size_t i = 0; i < M && start + i < full.size(); ++i) full[start + i] += buf[i].real();
    }
    const double g = std::pow(10.0, gainDb / 20.0);
    std::vector<double> out(n);
    for (std::size_t i = 0; i < n; ++i) out[i] = full[i + N / 2] * g;
    return out;
}

std::vector<double> butterLowpassLfe(const std::vector<double>& mono, int sr, double cutoffHz, double gainDb) {
    const std::size_t n = mono.size();
    if (cutoffHz <= 0) return std::vector<double>(n, 0.0);
    const double wn = std::min(cutoffHz / (sr * 0.5), 0.95);
    const int order = 4;
    const double warped = 4.0 * std::tan(kPi * wn / 2.0);
    hp::Sos sos;
    std::vector<cd> poles;
    cd prod(1, 0);
    for (int k = 0; k < order; ++k) {
        const cd pa = std::polar(1.0, kPi * (2.0 * k + order + 1) / (2.0 * order)) * warped;
        poles.push_back((4.0 + pa) / (4.0 - pa));
        prod *= (4.0 - pa);
    }
    const double gain = (std::pow(warped, order) / prod).real();
    bool first = true;
    for (const cd& p : poles) {
        if (p.imag() < 0) continue;
        const double g = first ? gain : 1.0;
        first = false;
        sos.s.push_back({g, 2 * g, g, 1.0, -2.0 * p.real(), std::norm(p)});
    }
    std::vector<double> y = n > 64 ? hp::sosfiltfilt(sos, mono) : mono;
    const double g = std::pow(10.0, gainDb / 20.0);
    for (double& v : y) v *= g;
    return y;
}

}
}
