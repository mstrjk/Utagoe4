#ifndef UTAGOE_RP_INTERNAL_H
#define UTAGOE_RP_INTERNAL_H

#include "rp.h"
#include "parallel.h"

#include <complex>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace utagoe {
namespace rp {

constexpr double kEps = 1e-12;
constexpr double kPi = 3.14159265358979323846;
using cd = std::complex<double>;

struct Mat {
    int rows = 0, cols = 0;
    std::vector<double> v;
    Mat() = default;
    Mat(int r, int c, double fill = 0.0) : rows(r), cols(c), v(static_cast<std::size_t>(r) * c, fill) {}
    double& at(int r, int c) { return v[static_cast<std::size_t>(r) * cols + c]; }
    double at(int r, int c) const { return v[static_cast<std::size_t>(r) * cols + c]; }
    const double* row(int r) const { return v.data() + static_cast<std::size_t>(r) * cols; }
    double* row(int r) { return v.data() + static_cast<std::size_t>(r) * cols; }
};

struct Bank {
    std::map<std::string, Mat> features;
    Mat magnitude;
    std::vector<double> frequency;
    std::vector<double> energy;
    double dt = 0, duration = 0, sideFraction = 0;
};

class Rng {
public:
    explicit Rng(std::uint64_t seed) : s_(seed ? seed : 0x9E3779B97F4A7C15ULL) {}
    std::uint64_t next() {
        std::uint64_t z = (s_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    long long below(long long n) { return n <= 0 ? 0 : static_cast<long long>(next() % static_cast<std::uint64_t>(n)); }
    double uniform() { return (next() >> 11) * (1.0 / 9007199254740992.0); }
    double normal();

private:
    std::uint64_t s_;
    bool have_ = false;
    double spare_ = 0;
};

std::uint32_t crc32(const std::string& s);
void fft(std::vector<cd>& a, bool inverse);
std::vector<cd> rfft(const std::vector<double>& x, std::size_t n);
std::vector<double> irfft(const std::vector<cd>& X, std::size_t n);
std::vector<double> convolveFull(const std::vector<double>& x, const std::vector<double>& h);
std::vector<double> convolveSame(const std::vector<double>& x, const std::vector<double>& h);
std::vector<double> correlateValid(const std::vector<double>& r, const std::vector<double>& q);
std::vector<double> hannPeriodic(int n);
double median(std::vector<double> v);
double quantile(std::vector<double> v, double q);
double mean(const std::vector<double>& v);
double stdev(const std::vector<double>& v);
std::vector<long long> findPeaksDistance(const std::vector<double>& x, long long distance);
double minimizeBounded(const std::function<double(double)>& f, double lo, double hi, double xatol, int maxiter, double& fval);
Mat matmulT(const Mat& a, const Mat& bt);
void rowsUnit(Mat& x, bool center);
void gaussian1d(Mat& x, double sigma, int order);
void medianFilterAxis(const Mat& in, Mat& out, int size, int axis);
Mat maxFilter2d(const Mat& in, int sizeRows, int sizeCols);
Audio sincSample(const Audio& a, const std::vector<double>& pos, int radius);
Audio slicePad(const Audio& a, long long start, long long length);
Audio guideAudio(const Audio& a, const std::string& mode);
void corrPeak(const Audio& r, const Audio& y, double& peak, double& quality);
Bank buildBank(const Audio& audio, int sr, const Config& cfg);
void patches(const Mat& x, double dt, double context, int stride, int bins, bool center, Mat& p, std::vector<int>& idx);
std::vector<Proposal> runWorker(const std::string& name, const Audio& audio, int sr, const Bank& bank, const Config& cfg);
const std::vector<std::pair<std::string, std::pair<std::string, double>>>& workerInfo();
void alignRegion(const Audio& audio, int sr, const Match& m, const Config& cfg, Audio& ref, Audio& y,
                 std::vector<char>& valid, std::string& guide, double& delay, double& ratePpm);
void foldMasks(std::size_t n, int sr, const std::vector<char>& valid, std::vector<char>& a, std::vector<char>& b, std::vector<char>& c);
double robustGain(const Audio& x, const Audio& y, const std::vector<char>& mask);
Metrics metrics(const Audio& y, const Audio& p, const std::vector<char>& mask);

}
}

#endif
