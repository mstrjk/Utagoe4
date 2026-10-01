// 元実装の CenterFocus (0x404f5c / 0x405398) に相当する Kaiser 窓 FIR。
// ローパスとハイパスで使う。名前は紛らわしいが、CFIRUnit は実 FFT ラッパーで、実際の FIR はこちら。

#ifndef UTAGOE_FIR_H
#define UTAGOE_FIR_H

#include <vector>

namespace utagoe {

class FirFilter {
public:
    // LowPass は遷移帯の中点を cutoff とする lowpass。HighPass は周波数軸を反転して設計した lowpass を変調したもの。
    enum class Mode { LowPass = 0, HighPass = 1 };

    FirFilter() = default;

    // lowHz..highHz が遷移帯。元実装では LPF が (hz, 1.2hz, 80 dB)、HPF が (0.75hz, hz, 60 dB)。
    void design(int sampleRate, float lowHz, float highHz, float attenDb, Mode mode);

    // 遅延線は呼び出し間で保持するので、連続ブロックを切れ目なく処理できる。
    // quantize が true なら元実装に合わせて入力を整数として扱い、1 sample ごとに整数へ丸める。
    // 元実装は位相補償をしないため、出力は (taps - 1) / 2 sample 遅れる。
    void process(double* out, const double* in, int count, bool quantize);

    void reset();

    // 速度優先のとき畳み込みを GPU で行う。GPU が使えなければ CPU に戻る。
    void setGpu(bool on) { gpu_ = on; }

    int taps() const { return static_cast<int>(coef_.size()); }
    const std::vector<float>& coefficients() const { return coef_; }

private:
    std::vector<float>  coef_;
    std::vector<double> delay_;
    std::vector<double> tmp_;
    std::vector<float> gpuX_, gpuH_, gpuY_;
    bool gpu_ = false;
};

namespace kaiser {

// 元実装の float 演算をそのまま写したもの。

// I0 は 20 項の級数。各項を float に丸めながら足す。
float besselI0(float x);

// 停止帯域減衰量から Kaiser beta を求める。21 dB 未満は 0。
float beta(float attenDb);

// (atten - 8) / (2.285 * dw) + 1 を丸め、奇数なら 1 足して偶数にし、1000 で打ち切る。実際のタップ数はこれに 1 足した奇数。
int order(float transitionWidthRad, float attenDb);

}

}

#endif
