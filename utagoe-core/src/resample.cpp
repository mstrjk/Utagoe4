// 入力同士の sample rate / channel 数をそろえる。
// インストは原曲と同じ内容なので、形式差を十分な精度で吸収できれば以降の減算がそのまま効く。
// rate 変換は Kaiser 窓 sinc。任意比率に対応し、出力位置は整数演算で求めて長尺でも drift しない。

#include "utagoe.h"
#include "mathconst.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

namespace utagoe {
namespace {


// sinc の片側ゼロ交差数。多いほど遷移帯域が狭くなる。128 で遷移幅は低い方の rate の約 2.5%。
constexpr int kZeroCrossings = 128;
// table の分解能 (ゼロ交差 1 つあたりの点数)。線形補間で誤差は約 -118 dB。
constexpr int kTableRes = 1024;
// 阻止域減衰 100 dB 相当の Kaiser beta。
constexpr double kBeta = 10.06;
// -6 dB 点を低い方の Nyquist の 97.5% に置き、遷移帯域が Nyquist を越えないようにする。
constexpr double kCutoff = 0.4875;

double besselI0(double x) {
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 64; ++k) {
        term *= (x * 0.5 / k) * (x * 0.5 / k);
        sum += term;
        if (term < sum * 1e-17) break;
    }
    return sum;
}

// u はゼロ交差単位の位置。table[i] = sinc(u) * kaiser(u / Z)、u = i / kTableRes。
const std::vector<float>& kernelTable() {
    static const std::vector<float> table = [] {
        std::vector<float> t(static_cast<std::size_t>(kZeroCrossings) * kTableRes + 2, 0.0f);
        const double i0b = besselI0(kBeta);
        for (std::size_t i = 0; i + 1 < t.size(); ++i) {
            const double u = static_cast<double>(i) / kTableRes;
            if (u >= kZeroCrossings) break;
            const double sinc = (i == 0) ? 1.0 : std::sin(kPi * u) / (kPi * u);
            const double r = u / kZeroCrossings;
            const double win = besselI0(kBeta * std::sqrt(1.0 - r * r)) / i0b;
            t[i] = static_cast<float>(sinc * win);
        }
        return t;
    }();
    return table;
}

// 1 channel 分の rate 変換。進捗と中断は thread 0 だけが扱う。
void resampleChannel(const AudioBuffer& in, int c, int outRate, std::vector<float>& out,
                     std::size_t outFrames, std::atomic<bool>& cancel,
                     bool (*progress)(float, void*), void* user, bool reporter) {
    const auto& table = kernelTable();
    const int ch = in.channels;
    const long long inRate = in.sampleRate;
    const std::size_t inFrames = in.frames();

    const double ratio = static_cast<double>(outRate) / static_cast<double>(inRate);
    // 間引き時は kernel を広げ、帯域を出力側の Nyquist に合わせる。
    const double fc = kCutoff * std::min(1.0, ratio);          // 周波数は input sample 単位。
    const double twoFc = 2.0 * fc;
    const double halfLen = kZeroCrossings / twoFc;             // 片側長は input sample 単位。
    const long long taps = static_cast<long long>(std::ceil(halfLen));

    for (std::size_t m = 0; m < outFrames; ++m) {
        // 出力 m の入力上の位置を整数で求める: pos = m * inRate / outRate。
        const long long num = static_cast<long long>(m) * inRate;
        const long long i0 = num / outRate;
        const double frac = static_cast<double>(num % outRate) / outRate;

        double acc = 0.0;
        const long long lo = std::max<long long>(0, i0 - taps + 1);
        const long long hi = std::min<long long>(static_cast<long long>(inFrames) - 1, i0 + taps);
        for (long long i = lo; i <= hi; ++i) {
            const double u = std::fabs((static_cast<double>(i - i0) - frac) * twoFc) * kTableRes;
            const std::size_t k = static_cast<std::size_t>(u);
            if (k + 1 >= table.size()) continue;
            const double w = table[k] + (table[k + 1] - table[k]) * (u - static_cast<double>(k));
            acc += static_cast<double>(in.samples[static_cast<std::size_t>(i) * ch + c]) * w;
        }
        out[m * ch + c] = static_cast<float>(acc * twoFc);

        if ((m & 0xFFFF) == 0) {
            if (cancel.load()) return;
            if (reporter && progress &&
                !progress(static_cast<float>(m) / static_cast<float>(outFrames), user)) {
                cancel.store(true);
                return;
            }
        }
    }
}

// 標準的な WAVE channel 順 (L R C LFE Ls Rs ...) を前提に stereo へ downmix する。LFE は捨てる。
void downmixToStereo(const AudioBuffer& in, std::vector<float>& out) {
    const int ch = in.channels;
    const std::size_t frames = in.frames();
    const float g = 0.70710678f;
    out.assign(frames * 2, 0.0f);
    for (std::size_t f = 0; f < frames; ++f) {
        const float* s = &in.samples[f * ch];
        float l = s[0], r = s[1];
        switch (ch) {
            case 3:  l += g * s[2]; r += g * s[2]; break;                     // 3ch は L R C の順。
            case 4:  l += g * s[2]; r += g * s[3]; break;                     // 4ch は L R Ls Rs の順。
            case 5:  l += g * (s[2] + s[3]); r += g * (s[2] + s[4]); break;   // 5ch は L R C Ls Rs の順。
            default:                                                          // 5.1 / 7.1ch は標準の並びを使う。
                l += g * s[2] + g * s[4];
                r += g * s[2] + g * s[5];
                if (ch >= 8) { l += g * s[6]; r += g * s[7]; }
                break;
        }
        out[f * 2] = l;
        out[f * 2 + 1] = r;
    }
}

AudioBuffer remix(const AudioBuffer& in, int targetChannels) {
    AudioBuffer out;
    out.sampleRate = in.sampleRate;
    out.channels = targetChannels;
    const std::size_t frames = in.frames();

    if (in.channels == targetChannels) {
        out.samples = in.samples;
        return out;
    }

    std::vector<float> stereo;
    const float* src = in.samples.data();
    int srcCh = in.channels;
    if (in.channels > 2) {
        downmixToStereo(in, stereo);
        src = stereo.data();
        srcCh = 2;
        if (targetChannels == 2) {
            out.samples = std::move(stereo);
            return out;
        }
    }

    out.samples.resize(frames * static_cast<std::size_t>(targetChannels));
    for (std::size_t f = 0; f < frames; ++f) {
        if (srcCh == 1) {
            for (int c = 0; c < targetChannels; ++c) out.samples[f * targetChannels + c] = src[f];
        } else {
            out.samples[f] = 0.5f * (src[f * 2] + src[f * 2 + 1]);
        }
    }
    return out;
}

}

