// 処理時間の計測。4 分の stereo 曲 (少しずつずれるインスト) を作り、設定ごとの所要時間を出す。
// GPU と CPU の比較や、どこが重いかの確認に使う。

#include "utagoe.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace utagoe;

namespace {

const double PI = 3.14159265358979323846;

void makeSong(int seconds, AudioBuffer& orig, AudioBuffer& inst) {
    const int rate = 44100;
    const std::size_t frames = static_cast<std::size_t>(rate) * seconds;
    std::vector<double> band(frames + 8000);
    uint32_t rng = 2024;
    double lp = 0.0;
    for (double& b : band) {
        rng = rng * 1664525u + 1013904223u;
        lp = 0.9 * lp + 0.1 * (static_cast<double>(rng >> 8) / 8388608.0 - 1.0);
        b = 0.3 * lp;
    }
    orig.sampleRate = inst.sampleRate = rate;
    orig.channels = inst.channels = 2;
    orig.samples.resize(frames * 2);
    inst.samples.resize(frames * 2);
    auto q16 = [](double v) { return static_cast<float>(std::nearbyint(v * 32768.0) / 32768.0); };
    for (std::size_t f = 0; f < frames; ++f) {
        const double t = static_cast<double>(f) / rate;
        const double voc = 0.15 * std::sin(2 * PI * 330 * t) * (0.6 + 0.4 * std::sin(2 * PI * 0.7 * t));
        const double src = static_cast<double>(f) + 4000.0 - 2.0 * t;
        const std::size_t k = static_cast<std::size_t>(src);
        const double fr = src - static_cast<double>(k);
        const double b = band[k] * (1 - fr) + band[k + 1] * fr;
        inst.samples[f * 2] = q16(band[f + 4000]);
        inst.samples[f * 2 + 1] = q16(band[f + 4000] * 0.8);
        orig.samples[f * 2] = q16(b + voc);
        orig.samples[f * 2 + 1] = q16(b * 0.8 + voc);
    }
}

}

int main(int argc, char** argv) {
    const int seconds = argc > 1 ? std::atoi(argv[1]) : 240;
    const char* only = argc > 2 && argv[2][0] ? argv[2] : nullptr;
    AudioBuffer orig, inst;
    makeSong(seconds, orig, inst);
    std::printf("%d s stereo 44.1 kHz\n", seconds);

    struct Case { const char* name; void (*set)(Settings&); bool q16; };
    const Case cases[] = {
        {"analysis only (wave, 16-bit)", [](Settings& s) { s.mergeMode = MergeMode::ByWaveform; }, true},
        {"wave, no analysis",            [](Settings& s) { s.mergeMode = MergeMode::ByWaveform; s.introMode = IntroMode::None;
                                                           s.adptMode = AdptMode::Manual; s.krkPhase = KrkPhase::Positive; s.levelAdpt = LevelAdpt::None; }, true},
        {"wave adaptive, no analysis",   [](Settings& s) { s.mergeMode = MergeMode::ByWaveform; s.introMode = IntroMode::None;
                                                           s.adptMode = AdptMode::Manual; s.krkPhase = KrkPhase::Positive; s.levelAdpt = LevelAdpt::AutoAdaptive; }, true},
        {"wave ovs x32, no analysis",    [](Settings& s) { s.mergeMode = MergeMode::ByWaveform; s.oversample = true; s.oversampleMul = 32; s.introMode = IntroMode::None;
                                                           s.adptMode = AdptMode::Manual; s.krkPhase = KrkPhase::Positive; s.levelAdpt = LevelAdpt::None; }, true},
        {"wave ovs x128, no analysis",   [](Settings& s) { s.mergeMode = MergeMode::ByWaveform; s.oversample = true; s.oversampleMul = 128; s.introMode = IntroMode::None;
                                                           s.adptMode = AdptMode::Manual; s.krkPhase = KrkPhase::Positive; s.levelAdpt = LevelAdpt::None; }, true},
        {"freq, no analysis",            [](Settings& s) { s.introMode = IntroMode::None; s.adptMode = AdptMode::Manual;
                                                           s.krkPhase = KrkPhase::Positive; s.levelAdpt = LevelAdpt::None; }, true},
        {"freq + cntr, no analysis",     [](Settings& s) { s.centralize = true; s.introMode = IntroMode::None; s.adptMode = AdptMode::Manual;
                                                           s.krkPhase = KrkPhase::Positive; s.levelAdpt = LevelAdpt::None; }, true},
        {"wave + LPF/HPF, no analysis",  [](Settings& s) { s.mergeMode = MergeMode::ByWaveform; s.lowPass = s.highPass = true; s.introMode = IntroMode::None;
                                                           s.adptMode = AdptMode::Manual; s.krkPhase = KrkPhase::Positive; s.levelAdpt = LevelAdpt::None; }, true},
        {"model robust (v3 align)",      [](Settings& s) { s.mergeMode = MergeMode::ByWaveform; s.waveModel = WaveModel::Robust; }, false},
        {"model ensemble (v3 align)",    [](Settings& s) { s.mergeMode = MergeMode::ByWaveform; s.waveModel = WaveModel::Ensemble; }, false},
        {"model robust (GCC)",           [](Settings& s) { s.mergeMode = MergeMode::ByWaveform; s.waveModel = WaveModel::Robust; s.waveAlign = WaveAlign::Gcc; }, false},
        {"model kalman (GCC)",           [](Settings& s) { s.mergeMode = MergeMode::ByWaveform; s.waveModel = WaveModel::Kalman; s.waveAlign = WaveAlign::Gcc; }, false},
        {"model nmf (GCC)",              [](Settings& s) { s.mergeMode = MergeMode::ByWaveform; s.waveModel = WaveModel::Nmf; s.waveAlign = WaveAlign::Gcc; }, false},
        {"model spatial (GCC)",          [](Settings& s) { s.mergeMode = MergeMode::ByWaveform; s.waveModel = WaveModel::Spatial; s.waveAlign = WaveAlign::Gcc; }, false},
        {"model ensemble (GCC)",         [](Settings& s) { s.mergeMode = MergeMode::ByWaveform; s.waveModel = WaveModel::Ensemble; s.waveAlign = WaveAlign::Gcc; }, false},
        {"defaults (freq + analysis)",   [](Settings&) {}, true},
        {"defaults, float path",         [](Settings&) {}, false},
    };

    for (const Case& c : cases) {
        if (only && !std::strstr(c.name, only)) continue;
        Settings s;
        c.set(s);
        // 第 3 引数: off (CPU のみ) / exact / fast
        const std::string mode = argc > 3 ? argv[3] : "exact";
        s.useGpu = mode != "off";
        s.gpuMode = mode == "fast" ? GpuMode::Fastest : GpuMode::Exact;
        const auto t0 = std::chrono::steady_clock::now();
        const ExtractResult r = extract(orig, inst, s, c.q16);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("  %-32s %8.0f ms  %s\n", c.name, ms, r ? r.alignment.debugString().c_str() : r.error.c_str());
        std::fflush(stdout);
    }
    return 0;
}
