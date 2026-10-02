// By Waveform の代替モデル (Reference Cancellation Lab の 5 エンジン) の C++ 移植。
// 元は Python/NumPy の研究実装 (MIT)。配列の約束事と数式はそのまま写し、結果は Python 版と照合している。
// 音声は interleaved [sample][channel]、スペクトルは [周波数][frame][channel]、伝達関数は [周波数][入力][出力]。
// 値の scale は full scale = 1.0。

#ifndef UTAGOE_RC_H
#define UTAGOE_RC_H

#include <complex>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace utagoe {
namespace rc {

using cf = std::complex<float>;
using cd = std::complex<double>;

// 調整値。既定値は Python 版 Config と同じ。
struct Config {
    int    nFft = 2048;
    int    hop = 512;
    double blockSeconds = 12.0;
    double contextSeconds = 1.0;
    int    calibrationFrames = 1800;
    std::vector<std::pair<double, double>> fitSpans;   // 声の無い区間 (秒)。空なら全体を使う

    int    alignment = 1;           // 0 = 補正なし、1 = affine、2 = constant。
    double maxOffsetSeconds = 8.0;
    double maxDriftPpm = 1500.0;
    int    alignmentAnchors = 17;
    double alignmentWindowSeconds = 1.5;
    int    sincTaps = 96;
    double minAlignmentScore = 0.12;

    int    robustIterations = 5;
    double ridge = 0.001;
    double huberDelta = 1.5;
    double smoothBins = 1.0;
    double nonlinearRidge = 0.08;
    double nonlinearMaxRelativeRms = 0.35;
    double nonlinearMinCvImprovement = 0.015;
    double kalmanProcessNoise = 3e-5;
    double kalmanMaxCorrection = 0.50;
    double kalmanMaxStep = 0.012;
    int    nmfRank = 12;
    int    nmfIterations = 35;
    double maskFloorDb = -30.0;
    int    covarianceFrames = 23;
    double covarianceLoading = 0.02;
    double ensembleStrength = 0.5;
    uint64_t seed = 0;
};

enum class Method { Robust = 0, Kalman = 1, Hammerstein = 2, Nmf = 3, Spatial = 4, Ensemble = 5 };

// 音声配列は [frames][channels]。
struct Audio {
    std::vector<float> v;
    int channels = 1;
    std::size_t frames() const { return channels ? v.size() / static_cast<std::size_t>(channels) : 0; }
    float& at(std::size_t f, int c) { return v[f * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)]; }
    float at(std::size_t f, int c) const { return v[f * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)]; }
};

// スペクトル配列は [F][T][C]。
struct Spec {
    int F = 0, T = 0, C = 0;
    std::vector<cf> v;
    void resize(int f, int t, int c) { F = f; T = t; C = c; v.assign(static_cast<std::size_t>(f) * t * c, cf(0, 0)); }
    cf& at(int f, int t, int c) { return v[(static_cast<std::size_t>(f) * T + t) * C + c]; }
    const cf& at(int f, int t, int c) const { return v[(static_cast<std::size_t>(f) * T + t) * C + c]; }
};

// 伝達関数の配列は [F][D][O]。
struct Transfer {
    int F = 0, D = 0, O = 0;
    std::vector<cf> v;
    void resize(int f, int d, int o) { F = f; D = d; O = o; v.assign(static_cast<std::size_t>(f) * d * o, cf(0, 0)); }
    cf& at(int f, int d, int o) { return v[(static_cast<std::size_t>(f) * D + d) * O + o]; }
    const cf& at(int f, int d, int o) const { return v[(static_cast<std::size_t>(f) * D + d) * O + o]; }
};

std::vector<float> analysisWindow(int n);
Spec stft(const Audio& x, int nFft, int hop);
Audio istft(const Spec& z, int nFft, int hop, std::size_t length);
Spec stftAt(const Audio& x, const std::vector<long long>& centers, int nFft);
Spec applyTransfer(const Spec& x, const Transfer& h);