AudioBuffer conformAudio(const AudioBuffer& in, int targetRate, int targetChannels,
                         bool (*progress)(float, void*), void* user) {
    // channel を先にそろえる。downmix 後の方が rate 変換の計算量が少ない。
    AudioBuffer mixed = remix(in, targetChannels);
    if (in.sampleRate == targetRate || in.frames() == 0) return mixed;

    AudioBuffer out;
    out.sampleRate = targetRate;
    out.channels = targetChannels;
    const std::size_t outFrames = static_cast<std::size_t>(
        (static_cast<long long>(mixed.frames()) * targetRate + mixed.sampleRate - 1) / mixed.sampleRate);
    out.samples.assign(outFrames * static_cast<std::size_t>(targetChannels), 0.0f);

    std::atomic<bool> cancel{false};
    std::vector<std::thread> workers;
    for (int c = 1; c < targetChannels; ++c)
        workers.emplace_back(resampleChannel, std::cref(mixed), c, targetRate, std::ref(out.samples),
                             outFrames, std::ref(cancel), nullptr, nullptr, false);
    resampleChannel(mixed, 0, targetRate, out.samples, outFrames, cancel, progress, user, true);
    for (auto& t : workers) t.join();

    if (cancel.load()) return AudioBuffer{{}, targetRate, targetChannels};
    if (progress) progress(1.0f, user);
    return out;
}

}
