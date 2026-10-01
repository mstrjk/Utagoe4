// Utagoe コアのセルフテスト。
// 元バイナリがなくても検証できる要素を対象にする: FFT は reference DFT、Kaiser は既知の式、全体処理は正解を作れる synthetic case。
// 16-bit 経路は float 化前の実装で取った golden hash と一致することを確認する。

#include "fft.h"
#include "fir.h"
#include "freq_engine.h"
#include "../src/gpu.h"
#include "../src/log.h"
#include "../src/hardpair/hp.h"
#include "utagoe.h"
#include "vocal_func.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <complex>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace utagoe;
namespace fs = std::filesystem;

namespace {

static const double PI = 3.14159265358979323846;

int failures = 0;

// 出力先。DLL 版では log file を開いて差し替える。stdout を freopen すると呼び出し元 process 全体の出力まで奪うため使わない。
std::FILE* g_out = stdout;

void say(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(g_out, fmt, ap);
    va_end(ap);
}

void check(bool ok, const char* what) {
    say("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

void skip(const char* what, const std::string& why) {
    say("  [SKIP] %s (%s)\n", what, why.c_str());
}

// テスト用の一時フォルダ。日本語名にして Unicode path の扱いも同時に確かめる。
std::string tempPath(const char* name) {
    static const fs::path dir = [] {
        fs::path d = fs::temp_directory_path() / fs::u8path("utagoe テスト");
        fs::create_directories(d);
        return d;
    }();
    return (dir / fs::u8path(name)).u8string();
}

float s16(double v) { return static_cast<float>(std::lround(v) / 32768.0); }

void testFft() {
    say("FFT\n");
    double worstRel = 0.0, worstRound = 0.0;

    for (std::size_t n : {16u, 64u, 256u, 1024u}) {
        std::vector<float> a(2 * n), orig(2 * n);
        std::vector<std::complex<double>> ref(n);
        for (std::size_t i = 0; i < n; ++i) {
            const double re = std::cos(i * 0.7) + 0.3 * static_cast<double>(i) / n;
            const double im = std::sin(i * 0.3);
            a[2 * i] = static_cast<float>(re);
            a[2 * i + 1] = static_cast<float>(im);
            ref[i] = {re, im};
        }
        orig = a;

        FftTables t(n);
        fft_forward(a.data(), n, t);

        double worst = 0.0, mag = 0.0;
        for (std::size_t k = 0; k < n; ++k) {
            std::complex<double> s = 0.0;
            for (std::size_t j = 0; j < n; ++j) {
                const double ang = -2.0 * PI * static_cast<double>(k) * j / n;
                s += ref[j] * std::complex<double>(std::cos(ang), std::sin(ang));
            }
            worst = std::max(worst, std::abs(s - std::complex<double>(a[2 * k], a[2 * k + 1])));
            mag = std::max(mag, std::abs(s));
        }
        worstRel = std::max(worstRel, worst / mag);

        fft_inverse(a.data(), n, t);
        for (std::size_t i = 0; i < 2 * n; ++i)
            worstRound = std::max(worstRound, static_cast<double>(std::fabs(a[i] - orig[i])));
    }

    say("    forward rel-err %.3g, roundtrip abs-err %.3g\n", worstRel, worstRound);
    check(worstRel < 1e-5, "forward matches reference DFT");
    check(worstRound < 1e-4, "inverse(forward(x)) == x  (confirms 1/n scaling)");
}

void testKaiser() {
    say("Kaiser design\n");

    // I0 は既知値と比較する (元実装どおり float 精度)。
    check(std::fabs(kaiser::besselI0(0.0f) - 1.0f) < 1e-6f, "I0(0) == 1");
    check(std::fabs(kaiser::besselI0(1.0f) - 1.2660658777520084) < 1e-5, "I0(1) correct");
    check(std::fabs(kaiser::besselI0(3.0f) - 4.880792585865024) < 1e-4, "I0(3) correct");

    // beta は textbook の piecewise formula と比較する。
    check(kaiser::beta(10.0f) == 0.0f, "beta == 0 below 21 dB");
    check(std::fabs(kaiser::beta(60.0f) - (60.0 - 8.7) * 0.1102) < 1e-5, "beta uses 0.1102 slope above 50 dB");
    {
        const double a = 40.0 - 21.0;
        const double want = std::pow(a, 0.4) * 0.5842 + a * 0.07886;
        check(std::fabs(kaiser::beta(40.0f) - want) < 1e-4, "beta mid-range formula");
    }

    // 次数は偶数 (タップ数は奇数) で上限 1000。
    check((kaiser::order(0.1f, 60.0f) % 2) == 0, "order is even, so the tap count is odd");
    check(kaiser::order(1e-6f, 60.0f) <= 1000, "order capped at 1000");

    auto responseAt = [](const FirFilter& fir, double hz) {
        const auto& c = fir.coefficients();
        std::complex<double> h = 0.0;
        for (std::size_t i = 0; i < c.size(); ++i) {
            const double w = 2.0 * PI * hz / 44100.0;
            h += static_cast<double>(c[i]) * std::complex<double>(std::cos(-w * i), std::sin(-w * i));
        }
        return std::abs(h);
    };

    // LPF は元実装に合わせて hz..1.2hz を遷移帯にして 80 dB。
    FirFilter lpf;
    lpf.design(44100, 4000.0f, 4800.0f, 80.0f, FirFilter::Mode::LowPass);
    const double lp1 = responseAt(lpf, 1000.0), lp12 = responseAt(lpf, 12000.0);
    say("    LPF 4 kHz: %d taps, |H(1kHz)| = %.4f   |H(12kHz)| = %.2e\n", lpf.taps(), lp1, lp12);
    check(lpf.taps() % 2 == 1, "LPF tap count is odd");
    check(std::fabs(lp1 - 1.0) < 0.01, "LPF passband gain is 1");
    check(lp12 < 1e-3, "LPF stopband is attenuated");

    // HPF は 0.75hz..hz を遷移帯にして 60 dB。
    FirFilter hpf;
    hpf.design(44100, 150.0f, 200.0f, 60.0f, FirFilter::Mode::HighPass);
    const double hp50 = responseAt(hpf, 50.0), hp5k = responseAt(hpf, 5000.0);
    say("    HPF 200 Hz: %d taps, |H(50Hz)| = %.2e   |H(5kHz)| = %.4f\n", hpf.taps(), hp50, hp5k);
    check(std::fabs(hp5k - 1.0) < 0.01, "HPF passband gain is 1");
    check(hp50 < 1e-2, "HPF stopband is attenuated");
}

// 非周期の backing。周期信号だと周期の整数倍ずれが同点になり、正解が一意に決まらない。
std::vector<double> makeBacking(std::size_t frames, int rate, uint32_t seed, bool tone = true) {
    uint32_t rng = seed;
    auto noise = [&]() {
        rng = rng * 1664525u + 1013904223u;
        return (static_cast<double>(rng >> 8) / 8388608.0) - 1.0;
    };
    std::vector<double> band(frames);
    double lp = 0.0;
    for (std::size_t f = 0; f < frames; ++f) {
        lp = 0.95 * lp + 0.05 * noise();
        const double t = static_cast<double>(f) / rate;
        band[f] = (tone ? 6000.0 : 30000.0) * lp + (tone ? 2000.0 * std::sin(2 * PI * 110.0 * t) : 0.0);
    }
    return band;
}

// FFT を使う 2 つのエンジンは frame をまとめて並列に計算する。元実装のように少しずつ流しても、
// 一度に流しても結果が同じであることを確認する。
void testStreamingEngines() {
    say("Streaming engines give the same result however the input is split\n");
    const int count = 100000;
    std::vector<float> a(count), b(count);
    std::vector<double> ad(count), bd(count), lv(count);
    uint32_t rng = 31;
    for (int i = 0; i < count; ++i) {
        rng = rng * 1664525u + 1013904223u;
        a[i] = static_cast<float>(std::nearbyint(8000 * std::sin(i * 0.013) + static_cast<int>(rng >> 20) - 2048));
        b[i] = static_cast<float>(std::nearbyint(7000 * std::sin(i * 0.013 + 0.2)));
        ad[i] = a[i];
        bd[i] = b[i];
        lv[i] = 0.9 + 0.2 * ((i / 4410) % 3);   // block ごとに level が変わる
    }
    auto splits = [&](uint32_t seed, auto&& fn) {
        uint32_t r = seed;
        for (int pos = 0; pos < count;) {
            r = r * 1664525u + 1013904223u;
            const int len = std::min<int>(count - pos, 1 + static_cast<int>(r >> 19));
            fn(pos, len);
            pos += len;
        }
    };

    for (int mode = 0; mode < 2; ++mode) {
        FreqEngine one, many;
        one.init(8192, 8, mode, 1.2f, true);
        many.init(8192, 8, mode, 1.2f, true);
        std::vector<double> x(count), y(count);
        one.process(a.data(), b.data(), lv.data(), x.data(), count);
        splits(7 + mode, [&](int pos, int len) {
            many.process(a.data() + pos, b.data() + pos, lv.data() + pos, y.data() + pos, len);
        });
        check(x == y, mode == 0 ? "By Frequency (Quality)" : "By Frequency (Extraction)");
    }

    VocalFunc one, many;
    one.init(8192, 8, 2.0f, true);
    many.init(8192, 8, 2.0f, true);
    std::vector<float> l1 = a, r1 = b, l2 = a, r2 = b;
    one.process(l1.data(), r1.data(), l1.data(), r1.data(), count);
    splits(99, [&](int pos, int len) {
        many.process(l2.data() + pos, r2.data() + pos, l2.data() + pos, r2.data() + pos, len);
    });
    check(l1 == l2 && r1 == r2, "Centralization");

    FirFilter f1, f2;
    f1.design(44100, 4000.0f, 4800.0f, 80.0f, FirFilter::Mode::LowPass);
    f2.design(44100, 4000.0f, 4800.0f, 80.0f, FirFilter::Mode::LowPass);
    std::vector<double> x(count), y(count);
    f1.process(x.data(), ad.data(), count, true);
    splits(5, [&](int pos, int len) { f2.process(y.data() + pos, ad.data() + pos, len, true); });
    check(x == y, "LPF");
}

void testAlignment() {
    say("Alignment (v3 analysis)\n");

    const int rate = 44100;
    const std::size_t frames = static_cast<std::size_t>(rate) * 12;
    const int trueDelay = 137;
    const std::vector<double> band = makeBacking(frames, rate, 12345, false);
    // 実際の曲と同じく両方とも無音から始める。元実装の簡易解析は最初の有音 sample を手掛かりにする。
    const std::size_t lead = static_cast<std::size_t>(rate) / 2;

    AudioBuffer orig, inst;
    orig.sampleRate = inst.sampleRate = rate;
    orig.channels = inst.channels = 2;
    orig.samples.assign(frames * 2, 0.0f);
    inst.samples.assign(frames * 2, 0.0f);
    std::vector<double> voc(frames);

    // instrumental は backing。original は backing を trueDelay だけ遅らせて vocal を重ねたもの。
    for (std::size_t f = 0; f < frames; ++f) {
        const double t = static_cast<double>(f) / rate;
        voc[f] = f < lead + 4000 ? 0.0 : 1500.0 * std::sin(2 * PI * 440.0 * t) * (0.5 + 0.5 * std::sin(2 * PI * 1.3 * t));
        const long long j = static_cast<long long>(f) - trueDelay - static_cast<long long>(lead);
        const double delayed = (j >= 0) ? band[static_cast<std::size_t>(j)] : 0.0;
        const double b = f >= lead ? band[f - lead] : 0.0;
        for (int c = 0; c < 2; ++c) {
            inst.samples[f * 2 + c] = s16(b);
            orig.samples[f * 2 + c] = s16(delayed + voc[f]);
        }
    }

    auto vocalError = [&](const ExtractResult& r) {
        double e = 0, v = 0;
        for (std::size_t f = frames / 4; f < 3 * frames / 4; ++f) {
            const double d = r.vocal.samples[f * 2] * 32768.0 - voc[f];
            e += d * d;
            v += voc[f] * voc[f];
        }
        return 10 * std::log10(e / v);
    };

    Settings s;
    s.mergeMode = MergeMode::ByWaveform;
    const ExtractResult a = extract(orig, inst, s, true);
    check(static_cast<bool>(a), "extraction ran");
    if (!a) return;
    // インストの frame = 原曲の frame + offset なので、遅れた backing は負の offset になる。
    say("   %s\n    vocal error %.1f dB\n", a.alignment.debugString().c_str(), vocalError(a));
    check(std::abs(a.alignment.offset + trueDelay) <= a.alignment.driftRange, "recovers a known delay");
    check(!a.alignment.inverted, "does not falsely report inverted polarity");
    check(vocalError(a) < -30.0, "backing cancels");

    // 極性反転した instrumental。
    for (auto& v : inst.samples) v = -v;
    const ExtractResult b = extract(orig, inst, s, true);
    say("   flipped:%s\n    vocal error %.1f dB\n", b.alignment.debugString().c_str(), vocalError(b));
    check(b.alignment.inverted, "detects inverted polarity");
    check(vocalError(b) < -30.0, "backing cancels with inverted instrumental");
}

// 各処理経路が破綻しないことの確認。backing は少しずつずれていく (drift) 素材にする。
void testProcessingPaths() {
    say("Processing paths (drifting instrumental)\n");

    const int rate = 44100;
    const std::size_t frames = static_cast<std::size_t>(rate) * 10;
    const std::vector<double> band = makeBacking(frames + 4000, rate, 777);

    AudioBuffer orig, inst;
    orig.sampleRate = inst.sampleRate = rate;
    orig.channels = inst.channels = 2;
    orig.samples.assign(frames * 2, 0.0f);
    inst.samples.assign(frames * 2, 0.0f);
    for (std::size_t f = 0; f < frames; ++f) {
        const double t = static_cast<double>(f) / rate;
        const double voc = 2500.0 * std::sin(2 * PI * 520.0 * t);
        // 原曲の backing は 1 秒あたり 10 sample 遅れていく (再生速度のわずかな違い)。
        const double src = static_cast<double>(f) + 2000.0 - 10.0 * t;
        const std::size_t k = static_cast<std::size_t>(src);
        const double fr = src - static_cast<double>(k);
        const double b = band[k] * (1 - fr) + band[k + 1] * fr;
        inst.samples[f * 2] = s16(band[f + 2000]);
        inst.samples[f * 2 + 1] = s16(band[f + 2000] * 0.7);
        orig.samples[f * 2] = s16(b + voc);
        orig.samples[f * 2 + 1] = s16(b * 0.7 + voc);
    }

    struct Case { const char* name; ProcMode pm; MergeMode mm; bool ovs; LevelAdpt la; bool cntr; bool filters; };
    const Case cases[] = {
        {"Waveform / Normal",             ProcMode::Normal,       MergeMode::ByWaveform,  false, LevelAdpt::AutoAveraged, false, false},
        {"Waveform / L-R Difference",     ProcMode::LRDifference, MergeMode::ByWaveform,  false, LevelAdpt::AutoAveraged, false, false},
        {"Waveform / Mono",               ProcMode::Mono,         MergeMode::ByWaveform,  false, LevelAdpt::AutoAveraged, false, false},
        {"Waveform / oversampled x8",     ProcMode::Normal,       MergeMode::ByWaveform,  true,  LevelAdpt::AutoAveraged, false, false},
        {"Waveform / L-R / oversampled",  ProcMode::LRDifference, MergeMode::ByWaveform,  true,  LevelAdpt::AutoAdaptive, false, false},
        {"Waveform / Mono / oversampled", ProcMode::Mono,         MergeMode::ByWaveform,  true,  LevelAdpt::AutoAveraged, false, false},
        {"Waveform / adaptive level",     ProcMode::Normal,       MergeMode::ByWaveform,  false, LevelAdpt::AutoAdaptive, false, false},
        {"Frequency / Normal",            ProcMode::Normal,       MergeMode::ByFrequency, false, LevelAdpt::AutoAveraged, false, false},
        {"Frequency / Mono",              ProcMode::Mono,         MergeMode::ByFrequency, false, LevelAdpt::AutoAveraged, false, false},
        {"Frequency + Centralization",    ProcMode::Normal,       MergeMode::ByFrequency, false, LevelAdpt::AutoAveraged, true,  false},
        {"Waveform + LPF/HPF",            ProcMode::Normal,       MergeMode::ByWaveform,  false, LevelAdpt::AutoAveraged, false, true},
    };

    for (const Case& k : cases) {
        Settings s;
        s.procMode = k.pm;
        s.mergeMode = k.mm;
        s.oversample = k.ovs;
        s.oversampleMul = 8;
        s.levelAdpt = k.la;
        s.centralize = k.cntr;
        s.lowPass = s.highPass = k.filters;
        const ExtractResult r = extract(orig, inst, s, false);
        if (!r) { check(false, k.name); continue; }

        // 出力には処理遅延があり得るので、vocal の 520 Hz 成分とそれ以外の電力比で評価する。
        double re = 0, im = 0, total = 0;
        bool finite = true;
        for (std::size_t f = frames / 4; f < 3 * frames / 4; ++f) {
            const double v = r.vocal.samples[f * 2] * 32768.0;
            if (!std::isfinite(v)) finite = false;
            const double t = static_cast<double>(f) / rate;
            re += v * std::cos(2 * PI * 520.0 * t);
            im += v * std::sin(2 * PI * 520.0 * t);
            total += v * v;
        }
        const double count = static_cast<double>(frames / 2);
        const double tone = 2.0 * (re * re + im * im) / count / count;
        const double rest = std::max(total / count - tone, 1e-9);
        const double snr = 10 * std::log10(tone / rest);
        say("    %-30s vocal/residual %6.1f dB %s\n", k.name, snr, r.alignment.debugString().c_str());
        char what[96];
        std::snprintf(what, sizeof what, "%s keeps the vocal and removes the backing", k.name);
        check(finite && snr > 10.0, what);
    }

    // 原曲とインストに同じファイル: 後処理だけ。後処理がなければ原曲そのまま。
    Settings s;
    const ExtractResult same = extractFiltersOnly(orig, s, true);
    check(same && same.vocal.samples == orig.samples, "same file for both inputs passes the original through");
}

// GPU。使えない環境では CPU に戻ることだけを確認する。
// Exact は位置探索をすべて GPU に回しても (UTAGOE_GPU_MIN_WORK=0) CPU だけの結果と 1 bit も違わないこと、
// Fastest は CPU の結果とほぼ同じであることを確かめる。
void testGpu() {
    say("GPU\n");
    const gpu::Status st = gpu::status();
    if (!st.available) {
        say("    not available: %s\n", st.reason.c_str());
        Settings s;
        AudioBuffer o, i;
        o.samples.assign(44100 * 2 * 3, 0.0f);
        i.samples.assign(44100 * 2 * 3, 0.0f);
        check(static_cast<bool>(extract(o, i, s, true)) , "falls back to the CPU");
        return;
    }
    say("    adapter: %s\n", st.adapter.c_str());

    const int rate = 44100;
    const std::size_t frames = static_cast<std::size_t>(rate) * 8;
    const std::vector<double> band = makeBacking(frames + 4000, rate, 4321);
    AudioBuffer orig, inst;
    orig.sampleRate = inst.sampleRate = rate;
    orig.channels = inst.channels = 2;
    orig.samples.assign(frames * 2, 0.0f);
    inst.samples.assign(frames * 2, 0.0f);
    for (std::size_t f = 0; f < frames; ++f) {
        const double t = static_cast<double>(f) / rate;
        const double voc = 2500.0 * std::sin(2 * PI * 520.0 * t);
        const double src = static_cast<double>(f) + 2000.0 - 7.0 * t;
        const std::size_t k = static_cast<std::size_t>(src);
        const double fr = src - static_cast<double>(k);
        const double b = band[k] * (1 - fr) + band[k + 1] * fr;
        // 片方は 16-bit の格子、もう片方は格子外の値 (lossy の decode 結果のような値) にする。
        inst.samples[f * 2] = s16(band[f + 2000]);
        inst.samples[f * 2 + 1] = static_cast<float>(band[f + 2000] * 0.7 / 32768.0);
        orig.samples[f * 2] = s16(b + voc);
        orig.samples[f * 2 + 1] = static_cast<float>((b * 0.7 + voc) / 32768.0);
    }

    struct Case { const char* name; ProcMode pm; MergeMode mm; bool ovs; int mul; LevelAdpt la; };
    const Case cases[] = {
        {"Waveform",                  ProcMode::Normal,       MergeMode::ByWaveform,  false, 1,   LevelAdpt::AutoAveraged},
        {"Waveform L-R",              ProcMode::LRDifference, MergeMode::ByWaveform,  false, 1,   LevelAdpt::AutoAveraged},
        {"Waveform Mono",             ProcMode::Mono,         MergeMode::ByWaveform,  false, 1,   LevelAdpt::AutoAveraged},
        {"Waveform x8",               ProcMode::Normal,       MergeMode::ByWaveform,  true,  8,   LevelAdpt::AutoAveraged},
        {"Waveform L-R x8 adaptive",  ProcMode::LRDifference, MergeMode::ByWaveform,  true,  8,   LevelAdpt::AutoAdaptive},
        {"Waveform Mono x128",        ProcMode::Mono,         MergeMode::ByWaveform,  true,  128, LevelAdpt::AutoAveraged},
        {"Frequency",                 ProcMode::Normal,       MergeMode::ByFrequency, false, 1,   LevelAdpt::AutoAveraged},
    };

    _putenv("UTAGOE_GPU_MIN_WORK=0");
    for (const Case& k : cases) {
        for (int q = 0; q < 2; ++q) {
            Settings s;
            s.procMode = k.pm;
            s.mergeMode = k.mm;
            s.oversample = k.ovs;
            s.oversampleMul = k.mul;
            s.levelAdpt = k.la;
            s.useGpu = false;
            const ExtractResult cpu = extract(orig, inst, s, q == 0);
            s.useGpu = true;
            s.gpuMode = GpuMode::Exact;
            const ExtractResult exact = extract(orig, inst, s, q == 0);
            char what[128];
            std::snprintf(what, sizeof what, "Exact matches the CPU bit for bit: %s (%s)", k.name, q == 0 ? "16-bit" : "hi-res");
            check(cpu && exact && exact.gpuUsed && cpu.vocal.samples == exact.vocal.samples, what);

            if (q == 1) continue;
            s.gpuMode = GpuMode::Fastest;
            s.centralize = k.mm == MergeMode::ByFrequency;
            s.lowPass = s.highPass = k.mm == MergeMode::ByWaveform && !k.ovs;
            s.useGpu = false;
            const ExtractResult ref = extract(orig, inst, s, false);
            s.useGpu = true;
            const ExtractResult fast = extract(orig, inst, s, false);
            double e = 0, r = 0;
            for (std::size_t n = frames / 4 * 2; n < frames / 4 * 6; ++n) {
                const double d = fast.vocal.samples[n] - ref.vocal.samples[n];
                e += d * d;
                r += static_cast<double>(ref.vocal.samples[n]) * ref.vocal.samples[n];
            }
            const double db = 10 * std::log10(std::max(e, 1e-30) / r);
            say("    Fastest %-26s differs from CPU by %6.1f dB\n", k.name, db);
            std::snprintf(what, sizeof what, "Fastest stays close to the CPU: %s", k.name);
            check(fast && db < -50.0, what);
        }
    }
    _putenv("UTAGOE_GPU_MIN_WORK=");
}

// By Waveform の代替モデル。リマスター風の組 (インストに EQ と stereo の混ぜ直しが掛かっている) で、
// v3 の時間領域の減算と各モデルを比べる。位置合わせは v3 の block 解析と GCC の両方。
void testWaveModels() {
    say("By Waveform models\n");

    std::vector<std::pair<double, double>> spans;
    std::string err;
    check(parseTimeSpans("0-10, 2:00-2:08.5; 1:00:00-1:00:05", spans, err) && spans.size() == 3 &&
          spans[1].first == 120.0 && spans[1].second == 128.5 && spans[2].first == 3600.0, "vocal-free spans parse");
    check(parseTimeSpans("", spans, err) && spans.empty(), "empty spans mean the whole song");
    check(!parseTimeSpans("10-5", spans, err) && !parseTimeSpans("abc", spans, err), "bad spans are rejected");

    const int rate = 44100;
    const std::size_t frames = static_cast<std::size_t>(rate) * 12;
    const int delay = 523;
    const std::vector<double> band = makeBacking(frames + 2000, rate, 99, false);
    // インスト: 背景をそのまま。原曲: 背景に短い EQ と左右の混ぜ直しを掛け、delay sample 遅らせ、2 秒後から声を足す。
    AudioBuffer orig, inst;
    orig.sampleRate = inst.sampleRate = rate;
    orig.channels = inst.channels = 2;
    orig.samples.assign(frames * 2, 0.0f);
    inst.samples.assign(frames * 2, 0.0f);
    std::vector<double> voc(frames);
    double y1 = 0, y2 = 0, prevL = 0, prevR = 0;
    for (std::size_t f = 0; f < frames; ++f) {
        const double t = static_cast<double>(f) / rate;
        const double l = band[f], r = band[f + 1000] * 0.8;
        inst.samples[f * 2] = static_cast<float>(l / 32768.0 * 0.5);
        inst.samples[f * 2 + 1] = static_cast<float>(r / 32768.0 * 0.5);
        voc[f] = t < 2.0 ? 0.0 : 0.08 * std::sin(2 * PI * 350 * t) * (0.6 + 0.4 * std::sin(2 * PI * 0.9 * t));
    }
    for (std::size_t f = 0; f < frames; ++f) {
        const long long j = static_cast<long long>(f) - delay;
        const double l = j >= 0 ? inst.samples[static_cast<std::size_t>(j) * 2] : 0.0;
        const double r = j >= 0 ? inst.samples[static_cast<std::size_t>(j) * 2 + 1] : 0.0;
        // 1 次の EQ と stereo の混ぜ直し
        y1 = 0.72 * l + 0.2 * prevL + 0.1 * y1;
        y2 = 0.72 * r + 0.2 * prevR + 0.1 * y2;
        prevL = l;
        prevR = r;
        orig.samples[f * 2] = static_cast<float>(0.97 * y1 + 0.08 * y2 + voc[f]);
        orig.samples[f * 2 + 1] = static_cast<float>(-0.05 * y1 + 1.03 * y2 + voc[f]);
    }

    auto vocalSnr = [&](const ExtractResult& r) {
        // 処理遅延の無い経路なので、そのまま比べる。端は除く。
        double s = 0, e = 0;
        for (std::size_t f = frames / 4; f < frames - frames / 8; ++f)
            for (int c = 0; c < 2; ++c) {
                const double d = r.vocal.samples[f * 2 + static_cast<std::size_t>(c)] - voc[f];
                s += voc[f] * voc[f];
                e += d * d;
            }
        return 10 * std::log10(s / std::max(e, 1e-30));
    };

    Settings base;
    base.mergeMode = MergeMode::ByWaveform;
    const ExtractResult v3r = extract(orig, inst, base, false);
    const double v3snr = v3r ? vocalSnr(v3r) : -999;
    say("    %-12s %-8s vocal SNR %6.1f dB %s\n", "v3", "", v3snr, v3r.alignment.debugString().c_str());

    const WaveModel models[] = {WaveModel::Robust, WaveModel::Kalman, WaveModel::Hammerstein,
                                WaveModel::Nmf, WaveModel::Spatial, WaveModel::Ensemble};
    const char* names[] = {"Robust", "Kalman", "Hammerstein", "NMF", "Spatial", "Ensemble"};
    for (int a = 0; a < 2; ++a) {
        for (int m = 0; m < 6; ++m) {
            Settings s = base;
            s.waveModel = models[m];
            s.waveAlign = a == 0 ? WaveAlign::V3Block : WaveAlign::Gcc;
            const ExtractResult r = extract(orig, inst, s, false);
            bool finite = static_cast<bool>(r) && r.vocal.samples.size() == orig.samples.size();
            if (finite)
                for (float v : r.vocal.samples) finite = finite && std::isfinite(v);
            const double db = finite ? vocalSnr(r) : -999;
            say("    %-12s %-8s vocal SNR %6.1f dB %s%s\n", names[m], a == 0 ? "v3 align" : "GCC", db,
                r.alignment.debugString().c_str(), r ? "" : (" error: " + r.error).c_str());
            char what[96];
            std::snprintf(what, sizeof what, "%s (%s) runs and gives finite full-length output", names[m], a == 0 ? "v3 align" : "GCC");
            check(finite, what);
            // 位相の合う減算系のモデルは、EQ の違う組で v3 の単純な減算より良くなるはず。
            if (m <= 2 || m == 5) {
                std::snprintf(what, sizeof what, "%s (%s) beats v3 on an EQ'd instrumental", names[m], a == 0 ? "v3 align" : "GCC");
                check(db > v3snr + 6.0, what);
            }
        }
    }

    const WaveModel algorithms[] = {WaveModel::Rational, WaveModel::Surface, WaveModel::Trend, WaveModel::Ctf, WaveModel::LowRank};
    const char* algorithmNames[] = {"Rational", "Surface", "Trend", "CTF", "Low-rank"};
    const WaveAlign aligns[] = {WaveAlign::Dense, WaveAlign::V3Block, WaveAlign::Gcc};
    const char* alignNames[] = {"dense", "v3 align", "GCC"};
    for (int a = 0; a < 3; ++a) {
        for (int m = 0; m < 5; ++m) {
            Settings s = base;
            s.waveModel = algorithms[m];
            s.waveAlign = aligns[a];
            const ExtractResult r = extract(orig, inst, s, false);
            bool finite = static_cast<bool>(r) && r.vocal.samples.size() == orig.samples.size();
            if (finite)
                for (float v : r.vocal.samples) finite = finite && std::isfinite(v);
            const double db = finite ? vocalSnr(r) : -999;
            say("    %-12s %-8s vocal SNR %6.1f dB %s%s\n", algorithmNames[m], alignNames[a], db,
                r.alignment.debugString().c_str(), r ? "" : (" error: " + r.error).c_str());
            char what[96];
            std::snprintf(what, sizeof what, "%s (%s) runs and gives finite full-length output", algorithmNames[m], alignNames[a]);
            check(finite, what);
            std::snprintf(what, sizeof what, "%s (%s) beats v3 on an EQ'd instrumental", algorithmNames[m], alignNames[a]);
            check(db > v3snr, what);
        }
    }
    {
        Settings s = base;
        s.waveModel = WaveModel::Robust;
        s.waveAlign = WaveAlign::Dense;
        const ExtractResult r = extract(orig, inst, s, false);
        const double db = r ? vocalSnr(r) : -999;
        say("    %-12s %-8s vocal SNR %6.1f dB %s%s\n", "Robust", "dense", db, r.alignment.debugString().c_str(),
            r ? "" : (" error: " + r.error).c_str());
        check(r && db > v3snr + 6.0, "Robust (dense) beats v3 on an EQ'd instrumental");
        s.outputKind = OutputKind::AlignedPair;
        const ExtractResult p = extract(orig, inst, s, false);
        double e = 0, sig = 0;
        if (p && p.alignedInst.samples.size() == orig.samples.size())
            for (std::size_t f = frames / 4; f < frames - frames / 8; ++f)
                for (int c = 0; c < 2; ++c) {
                    const std::size_t j = f - static_cast<std::size_t>(delay);
                    const double d = p.alignedInst.samples[f * 2 + static_cast<std::size_t>(c)] - inst.samples[j * 2 + static_cast<std::size_t>(c)];
                    sig += static_cast<double>(inst.samples[j * 2 + static_cast<std::size_t>(c)]) * inst.samples[j * 2 + static_cast<std::size_t>(c)];
                    e += d * d;
                }
        const double pairDb = 10 * std::log10(std::max(sig, 1e-30) / std::max(e, 1e-30));
        say("    aligned pair (dense): instrumental match %.1f dB %s\n", pairDb, p.alignment.debugString().c_str());
        check(p && pairDb > 15.0, "aligned pair with the dense time map lines up the instrumental");
        AudioBuffer monoOrig, monoInst;
        monoOrig.sampleRate = monoInst.sampleRate = rate;
        monoOrig.channels = monoInst.channels = 1;
        monoOrig.samples.resize(frames);
        monoInst.samples.resize(frames);
        for (std::size_t f = 0; f < frames; ++f) {
            monoOrig.samples[f] = orig.samples[f * 2];
            monoInst.samples[f] = inst.samples[f * 2];
        }
        Settings ms = base;
        ms.waveModel = WaveModel::Surface;
        const ExtractResult mr = extract(monoOrig, monoInst, ms, false);
        check(!mr && mr.error.find("stereo") != std::string::npos, "the algorithms report that they need stereo input");
    }

    for (WaveModel m : {WaveModel::EnsembleLarge, WaveModel::Auto}) {
        const char* mn = m == WaveModel::Auto ? "Auto" : "Ensemble Large";
        for (int a = 0; a < 3; ++a) {
            Settings s = base;
            s.waveModel = m;
            s.waveAlign = aligns[a];
            const ExtractResult r = extract(orig, inst, s, false);
            bool finite = static_cast<bool>(r) && r.vocal.samples.size() == orig.samples.size();
            if (finite)
                for (float v : r.vocal.samples) finite = finite && std::isfinite(v);
            const double db = finite ? vocalSnr(r) : -999;
            say("    %-14s %-8s vocal SNR %6.1f dB %s%s\n", mn, alignNames[a], db, r.alignment.debugString().c_str(),
                r ? "" : (" error: " + r.error).c_str());
            char what[96];
            std::snprintf(what, sizeof what, "%s (%s) runs and beats v3 by 6 dB on an EQ'd instrumental", mn, alignNames[a]);
            check(finite && db > v3snr + 6.0, what);
            if (m == WaveModel::Auto) break;
        }
    }
    {
        AudioBuffer mo, mi;
        mo.sampleRate = mi.sampleRate = rate;
        mo.channels = mi.channels = 1;
        mo.samples.resize(frames);
        mi.samples.resize(frames);
        for (std::size_t f = 0; f < frames; ++f) {
            mo.samples[f] = orig.samples[f * 2];
            mi.samples[f] = inst.samples[f * 2];
        }
        Settings s = base;
        s.waveModel = WaveModel::Auto;
        const ExtractResult r = extract(mo, mi, s, false);
        say("    Auto on mono: %s%s\n", r.alignment.debugString().c_str(), r ? "" : (" error: " + r.error).c_str());
        check(r && r.alignment.model.find("->Ensemble Small") != std::string::npos, "Auto on mono uses Ensemble Small");
    }

    // 声の無い区間を指定すると、その区間だけで較正する。
    Settings s = base;
    s.waveModel = WaveModel::Robust;
    s.waveAlign = WaveAlign::Gcc;
    s.fitSpans = "0-1.9";
    const ExtractResult sp = extract(orig, inst, s, false);
    say("    Robust with span 0-1.9: vocal SNR %.1f dB %s\n", sp ? vocalSnr(sp) : -999.0, sp.alignment.debugString().c_str());
    check(sp && sp.alignment.model.find("spans:yes") != std::string::npos, "vocal-free span is used for calibration");
    s.fitSpans = "oops";
    check(!extract(orig, inst, s, false) && !extract(orig, inst, s, false).error.empty(), "a bad span is reported, not ignored");
}

// stack trace は関数名まで出る。例外は投げた位置の stack が残る。
__attribute__((noinline)) std::string stackProbeInner() { return utagoe::log::stackTrace(); }
__attribute__((noinline)) std::string stackProbeOuter() { return stackProbeInner() + ""; }
__attribute__((noinline)) void throwProbe(int depth) {
    if (depth == 0) throw std::runtime_error("probe");
    throwProbe(depth - 1);
    std::printf("%s", "");
}

void testStackTraces() {
    say("Stack traces\n");
    const std::string here = stackProbeOuter();
    say("%s", here.substr(0, here.find('\n', here.find('\n') + 1) + 1).c_str());
    check(here.find("stackProbeInner") != std::string::npos && here.find("stackProbeOuter") != std::string::npos,
          "names the functions on the stack");
    std::string thrown;
    try {
        throwProbe(3);
    } catch (const std::exception&) {
        thrown = utagoe::log::lastThrowTrace();
    }
    say("%s", thrown.substr(0, thrown.find('\n') + 1).c_str());
    check(thrown.find("throwProbe") != std::string::npos, "an exception keeps the stack where it was thrown");
}

void testExtract() {
    say("End-to-end extraction\n");

    const int rate = 44100;
    const std::size_t frames = static_cast<std::size_t>(rate) * 8;

    AudioBuffer orig, inst;
    orig.sampleRate = inst.sampleRate = rate;
    orig.channels = inst.channels = 2;
    orig.samples.assign(frames * 2, 0.0f);
    inst.samples.assign(frames * 2, 0.0f);

    // pipeline test では broadband backing と別帯域の vocal を使う。
    // backing は非周期で、両入力の backing 成分は完全一致させる。これはアルゴリズムの前提。
    uint32_t rng = 999;
    auto noise = [&]() {
        rng = rng * 1664525u + 1013904223u;
        return (static_cast<double>(rng >> 8) / 8388608.0) - 1.0;
    };

    double lp = 0.0;
    for (std::size_t f = 0; f < frames; ++f) {
        lp = 0.97 * lp + 0.03 * noise();
        const double t = static_cast<double>(f) / rate;
        const double band = 6000.0 * lp;
        const double voc  = 4000.0 * std::sin(2 * PI * 1200.0 * t);
        for (int c = 0; c < 2; ++c) {
            inst.samples[f * 2 + c] = s16(band);
            orig.samples[f * 2 + c] = s16(band + voc);
        }
    }

    Settings s;
    s.introMode = IntroMode::None;   // ここでは既に aligned なので探索は省く。
    ExtractResult r = extract(orig, inst, s, true);

    check(static_cast<bool>(r), "pipeline completed");
    if (!r) { say("    error: %s\n", r.error.c_str()); return; }
    check(r.vocal.samples.size() == orig.samples.size(), "output length matches input");

    // 150 Hz の instrumental 残留量と 1200 Hz の vocal 残留量を比べる。
    auto bandEnergy = [&](double hz) {
        double re = 0.0, im = 0.0;
        const std::size_t start = frames / 4, count = frames / 2;
        for (std::size_t f = start; f < start + count; ++f) {
            const double t = static_cast<double>(f) / rate;
            const double v = r.vocal.samples[f * 2] * 32768.0;
            re += v * std::cos(2 * PI * hz * t);
            im += v * std::sin(2 * PI * hz * t);
        }
        return std::sqrt(re * re + im * im) / count;
    };

    const double inst150  = bandEnergy(150.0);
    const double voc1200  = bandEnergy(1200.0);
    say("    residual 150Hz (instrumental) %.2f, 1200Hz (vocal) %.2f\n",
                inst150, voc1200);
    check(voc1200 > inst150, "vocal survives more strongly than the instrumental");
}

// 揃えた組の出力。原曲 = (遅らせて極性を反転した) インスト + 声。揃えたインストは原曲の中のインストと一致し、
// 原曲はそのまま返る。v3 の block 解析と GCC の両方で確かめる。
void testAlignedPair() {
    say("Aligned main + instrumental pair\n");

    const int rate = 44100;
    const std::size_t frames = static_cast<std::size_t>(rate) * 12;
    const std::size_t shift = 1234;
    uint32_t rng = 777;
    auto noise = [&]() { rng = rng * 1664525u + 1013904223u; return static_cast<double>(rng >> 8) / 8388608.0 - 1.0; };

    AudioBuffer orig, inst;
    orig.sampleRate = inst.sampleRate = rate;
    orig.channels = inst.channels = 2;
    orig.samples.assign(frames * 2, 0.0f);
    inst.samples.assign(frames * 2, 0.0f);
    std::vector<float> placed(frames * 2, 0.0f);
    double lp[2] = {0.0, 0.0};
    for (std::size_t f = 0; f < frames; ++f)
        for (int c = 0; c < 2; ++c) {
            lp[c] = 0.9 * lp[c] + 0.1 * noise();
            inst.samples[f * 2 + c] = s16(6000.0 * lp[c]);
        }
    for (std::size_t f = shift; f < frames; ++f)
        for (int c = 0; c < 2; ++c) placed[f * 2 + c] = -inst.samples[(f - shift) * 2 + c];
    for (std::size_t f = 0; f < frames; ++f) {
        const double voc = 3000.0 * std::sin(2 * PI * 700.0 * static_cast<double>(f) / rate);
        for (int c = 0; c < 2; ++c) orig.samples[f * 2 + c] = placed[f * 2 + c] + s16(voc);
    }

    for (const WaveAlign align : {WaveAlign::V3Block, WaveAlign::Gcc}) {
        const char* name = align == WaveAlign::Gcc ? "GCC" : "v3 block";
        Settings s;
        s.mergeMode = MergeMode::ByWaveform;
        s.outputKind = OutputKind::AlignedPair;
        s.waveAlign = align;
        ExtractResult r = extract(orig, inst, s, true);
        check(static_cast<bool>(r), (std::string(name) + ": pair completed").c_str());
        if (!r) { say("    error: %s\n", r.error.c_str()); continue; }
        check(r.vocal.samples == orig.samples, (std::string(name) + ": the original is returned unchanged").c_str());
        check(r.alignedInst.samples.size() == orig.samples.size(), (std::string(name) + ": aligned instrumental has the original's length").c_str());
        // 冒頭 (v3 は探索幅をインストなしで通す) を除き、原曲の中のインストとの差を測る。
        double sig = 0.0, err = 0.0;
        for (std::size_t i = static_cast<std::size_t>(rate) * 2 * 2; i < placed.size(); ++i) {
            const double d = static_cast<double>(r.alignedInst.samples[i]) - placed[i];
            sig += static_cast<double>(placed[i]) * placed[i];
            err += d * d;
        }
        const double snr = err > 0 ? 10.0 * std::log10(sig / err) : 999.0;
        say("    %s: offset %d, phase %s, match %.1f dB\n", name, r.alignment.offset, r.alignment.inverted ? "inverted" : "normal", snr);
        check(r.alignment.inverted, (std::string(name) + ": inverted polarity detected").c_str());
        check(snr > (align == WaveAlign::Gcc ? 50.0 : 200.0), (std::string(name) + ": aligned instrumental matches the one inside the original").c_str());
    }

    std::string m, i;
    alignedPairPaths("C:/a.b/song.flac", m, i);
    check(m == "C:/a.b/song_main.flac" && i == "C:/a.b/song_inst.flac", "pair file names");
    alignedPairPaths("C:/a.b/song", m, i);
    check(m == "C:/a.b/song_main" && i == "C:/a.b/song_inst", "pair file names without an extension");
}

// 16-bit 経路の回帰確認用 hash。v3 の出力ではなく、この移植の出力で取った値。処理を意図して変えたら取り直す。
void testLegacyBitExact() {
    say("16-bit path regression hashes\n");

    const int rate = 44100;
    const std::size_t frames = static_cast<std::size_t>(rate) * 4;
    const int off = 211;
    uint32_t rng = 4242;
    auto noise = [&]() { rng = rng * 1664525u + 1013904223u; return static_cast<double>(rng >> 8) / 8388608.0 - 1.0; };
    std::vector<double> band(frames);
    double lp = 0.0;
    for (std::size_t f = 0; f < frames; ++f) { lp = 0.95 * lp + 0.05 * noise(); band[f] = 6000 * lp; }

    AudioBuffer o, i;
    o.sampleRate = i.sampleRate = rate;
    o.channels = i.channels = 2;
    o.samples.resize(frames * 2);
    i.samples.resize(frames * 2);
    for (std::size_t f = 0; f < frames; ++f) {
        const double t = static_cast<double>(f) / rate;
        const double voc = 3000 * std::sin(2 * PI * 660 * t);
        const long long j = static_cast<long long>(f) - off;
        const double d = j >= 0 ? band[static_cast<std::size_t>(j)] : 0.0;
        for (int c = 0; c < 2; ++c) {
            i.samples[f * 2 + c] = s16(band[f] * (c ? 0.9 : 1.0));
            o.samples[f * 2 + c] = s16(d * (c ? 0.9 : 1.0) + voc);
        }
    }

    const unsigned long long golden[] = {0x8ACEB317E56EAF6DULL, 0x2DE7887EA8426394ULL, 0x29D7A02B0BCABE8EULL};
    const char* names[] = {"By Frequency", "By Frequency + LPF/HPF", "By Waveform"};
    for (int mode = 0; mode < 3; ++mode) {
        Settings s;
        if (mode == 1) { s.lowPass = true; s.highPass = true; }
        if (mode == 2) s.mergeMode = MergeMode::ByWaveform;
        ExtractResult r = extract(o, i, s, true);
        unsigned long long h = 1469598103934665603ULL;
        for (float v : r.vocal.samples) {
            h ^= static_cast<uint16_t>(static_cast<int16_t>(std::lround(v * 32768.0f)));
            h *= 1099511628211ULL;
        }
        h ^= static_cast<unsigned long long>(static_cast<unsigned>(r.alignment.offset));
        h *= 1099511628211ULL;
        say("    %-24s 0x%016llX\n", names[mode], h);
        check(h == golden[mode], names[mode]);
    }
}


// 3 秒の tone + noise。値を bits bit の格子に乗せると、lossless 形式で完全一致を確認できる。
AudioBuffer testSignal(int rate, int channels, int gridBits) {
    AudioBuffer a;
    a.sampleRate = rate;
    a.channels = channels;
    const std::size_t frames = static_cast<std::size_t>(rate) * 3;
    a.samples.resize(frames * channels);
    uint32_t rng = 77;
    const double grid = gridBits > 0 ? std::ldexp(1.0, gridBits - 1) : 0.0;
    for (std::size_t f = 0; f < frames; ++f) {
        const double t = static_cast<double>(f) / rate;
        for (int c = 0; c < channels; ++c) {
            rng = rng * 1664525u + 1013904223u;
            double v = 0.3 * std::sin(2 * PI * (220.0 + 110.0 * c) * t) + 0.2 * std::sin(2 * PI * 3150.0 * t)
                     + 0.05 * (static_cast<double>(rng >> 8) / 8388608.0 - 1.0);
            if (grid > 0) v = std::nearbyint(v * grid) / grid;
            a.samples[f * channels + c] = static_cast<float>(v);
        }
    }
    return a;
}

// encoder delay を考慮して、decode 結果と元信号の相関が最大になる lag を探す。
double bestCorrelation(const AudioBuffer& ref, const AudioBuffer& got) {
    const AudioBuffer g = conformAudio(got, ref.sampleRate, ref.channels);
    const std::size_t start = static_cast<std::size_t>(ref.sampleRate) / 2;
    const std::size_t len = static_cast<std::size_t>(ref.sampleRate);
    double best = 0.0;
    for (int lag = 0; lag < 5000; ++lag) {
        double xy = 0, xx = 0, yy = 0;
        for (std::size_t k = start; k < start + len; ++k) {
            if (k + lag >= g.frames()) break;
            const double x = ref.samples[k * ref.channels];
            const double y = g.samples[(k + lag) * g.channels];
            xy += x * y; xx += x * x; yy += y * y;
        }
        if (xx > 0 && yy > 0) best = std::max(best, xy / std::sqrt(xx * yy));
    }
    return best;
}

bool mfMissing(const std::string& err) {
    return err.find("Media Foundation is not available") != std::string::npos ||
           err.find("no encoder") != std::string::npos;
}

void testLosslessRoundTrips() {
    say("Lossless formats round-trip exactly\n");
    struct Case { const char* name; const char* file; OutputFormat f; OutputDepth d; int grid; };
    const Case cases[] = {
        {"WAV 16-bit", "rt16.wav", OutputFormat::Wav, OutputDepth::Int16, 16},
        {"WAV 24-bit", "rt24.wav", OutputFormat::Wav, OutputDepth::Int24, 24},
        {"WAV 32-bit float", "rt32f.wav", OutputFormat::Wav, OutputDepth::Float32, 0},
        {"AIFF 16-bit", "rt16.aiff", OutputFormat::Aiff, OutputDepth::Int16, 16},
        {"AIFF 24-bit", "rt24.aiff", OutputFormat::Aiff, OutputDepth::Int24, 24},
        {"FLAC 16-bit", "rt16.flac", OutputFormat::Flac, OutputDepth::Int16, 16},
        {"FLAC 24-bit", "rt24.flac", OutputFormat::Flac, OutputDepth::Int24, 24},
        {"ALAC 16-bit", "rt16.m4a", OutputFormat::Alac, OutputDepth::Int16, 16},
        {"ALAC 24-bit", "rt24.m4a", OutputFormat::Alac, OutputDepth::Int24, 24},
    };
    for (const auto& c : cases) {
        const AudioBuffer src = testSignal(48000, 2, c.grid);
        const std::string path = tempPath(c.file);
        std::string err;
        EncodeOptions opt;
        opt.format = c.f;
        opt.depth = c.d;
        if (!encodeAudio(path, src, opt, err)) {
            if (mfMissing(err)) { skip(c.name, err); continue; }
            say("    encode error: %s\n", err.c_str());
            check(false, c.name);
            continue;
        }
        AudioBuffer back;
        AudioInfo info;
        const bool ok = decodeAudio(path, back, &info, err);
        // Windows の ALAC muxer は長さを 4096 frame 単位へ切り上げ、末尾に無音を足す (container の長さも切り上がる)。
        // 音声自体は bit 単位で一致し、足されるのは 1 packet 未満の無音だけなので、その範囲を許容する。
        if (ok && c.f == OutputFormat::Alac && back.samples.size() > src.samples.size() &&
            back.frames() - src.frames() < 4096 &&
            std::all_of(back.samples.begin() + static_cast<std::ptrdiff_t>(src.samples.size()),
                        back.samples.end(), [](float v) { return v == 0.0f; }))
            back.samples.resize(src.samples.size());
        const bool same = ok && back.sampleRate == src.sampleRate && back.channels == src.channels &&
                          back.samples == src.samples;
        if (!same) {
            say("    %s: %s (%zu vs %zu frames)\n", c.name, ok ? "mismatch" : err.c_str(),
                        back.frames(), src.frames());
            if (ok && back.samples.size() >= src.samples.size()) {
                const bool prefix = std::equal(src.samples.begin(), src.samples.end(), back.samples.begin());
                float tail = 0.0f;
                for (std::size_t k = src.samples.size(); k < back.samples.size(); ++k)
                    tail = std::max(tail, std::fabs(back.samples[k]));
                AudioInfo probed;
                probeAudio(path, probed, err);
                say("      prefix exact %d, tail max %g, container duration %lld frames\n",
                            prefix ? 1 : 0, tail, static_cast<long long>(probed.frames));
            }
        }
        check(same, c.name);
        fs::remove(fs::u8path(path));
    }
}

void testLossyRoundTrips() {
    say("Lossy formats decode back close to the source\n");
    struct Case { const char* name; const char* file; OutputFormat f; int kbps; };
    const Case cases[] = {
        {"MP3 320 kbps", "lossy.mp3", OutputFormat::Mp3, 320},
        {"AAC 192 kbps", "lossy.m4a", OutputFormat::Aac, 192},
        {"WMA 192 kbps", "lossy.wma", OutputFormat::Wma, 192},
        {"Ogg Vorbis 320 kbps", "lossy.ogg", OutputFormat::Vorbis, 320},
        {"Opus 256 kbps", "lossy.opus", OutputFormat::Opus, 256},
    };
    const AudioBuffer src = testSignal(44100, 2, 0);
    for (const auto& c : cases) {
        const std::string path = tempPath(c.file);
        std::string err;
        EncodeOptions opt;
        opt.format = c.f;
        opt.bitrate = c.kbps;
        if (!encodeAudio(path, src, opt, err)) {
            if (mfMissing(err)) { skip(c.name, err); continue; }
            say("    encode error: %s\n", err.c_str());
            check(false, c.name);
            continue;
        }
        AudioBuffer back;
        AudioInfo info;
        if (!decodeAudio(path, back, &info, err)) {
            say("    decode error: %s\n", err.c_str());
            check(false, c.name);
            continue;
        }
        const double corr = bestCorrelation(src, back);
        say("    %-22s decoded as %-10s %6d Hz  corr %.4f\n", c.name, info.codec.c_str(),
                    info.sampleRate, corr);
        check(corr > 0.95, c.name);
        fs::remove(fs::u8path(path));
    }
}

void testResampler() {
    say("Resampler\n");

    // 解析的な sine を 48 kHz で作り、44.1 kHz へ変換した結果を 44.1 kHz で直接作った sine と比べる。
    auto sine = [](int rate, double hz, std::size_t frames) {
        AudioBuffer a;
        a.sampleRate = rate;
        a.channels = 1;
        a.samples.resize(frames);
        for (std::size_t i = 0; i < frames; ++i)
            a.samples[i] = static_cast<float>(0.5 * std::sin(2 * PI * hz * i / rate));
        return a;
    };
    auto errorDb = [](const AudioBuffer& a, const AudioBuffer& b) {
        const std::size_t n = std::min(a.samples.size(), b.samples.size());
        double e = 0, s = 0;
        for (std::size_t i = n / 4; i < 3 * n / 4; ++i) {   // 端の過渡部は評価から外す。
            e += (a.samples[i] - b.samples[i]) * (a.samples[i] - b.samples[i]);
            s += b.samples[i] * b.samples[i];
        }
        return 10.0 * std::log10(e / s);
    };

    for (double hz : {1000.0, 15000.0}) {
        const AudioBuffer out = conformAudio(sine(48000, hz, 48000), 44100, 1);
        const double db = errorDb(out, sine(44100, hz, 44100));
        say("    48k -> 44.1k  %5.0f Hz sine: error %.1f dB\n", hz, db);
        check(db < -90.0, "48 kHz to 44.1 kHz is accurate");
    }
    {
        const AudioBuffer src = sine(44100, 5000.0, 44100);
        const AudioBuffer back = conformAudio(conformAudio(src, 96000, 1), 44100, 1);
        const double db = errorDb(back, src);
        say("    44.1k -> 96k -> 44.1k round trip: error %.1f dB\n", db);
        check(db < -90.0, "up/down round trip is accurate");
    }
    {
        AudioBuffer st;
        st.sampleRate = 44100;
        st.channels = 2;
        st.samples = {0.5f, 0.1f, -0.2f, 0.4f};
        const AudioBuffer mono = conformAudio(st, 44100, 1);
        check(mono.samples.size() == 2 && std::fabs(mono.samples[0] - 0.3f) < 1e-6f &&
              std::fabs(mono.samples[1] - 0.1f) < 1e-6f, "stereo to mono averages the channels");
        const AudioBuffer again = conformAudio(mono, 44100, 2);
        check(again.samples.size() == 4 && again.samples[0] == again.samples[1], "mono to stereo duplicates");
    }
}

// 同じ曲の別形式 (48 kHz mono) をインストに与えても、同じ形式を与えたときとほぼ同じ結果になるか確認する。
void testMismatchedInputs() {
    say("Mismatched inputs (instrumental at 48 kHz mono, original at 44.1 kHz stereo)\n");

    // backing は解析的に作り、どの sample rate でも同じ波形を正確に生成できるようにする。
    struct Partial { double hz, amp, phase; };
    std::vector<Partial> partials;
    uint32_t rng = 31337;
    auto rnd = [&]() { rng = rng * 1664525u + 1013904223u; return static_cast<double>(rng >> 8) / 16777216.0; };
    for (int k = 0; k < 60; ++k) partials.push_back({60.0 * std::pow(180.0, rnd()), 0.02 + 0.03 * rnd(), 2 * PI * rnd()});
    auto backing = [&](double t) {
        double v = 0;
        for (const auto& p : partials) v += p.amp * std::sin(2 * PI * p.hz * t + p.phase);
        return v * (0.7 + 0.3 * std::sin(2 * PI * 0.37 * t));
    };

    const int rateO = 44100, rateI = 48000;
    const double seconds = 12.0;
    const int delay = 300;   // 原曲側の backing の遅れ (44.1 kHz の sample 数)
    const std::size_t fo = static_cast<std::size_t>(rateO * seconds), fi = static_cast<std::size_t>(rateI * seconds);

    AudioBuffer orig, instSame, instOther;
    orig.sampleRate = instSame.sampleRate = rateO;
    orig.channels = instSame.channels = 2;
    instOther.sampleRate = rateI;
    instOther.channels = 1;
    orig.samples.resize(fo * 2);
    instSame.samples.resize(fo * 2);
    instOther.samples.resize(fi);
    std::vector<float> vocal(fo);
    for (std::size_t f = 0; f < fo; ++f) {
        const double t = static_cast<double>(f) / rateO;
        vocal[f] = static_cast<float>(0.25 * std::sin(2 * PI * 880 * t) * (0.5 + 0.5 * std::sin(2 * PI * 1.1 * t)));
        const double b = backing(t - static_cast<double>(delay) / rateO);
        const double bi = backing(t);
        for (int c = 0; c < 2; ++c) {
            orig.samples[f * 2 + c] = static_cast<float>(b) + vocal[f];
            instSame.samples[f * 2 + c] = static_cast<float>(bi);
        }
    }
    for (std::size_t f = 0; f < fi; ++f) instOther.samples[f] = static_cast<float>(backing(static_cast<double>(f) / rateI));

    for (MergeMode mode : {MergeMode::ByWaveform, MergeMode::ByFrequency}) {
        Settings s;
        s.mergeMode = mode;
        const ExtractResult a = extract(orig, instSame, s, false);
        const ExtractResult b = extract(orig, instOther, s, false);
        if (!a || !b) { check(false, "extraction ran"); continue; }

        std::size_t bad = 0, firstBad = 0;
        for (std::size_t k = 0; k < a.vocal.samples.size(); ++k)
            if (!std::isfinite(a.vocal.samples[k]) || !std::isfinite(b.vocal.samples[k])) {
                if (!bad) firstBad = k / 2;
                ++bad;
            }
        if (bad) say("    %zu non-finite samples, first at frame %zu\n", bad, firstBad);
        check(bad == 0, "output has no NaN / Inf");

        double diff = 0, ref = 0, res = 0;
        for (std::size_t f = fo / 4; f < 3 * fo / 4; ++f) {
            const double x = a.vocal.samples[f * 2], y = b.vocal.samples[f * 2];
            diff += (x - y) * (x - y);
            ref += x * x;
            res += (y - vocal[f]) * (y - vocal[f]);
        }
        const double vref = [&] { double v = 0; for (std::size_t f = fo / 4; f < 3 * fo / 4; ++f) v += vocal[f] * vocal[f]; return v; }();
        const double diffDb = 10 * std::log10(diff / ref);
        const char* name = mode == MergeMode::ByWaveform ? "By Waveform" : "By Frequency";
        say("    %-12s offset %d / %d, result differs from matched-input result by %.1f dB",
                    name, a.alignment.offset, b.alignment.offset, diffDb);
        if (mode == MergeMode::ByWaveform) say(", vocal error %.1f dB", 10 * std::log10(res / vref));
        say("\n");
        check(b.alignment.offset == a.alignment.offset, "same alignment as with matched input");
        check(diffDb < -40.0, "same result as with matched input");
        // level は元実装どおり 0.002 刻みの推定なので、完全一致の減算ほどは消えない。
        if (mode == MergeMode::ByWaveform) check(10 * std::log10(res / vref) < -30.0, "backing cancels, vocal recovered");
    }
}

// 実ファイル経由: 24-bit FLAC 48 kHz の原曲と MP3 44.1 kHz mono のインスト。日本語ファイル名。
void testMixedCodecFiles() {
    say("Mixed codecs through files (24-bit FLAC original, MP3 instrumental)\n");

    const int rate = 48000;
    const std::size_t frames = static_cast<std::size_t>(rate) * 12;
    uint32_t rng = 555;
    auto noise = [&]() { rng = rng * 1664525u + 1013904223u; return static_cast<double>(rng >> 8) / 8388608.0 - 1.0; };
    AudioBuffer orig, inst;
    orig.sampleRate = inst.sampleRate = rate;
    orig.channels = inst.channels = 2;
    orig.samples.resize(frames * 2);
    inst.samples.resize(frames * 2);
    std::vector<float> vocal(frames);
    double lp = 0;
    for (std::size_t f = 0; f < frames; ++f) {
        lp = 0.96 * lp + 0.04 * noise();
        const double t = static_cast<double>(f) / rate;
        vocal[f] = static_cast<float>(0.2 * std::sin(2 * PI * 700 * t));
        for (int c = 0; c < 2; ++c) {
            inst.samples[f * 2 + c] = static_cast<float>(0.4 * lp);
            orig.samples[f * 2 + c] = static_cast<float>(0.4 * lp) + vocal[f];
        }
    }

    const std::string origPath = tempPath("原曲.flac");
    const std::string instPath = tempPath("原曲 (off vocal).mp3");
    std::string err;
    EncodeOptions eo;
    eo.format = OutputFormat::Flac;
    eo.depth = OutputDepth::Int24;
    check(encodeAudio(origPath, orig, eo, err), "wrote 24-bit FLAC with a Japanese name");
    AudioBuffer instMono = conformAudio(inst, 44100, 1);
    EncodeOptions ei;
    ei.format = OutputFormat::Mp3;
    ei.bitrate = 320;
    if (!encodeAudio(instPath, instMono, ei, err)) {
        if (mfMissing(err)) { skip("MP3 instrumental", err); return; }
        check(false, "wrote MP3 instrumental");
        return;
    }

    AudioBuffer o, i;
    AudioInfo oi, ii;
    check(decodeAudio(origPath, o, &oi, err) && oi.codec == "FLAC" && oi.bits == 24, "decoded FLAC as 24-bit");
    check(decodeAudio(instPath, i, &ii, err) && ii.codec == "MP3", "decoded MP3");
    say("    original %s %d Hz %d-bit %d ch, instrumental %s %d Hz %d ch\n", oi.codec.c_str(),
                oi.sampleRate, oi.bits, oi.channels, ii.codec.c_str(), ii.sampleRate, ii.channels);

    Settings s;
    s.mergeMode = MergeMode::ByWaveform;
    const ExtractResult r = extract(o, i, s, false);
    check(static_cast<bool>(r), "extraction ran");
    if (!r) return;

    double xy = 0, xx = 0, yy = 0;
    for (std::size_t f = frames / 4; f < 3 * frames / 4; ++f) {
        const double x = vocal[f], y = r.vocal.samples[f * 2];
        xy += x * y; xx += x * x; yy += y * y;
    }
    const double corr = xy / std::sqrt(xx * yy);
    say("    alignment offset %d, output/vocal correlation %.4f\n", r.alignment.offset, corr);
    check(corr > 0.9, "vocal recovered despite lossy, resampled, mono instrumental");
    fs::remove(fs::u8path(origPath));
    fs::remove(fs::u8path(instPath));
}

void testSettingsRoundTrip() {
    say("Settings INI\n");

    Settings a;
    a.mergeMode    = MergeMode::ByWaveform;
    a.extractLevel = 17;
    a.blockSizeMs  = 400;
    a.krkPhase     = KrkPhase::Inverted;
    a.lowPass      = true;
    a.lowPassPos   = 13;
    a.outputSuffix = "_vocal";
    a.outputFormat = OutputFormat::Opus;
    a.outputDepth  = OutputDepth::Float32;
    a.outputBitrate = 192;
    a.outputKind   = OutputKind::AlignedPair;
    a.outputFolder = "C:/Users/someone/Music/歌声 Utagoe";
    a.appIcon      = "rainbow";
    a.waveModel    = WaveModel::Hammerstein;
    a.waveAlign    = WaveAlign::Gcc;
    a.fitSpans     = "0-10, 2:00-2:08";
    a.kickDuck     = true;

    const std::string path = tempPath("settings.ini");
    check(a.save(path), "save");

    Settings b;
    check(b.load(path), "load");
    check(b.mergeMode == a.mergeMode && b.extractLevel == a.extractLevel &&
          b.blockSizeMs == a.blockSizeMs && b.krkPhase == a.krkPhase &&
          b.lowPass == a.lowPass && b.lowPassPos == a.lowPassPos &&
          b.outputSuffix == a.outputSuffix,
          "round-trips exactly");
    check(b.outputFormat == a.outputFormat && b.outputDepth == a.outputDepth &&
          b.outputBitrate == a.outputBitrate && b.outputKind == a.outputKind && b.outputFolder == a.outputFolder && b.appIcon == a.appIcon, "output settings round-trip");
    check(b.waveModel == a.waveModel && b.waveAlign == a.waveAlign && b.fitSpans == a.fitSpans && b.kickDuck == a.kickDuck,
          "waveform model settings round-trip");
    fs::remove(fs::u8path(path));
}

void testKickDuck() {
    say("Kick duck matching\n");
    const int rate = 44100;
    const std::size_t frames = static_cast<std::size_t>(rate) * 30;
    std::vector<double> kick(frames, 0.0), duck(frames, 0.0);
    for (double k0 = 0.25; k0 < 30.0 - 0.3; k0 += 0.5) {
        const std::size_t i0 = static_cast<std::size_t>(k0 * rate);
        for (std::size_t i = i0; i < frames; ++i) {
            const double u = static_cast<double>(i) / rate - k0;
            if (u > 1.5) break;
            kick[i] += 0.6 * std::exp(-u / 0.12) * std::sin(2 * PI * (55 + 60 * std::exp(-u / 0.02)) * u);
            duck[i] += (1 - std::exp(-u / 0.0055)) * std::exp(-u / 0.09);
        }
    }
    double dmax = 0;
    for (double d : duck) dmax = std::max(dmax, d);
    const std::vector<double> padL = makeBacking(frames, rate, 5, false), padR = makeBacking(frames, rate, 6, false);
    AudioBuffer orig, inst, flat;
    orig.sampleRate = inst.sampleRate = flat.sampleRate = rate;
    orig.channels = inst.channels = flat.channels = 2;
    orig.samples.resize(frames * 2);
    inst.samples.resize(frames * 2);
    flat.samples.resize(frames * 2);
    std::vector<double> voc(frames);
    for (std::size_t f = 0; f < frames; ++f) {
        const double t = static_cast<double>(f) / rate;
        const double l = padL[f] / 32768.0 * 0.25 + kick[f], r = (0.5 * padL[f] + 0.85 * padR[f]) / 32768.0 * 0.25 + 0.9 * kick[f];
        inst.samples[f * 2] = static_cast<float>(l);
        inst.samples[f * 2 + 1] = static_cast<float>(r);
        voc[f] = 0.07 * std::sin(2 * PI * 300 * t * (1 + 0.05 * std::sin(2 * PI * 0.13 * t))) * (std::sin(2 * PI * 0.17 * t) > -0.2 ? 1.0 : 0.0);
        const double g = std::pow(10.0, -1.0 * std::clamp(duck[f] / dmax, 0.0, 1.0) / 20.0);
        orig.samples[f * 2] = static_cast<float>(l * g + voc[f]);
        orig.samples[f * 2 + 1] = static_cast<float>(r * g + voc[f]);
        flat.samples[f * 2] = static_cast<float>(l + voc[f]);
        flat.samples[f * 2 + 1] = static_cast<float>(r + voc[f]);
    }

    hp::Audio mix, ref;
    mix.channels = ref.channels = 2;
    mix.v = orig.samples;
    ref.v = inst.samples;
    const hp::KickDuck k = hp::kickDuck(mix, ref, rate);
    say("    %d kicks, dip %.3f dB, attack %.2f ms, release %.2f ms, strength %.2f\n", k.kicks, k.depthDb, k.attackMs, k.releaseMs, k.strength);
    check(k.strength > 0.5 && std::abs(k.depthDb - 1.0) < 0.25 && k.releaseMs > 60 && k.releaseMs < 130,
          "recovers an injected 1 dB kick-driven duck");

    auto vocalSnr = [&](const ExtractResult& r) {
        double sg = 0, e = 0;
        for (std::size_t f = frames / 8; f < frames - frames / 8; ++f)
            for (int c = 0; c < 2; ++c) {
                const double d = r.vocal.samples[f * 2 + static_cast<std::size_t>(c)] - voc[f];
                sg += voc[f] * voc[f];
                e += d * d;
            }
        return 10 * std::log10(sg / std::max(e, 1e-30));
    };
    Settings s;
    s.mergeMode = MergeMode::ByWaveform;
    s.waveAlign = WaveAlign::Dense;
    for (WaveModel m : {WaveModel::Robust, WaveModel::Surface}) {
        s.waveModel = m;
        s.kickDuck = false;
        const ExtractResult off = extract(orig, inst, s, false);
        s.kickDuck = true;
        const ExtractResult on = extract(orig, inst, s, false);
        const double a = off ? vocalSnr(off) : -999, b = on ? vocalSnr(on) : -999;
        say("    %-8s vocal SNR %.1f dB without, %.1f dB with kick duck matching %s\n", m == WaveModel::Robust ? "Robust" : "Surface", a, b,
            on.alignment.debugString().c_str());
        check(on && b > a + 3.0, m == WaveModel::Robust ? "kick duck matching improves Robust on a ducked mix" : "kick duck matching improves Surface on a ducked mix");
    }
    s.waveModel = WaveModel::Surface;
    s.kickDuck = true;
    const ExtractResult same = extract(flat, inst, s, false);
    s.kickDuck = false;
    const ExtractResult base = extract(flat, inst, s, false);
    double diff = 0;
    if (same && base)
        for (std::size_t i = 0; i < same.vocal.samples.size(); ++i) diff = std::max(diff, static_cast<double>(std::abs(same.vocal.samples[i] - base.vocal.samples[i])));
    say("    undistorted mix: max difference %.2e\n", diff);
    check(same && base && diff < 1e-4, "kick duck matching leaves a mix without ducking alone");
}

void testCenterSides() {
    say("Centre + sides\n");
    const int rate = 44100;
    const std::size_t frames = static_cast<std::size_t>(rate) * 4;
    AudioBuffer in;
    in.sampleRate = rate;
    in.channels = 2;
    in.samples.resize(frames * 2);
    std::vector<double> centre(frames), left(frames);
    for (std::size_t f = 0; f < frames; ++f) {
        const double t = static_cast<double>(f) / rate;
        centre[f] = 0.2 * std::sin(2 * PI * 440 * t);
        left[f] = 0.2 * std::sin(2 * PI * 1250 * t);
        in.samples[f * 2] = static_cast<float>(centre[f] + left[f]);
        in.samples[f * 2 + 1] = static_cast<float>(centre[f]);
    }
    const char* names[] = {"True M/S", "Phantom", "Coherence", "ADRess", "DUET", "PCA", "Pretty"};
    for (int m = 0; m < 7; ++m) {
        Settings st;
        st.outputKind = OutputKind::CenterSides;
        st.centerMethod = m;
        const ExtractResult r = extractCenterSides(in, st);
        double recon = 0, inC = 0, inL = 0, cE = 0;
        bool ok = r && r.vocal.samples.size() == in.samples.size() && r.alignedInst.samples.size() == in.samples.size();
        if (ok)
            for (std::size_t f = frames / 8; f < frames - frames / 8; ++f) {
                for (int c = 0; c < 2; ++c) {
                    const std::size_t i = f * 2 + static_cast<std::size_t>(c);
                    recon = std::max(recon, static_cast<double>(std::abs(r.vocal.samples[i] + r.alignedInst.samples[i] - in.samples[i])));
                }
                const double cv = r.vocal.samples[f * 2];
                inC += cv * centre[f];
                inL += cv * left[f];
                cE += centre[f] * centre[f];
            }
        const double keepCentre = inC / std::max(cE, 1e-30), leakLeft = inL / std::max(cE, 1e-30);
        say("    %-10s centre tone kept %.3f, hard-left tone in centre %.3f, max |centre+sides-original| %.1e\n", names[m], keepCentre, leakLeft, recon);
        char what[96];
        std::snprintf(what, sizeof what, "%s: centre + sides rebuilds the original", names[m]);
        check(ok && recon < 1e-5, what);
        if (m == 0) {
            check(std::abs(keepCentre - 1.0) < 1e-3 && std::abs(leakLeft - 0.5) < 2e-2, "True M/S is exact mid");
        } else {
            std::snprintf(what, sizeof what, "%s: keeps the centred tone and leaves the hard-left tone in the sides", names[m]);
            check(keepCentre > 0.8 && std::abs(leakLeft) < 0.05, what);
        }
    }
    AudioBuffer mono;
    mono.sampleRate = rate;
    mono.channels = 1;
    mono.samples.assign(frames, 0.1f);
    Settings st;
    st.outputKind = OutputKind::CenterSides;
    check(!extractCenterSides(mono, st), "Centre + sides refuses a mono file");
    std::string a, b;
    outputPaths("C:/x/song.flac", OutputKind::CenterSides, a, b);
    check(a == "C:/x/song_centre.flac" && b == "C:/x/song_sides.flac", "centre + sides file names");

    Settings w;
    w.outputKind = OutputKind::CenterSides;
    w.centerMethod = 5;
    const std::string path = tempPath("cs.ini");
    w.save(path);
    Settings r;
    r.load(path);
    check(r.outputKind == OutputKind::CenterSides && r.centerMethod == 5, "centre + sides settings round-trip");
    fs::remove(fs::u8path(path));
}

int runAll() {
    failures = 0;
    testFft();
    testStackTraces();
    testKaiser();
    testSettingsRoundTrip();
    testStreamingEngines();
    testAlignment();
    testProcessingPaths();
    testGpu();
    testWaveModels();
    testKickDuck();
    testCenterSides();
    testExtract();
    testAlignedPair();
    testLegacyBitExact();
    testResampler();
    testLosslessRoundTrips();
    testLossyRoundTrips();
    testMismatchedInputs();
    testMixedCodecFiles();

    say("\n%s (%d failure%s)\n",
                failures ? "FAILED" : "all tests passed",
                failures, failures == 1 ? "" : "s");
    std::fflush(g_out);
    return failures;
}

}

#ifdef UTAGOE_TEST_DLL
// Smart App Control などで未署名 exe が実行できない環境向けに、DLL として読み込んで実行する入口。
extern "C" __declspec(dllexport) int utagoe_run_tests(const char* logPath) {
    std::FILE* log = (logPath && *logPath) ? std::fopen(logPath, "w") : nullptr;
    if (log) g_out = log;
    const int result = runAll();
    if (log) {
        std::fclose(log);
        g_out = stdout;
    }
    return result;
}
#else
int main() {
    return runAll() ? 1 : 0;
}
#endif