// scipy.ndimage.gaussian_filter1d (mode nearest, truncate 4) と同じ。data は [n0][n1][n2] の連続配列で axis 方向に掛ける。
void gaussian(float* data, int n0, int n1, int n2, int axis, double sigma);
void gaussian(double* data, int n0, int n1, int n2, int axis, double sigma);
void gaussianComplex(cf* data, int n0, int n1, int n2, int axis, double sigma);
void gaussianComplex(cd* data, int n0, int n1, int n2, int axis, double sigma);

double quantile(std::vector<double> v, double q);   // numpy の linear 補間。NaN を除く。全部 NaN なら NaN
double median(std::vector<double> v);
double besselI0(double x);

// 倍精度の複素 FFT (長さは 2 のべき乗)。位置合わせの相互相関で使う。
void fftDouble(std::vector<cd>& a, bool inverse);

// 1. Huber-IRLS 複素 MIMO ridge 回帰。x [F,T,D], y [F,T,O]。
Transfer fitTransfer(const Spec& x, const Spec& y, const Config& cfg,
                     const Transfer* prior = nullptr, const std::vector<double>* penalties = nullptr);

// 2. 部分帯域 Kalman 追従。updateMask は frame ごと (空なら全 frame 更新)。
Spec kalmanBackground(const Spec& mixture, const Spec& baseline, const Config& cfg, const std::vector<char>& updateMask);

// 3. 多項式 Hammerstein。
struct PolynomialBasis {
    std::vector<double> scale;                        // channel ごとの配列。
    std::vector<std::vector<double>> coefficients[2]; // degree 2, 3 ごとに [前段][channel]
    std::vector<double> divisors[2];                  // channel ごとの配列。
    void fit(const Audio& referenceSamples);
    Audio transform(const Audio& x) const;            // 配列の形は [frames][channels*3]。
};
Spec hammersteinBackground(const Spec& features, const Transfer& h, const Spec& linear, bool accepted, double maxRelativeRms);

// 4. 参照分散を固定した IS-NMF。
Spec nmfBackground(const Spec& mixture, const Spec& powerReference, const Config& cfg);

// 5. 参照共分散による多チャンネル Wiener。
Spec spatialBackground(const Spec& mixture, const Spec& powerReference, const Config& cfg);

Spec consensusBackground(const std::vector<const Spec*>& candidates, const Spec& baseline, double strength);

struct Calibration {
    Transfer linear;
    Transfer nonlinear;
    bool hasNonlinear = false;
    PolynomialBasis basis;
    std::vector<float> powerGain;     // 配列の形は [F][C]。
    bool nonlinearAccepted = false;
    double medianCoherence = 0.0;
    std::vector<double> cvImprovement;
    int frames = 0;
};
Calibration calibrate(const Audio& mix, const Audio& reference, const std::vector<char>& valid,
                      int sr, const Config& cfg, bool nonlinear);

struct Alignment {
    int mode = 0;              // 0 = 補正なし、1 = affine、2 = constant。
    double offset = 0.0;       // 原曲 sample n はインストの n - (offset + slope n) に当たる
    double slope = 0.0;
    double score = 1.0;
    int anchors = 0;
    double anchorMad = 0.0;
    double delay(double n) const { return offset + slope * n; }
};
Alignment estimateAlignment(const Audio& mix, const Audio& reference, int sr, const Config& cfg);
// インストだけを原曲の時間軸へ写す。valid は参照が範囲内の sample。
Audio warpReference(const Audio& reference, std::size_t length, const Alignment& a, int taps, std::vector<char>& valid);

struct Result {
    Audio estimate;            // 原曲から背景を引いたもの
    Audio reference;           // 原曲の時間軸に合わせたインスト
    Alignment alignment;
    Calibration calibration;
    std::string error;
};
using Progress = std::function<bool(double)>;   // false で中断

// mix と reference は同じ rate / channel 数。alignment == 0 なら reference は位置合わせ済みで、valid も渡す。
Result separate(const Audio& mix, const Audio& reference, std::vector<char> valid, int sr,
                const Config& cfg, Method method, const Progress& progress);

}
}

#endif
