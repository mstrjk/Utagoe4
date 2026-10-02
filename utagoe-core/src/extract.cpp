// 形式合わせから元実装の本処理までをまとめる入口。
// 形式合わせ (rate / channel の変換) は v3 にない拡張。そろえた後は engine.cpp の ProcessMain をそのまま流す。
// 内部は int16 scale (full scale = 32768)。

#include "utagoe.h"
#include "algorithms/v3/engine.h"
#include "gpu.h"
#include "algorithms/refcancel/rc.h"
#include "algorithms/hardpair/hp.h"
#include "algorithms/centresides/cs.h"
#include "algorithms/findwithin/fw.h"
#include "parallel.h"
#include "log.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace utagoe {
namespace {

constexpr float kScale = 32768.0f;

// 進捗の範囲 [from, to] に子処理の進捗を割り当てる。
struct Stage {
    ProgressFn fn;
    void* user;
    float from, to;
    bool logged = false;
    bool report(float f) const {
        if (logged) log::status(3, false, "%s", log::progressBar(f).c_str());
        return !fn || fn(from + (to - from) * f, user);
    }
};

// 元実装は x87 の拡張精度 (64-bit 仮数、C++Builder の既定) で計算する。こちらも long double でそれを再現しているが、
// x87 の精度設定は process ごとに違う (MinGW の exe は 64-bit、.NET など他の host から DLL として呼ぶと 53-bit)。
// host によって結果が変わらないよう、処理の間だけ 64-bit 精度 / 最近接丸めにして、終わったら戻す。
class X87Extended {
public:
#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
    X87Extended() {
        __asm__ volatile("fnstcw %0" : "=m"(saved_));
        unsigned short cw = static_cast<unsigned short>((saved_ & ~0x0F00) | 0x0300);
        __asm__ volatile("fldcw %0" : : "m"(cw));
    }
    ~X87Extended() { __asm__ volatile("fldcw %0" : : "m"(saved_)); }
private:
    unsigned short saved_ = 0;
#endif
};

bool stageTrampoline(float f, void* user) {
    return static_cast<const Stage*>(user)->report(f);
}

// 16-bit 以外の経路では 1/256 sample (24-bit 相当、-144 dB) の格子に乗せる。位置探索の score が整数で正確に求まり、
// CPU と GPU で同じ位置が選ばれる。24-bit 入力はもともとこの格子に乗っているので変わらない。
std::vector<float> toInt16Scale(const AudioBuffer& a, bool quantize) {
    std::vector<float> v(a.samples.size());
    for (std::size_t i = 0; i < v.size(); ++i) {
        const float x = a.samples[i] * kScale;
        v[i] = quantize ? std::clamp(std::nearbyint(x), -32768.0f, 32767.0f)
                        : static_cast<float>(std::nearbyint(static_cast<double>(x) * 256.0) / 256.0);
    }
    return v;
}

// GPU の使い方を決める。UTAGOE_GPU_MIN_WORK で GPU に回す探索の最小規模を変えられる (試験用)。
void setupGpu(const Settings& s, v3::Context& c, ExtractResult& res) {
    if (!s.useGpu) {
        log::detail("GPU: off (turned off in Settings), %d CPU threads", workerCount());
        return;
    }
    const gpu::Status st = gpu::status();
    if (!st.available) {
        log::detail("GPU: not used (%s), %d CPU threads", st.reason.c_str(), workerCount());
        return;
    }
    log::detail("GPU: %s, %s mode, %d CPU threads", st.adapter.c_str(), s.gpuMode == GpuMode::Fastest ? "fastest" : "exact", workerCount());
    c.gpuSearch = true;
    c.gpuFast = s.gpuMode == GpuMode::Fastest;
    if (const char* env = std::getenv("UTAGOE_GPU_MIN_WORK")) c.gpuMinWork = std::atoll(env);
    res.gpuUsed = true;
    res.gpuAdapter = st.adapter;
}

// エンジンは mono / stereo 前提。3ch 以上は stereo へ downmix する。
const AudioBuffer& atMostStereo(const AudioBuffer& in, AudioBuffer& storage) {
    if (in.channels <= 2) return in;
    storage = conformAudio(in, in.sampleRate, 2);
    return storage;
}

// 元実装は入力より長く書き出すことがある (最後の block の残り)。原曲の長さにそろえ、足りない分は無音にする。
void finishOutput(v3::Context& c, const AudioBuffer& orig, ExtractResult& res) {
    const std::size_t n = orig.samples.size();
    std::vector<float>& d = c.out.data;
    d.resize(n, 0.0f);
    res.vocal.sampleRate = orig.sampleRate;
    res.vocal.channels = orig.channels;
    res.vocal.samples.resize(n);
    for (std::size_t i = 0; i < n; ++i) res.vocal.samples[i] = d[i] / kScale;
}

}

namespace {

double parseTime(const std::string& t, bool& ok) {
    // 秒 (小数可)、m:ss、h:mm:ss
    double total = 0.0;
    std::size_t start = 0;
    int parts = 0;
    ok = !t.empty();
    while (ok && start <= t.size()) {
        const std::size_t colon = t.find(':', start);
        const std::string piece = t.substr(start, colon == std::string::npos ? std::string::npos : colon - start);
        char* end = nullptr;
        const double v = std::strtod(piece.c_str(), &end);
        if (piece.empty() || end != piece.c_str() + piece.size() || v < 0) ok = false;
        total = total * 60.0 + v;
        ++parts;
        if (colon == std::string::npos) break;
        start = colon + 1;
    }
    if (parts > 3) ok = false;
    return total;
}

std::string trim(const std::string& s) {
    const std::size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t") - a + 1);
}

const char* modelName(WaveModel m) {
    switch (m) {
    case WaveModel::Robust: return "Robust";
    case WaveModel::Kalman: return "Kalman";
    case WaveModel::Hammerstein: return "Hammerstein";
    case WaveModel::Nmf: return "NMF";
    case WaveModel::Spatial: return "Spatial";
    case WaveModel::Ensemble: return "Ensemble Small";
    case WaveModel::Rational: return "Rational";
    case WaveModel::Surface: return "Surface";
    case WaveModel::Trend: return "Trend";
    case WaveModel::Ctf: return "CTF";
    case WaveModel::LowRank: return "Low-rank";
    case WaveModel::EnsembleLarge: return "Ensemble Large";
    case WaveModel::Auto: return "Auto";
    default: return "v3";
    }
}

struct RcProgress {
    ProgressFn fn;
    void* user;
    float from, to;
};

}

bool parseTimeSpans(const std::string& text, std::vector<std::pair<double, double>>& spans, std::string& error) {
    spans.clear();
    std::string item;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t sep = text.find_first_of(",;", start);
        item = trim(text.substr(start, sep == std::string::npos ? std::string::npos : sep - start));
        if (!item.empty()) {
            const std::size_t dash = item.find('-');
            bool ok1 = false, ok2 = false;
            double a = 0, b = 0;
            if (dash != std::string::npos) {
                a = parseTime(trim(item.substr(0, dash)), ok1);
                b = parseTime(trim(item.substr(dash + 1)), ok2);
            }
            if (!ok1 || !ok2 || !(b > a)) {
                error = "cannot read the vocal-free span \"" + item + "\" (write it like 0-10 or 2:00-2:08)";
                return false;
            }
            spans.emplace_back(a, b);
        }
        if (sep == std::string::npos) break;
        start = sep + 1;
    }
    return true;
}

std::string Alignment::debugString() const {
    char buf[200];
    if (!model.empty()) return " " + model;
    std::snprintf(buf, sizeof buf, " ofs:%d phase:%d level:%f%s BaseOfs:%d Range:%d residual:%f",
                  offset, inverted ? 1 : 0, static_cast<double>(gain), adaptive ? "(adaptive)" : "",
                  driftBase, driftRange, residual);
    return buf;
}

namespace {

// By Waveform の代替モデル。位置合わせ (v3 の block 解析か GCC) -> エンジン -> 元の後処理 (Centralization / LPF / HPF / soft clip)。
// 元実装にない処理なので、16-bit 入力でも丸めずに float で計算する。
bool isAlgorithm(WaveModel m) {
    const int v = static_cast<int>(m);
    return v >= static_cast<int>(WaveModel::Rational) && v <= static_cast<int>(WaveModel::LowRank);
}

constexpr double kLargeStrength = 0.5;
constexpr double kAutoMarginDb = 0.2;

hp::Progress hpProgress(ProgressFn fn, void* user, float from, float to, bool& cancelled) {
    return [fn, user, from, to, &cancelled](double f, const std::string& what) {
        log::status(1, false, "  %s | %s", log::progressBar(f).c_str(), what.c_str());
        if (fn && !fn(from + (to - from) * static_cast<float>(f), user)) cancelled = true;
        return !cancelled;
    };
}

struct GccAligned {
    rc::Audio warped;
    std::vector<char> valid;
    rc::Alignment a;
    bool inverted = false;
};

bool gccAlign(const rc::Audio& mix, const rc::Audio& ref, int rate, std::size_t frames, GccAligned& g, std::string& error) {
    rc::Config cfg;
    g.a = rc::estimateAlignment(mix, ref, rate, cfg);
    if (g.a.score < cfg.minAlignmentScore) {
        char buf[200];
        std::snprintf(buf, sizeof buf, "the alignment confidence %.3f is below %.3f; the instrumental may not match this original",
                      g.a.score, cfg.minAlignmentScore);
        error = buf;
        return false;
    }
    g.warped = rc::warpReference(ref, frames, g.a, cfg.sincTaps, g.valid);
    double dot = 0.0;
    for (std::size_t i = 0; i < g.warped.v.size(); ++i) dot += static_cast<double>(g.warped.v[i]) * mix.v[i];
    g.inverted = dot < 0.0;
    if (g.inverted)
        for (float& v : g.warped.v) v = -v;
    log::detail("  offset %.3f samples, drift %+.3f ppm, score %.3f, %d anchors, phase %s",
                -g.a.offset, -g.a.slope * 1e6, g.a.score, g.a.anchors, g.inverted ? "inverted" : "normal");
    return true;
}

std::string denseInfo(const hp::TimeMap& map, std::size_t frames, int rate) {
    const double sr = static_cast<double>(rate);
    const double end = static_cast<double>(frames > 0 ? frames - 1 : 0);
    const double d0 = map.positionAt(0.0, sr), d1 = map.positionAt(end, sr) - end;
    log::detail("  delay %.3f samples at the start, %.3f at the end, %zu stages, phase %s",
                d0, d1, map.stages.size(), map.polarity < 0 ? "inverted" : "normal");
    char buf[160];
    std::snprintf(buf, sizeof buf, "align:dense start:%.3f end:%.3f phase:%d", d0, d1, map.polarity < 0 ? 1 : 0);
    return buf;
}

const char* alignSuffix(WaveAlign a) {
    switch (a) {
    case WaveAlign::Gcc: return " (cross-correlation alignment)";
    case WaveAlign::Dense: return " (dense time map)";
    default: return "";
    }
}

struct Candidate {
    std::string name;
    rc::Audio background;
    int rank = 0;
    bool scored = true;
};

rc::Audio consensusOf(const rc::Audio& mix, const std::vector<const rc::Audio*>& backgrounds, const rc::Audio& baseline,
                      int rate, const rc::Config& cfg, double strength) {
    const std::size_t len = mix.frames();
    const int C = mix.channels;
    std::vector<double> acc(len * static_cast<std::size_t>(C), 0.0), norm(len, 0.0);
    const std::size_t core = std::max<std::size_t>(static_cast<std::size_t>(cfg.nFft) * 4, static_cast<std::size_t>(cfg.blockSeconds * rate));
    const std::size_t context = std::max<std::size_t>(static_cast<std::size_t>(cfg.nFft), static_cast<std::size_t>(cfg.contextSeconds * rate));
    const std::size_t step = std::max<std::size_t>(1, static_cast<std::size_t>(core * 0.75));
    auto slice = [&](const rc::Audio& a, std::size_t lo, std::size_t hi) {
        rc::Audio s;
        s.channels = C;
        s.v.assign(a.v.begin() + static_cast<std::ptrdiff_t>(lo * C), a.v.begin() + static_cast<std::ptrdiff_t>(hi * C));
        return s;
    };
    for (std::size_t a = 0; a < len; a += step) {
        const std::size_t b = std::min(a + core, len);
        const std::size_t left = a >= context ? a - context : 0, right = std::min(len, b + context);
        std::vector<rc::Spec> specs;
        specs.reserve(backgrounds.size());
        for (const rc::Audio* bg : backgrounds) specs.push_back(rc::stft(slice(*bg, left, right), cfg.nFft, cfg.hop));
        const rc::Spec base = rc::stft(slice(baseline, left, right), cfg.nFft, cfg.hop);
        std::vector<const rc::Spec*> ptrs;
        for (const auto& s : specs) ptrs.push_back(&s);
        const rc::Audio removed = rc::istft(rc::consensusBackground(ptrs, base, strength), cfg.nFft, cfg.hop, right - left);
        const std::size_t length = b - a;
        for (std::size_t i = 0; i < length; ++i) {
            double w = std::pow(std::sin(3.14159265358979323846 * (static_cast<double>(i) + 0.5) / length), 2);
            if (a == 0 && i < std::min(step / 2, length)) w = 1.0;
            if (b == len && i + step / 2 >= length) w = 1.0;
            norm[a + i] += w;
            for (int c = 0; c < C; ++c) acc[(a + i) * static_cast<std::size_t>(C) + c] += static_cast<double>(removed.at(a + i - left, c)) * w;
        }
    }
    rc::Audio out;
    out.channels = C;
    out.v.resize(len * static_cast<std::size_t>(C));
    for (std::size_t i = 0; i < len; ++i)
        for (int c = 0; c < C; ++c)
            out.at(i, c) = static_cast<float>(acc[i * static_cast<std::size_t>(C) + c] / std::max(norm[i], 1e-12));
    return out;
}

rc::Audio subtractAudio(const rc::Audio& a, const rc::Audio& b) {
    rc::Audio r = a;
    for (std::size_t i = 0; i < r.v.size(); ++i) r.v[i] = a.v[i] - b.v[i];
    return r;
}

double sideLeftoverDb(const rc::Audio& mix, const rc::Audio& background, const std::vector<char>& valid) {
    double s = 0.0;
    std::size_t n = 0;
    for (std::size_t i = 0; i < mix.frames(); ++i) {
        if (!valid.empty() && !valid[i]) continue;
        const double d = 0.5 * ((static_cast<double>(mix.at(i, 0)) - background.at(i, 0)) - (static_cast<double>(mix.at(i, 1)) - background.at(i, 1)));
        s += d * d;
        ++n;
    }
    return 10.0 * std::log10(s / std::max<std::size_t>(n, 1) + 1e-30);
}

double distanceDb(const rc::Audio& a, const rc::Audio& b) {
    double e = 0.0, s = 0.0;
    for (std::size_t i = 0; i < a.v.size(); ++i) {
        const double d = static_cast<double>(a.v[i]) - b.v[i];
        e += d * d;
        s += static_cast<double>(b.v[i]) * b.v[i];
    }
    return 10.0 * std::log10((e + 1e-30) / (s + 1e-30));
}

ExtractResult extractModel(const AudioBuffer& orig, const AudioBuffer& inst, const Settings& settings,
                           v3::Context& c, ExtractResult& res, ProgressFn progress, void* progressUser) {
    const WaveModel model = settings.waveModel;
    const bool algorithm = isAlgorithm(model);
    const bool autoPick = model == WaveModel::Auto;
    const bool multi = autoPick || model == WaveModel::EnsembleLarge;
    const std::string name = modelName(model);
    rc::Config cfg;
    std::string err;
    if (!algorithm && !parseTimeSpans(settings.fitSpans, cfg.fitSpans, err)) {
        res.error = err;
        return res;
    }
    const int ch = orig.channels, rate = orig.sampleRate;
    const std::size_t frames = orig.frames();
    const float from = c.progressFrom;
    WaveAlign align = settings.waveAlign;
    if (autoPick) {
        align = ch == 2 ? WaveAlign::Dense : WaveAlign::V3Block;
        log::detail("  Auto: alignment %s", ch == 2 ? "dense time map" : "Utagoe v3 analysis (mono input)");
    }
    if (ch != 2 && (algorithm || align == WaveAlign::Dense)) {
        res.error = algorithm ? name + ": this algorithm needs a stereo original and instrumental"
                              : "the dense time map needs a stereo original and instrumental";
        return res;
    }

    rc::Audio mix;
    mix.channels = ch;
    mix.v = orig.samples;
    rc::Audio ref;
    ref.channels = ch;
    std::vector<char> valid;
    float engineFrom = from;
    std::string alignInfo;
    const bool kick = settings.kickDuck && ch == 2;
    if (settings.kickDuck && ch != 2) log::warn("  kick duck matching skipped: it needs a stereo original and instrumental");
    const bool prealign = algorithm || multi || kick;

    auto alignV3 = [&]() {
        cfg.alignment = 0;
        c.collect = true;
        c.progressTo = from + (1.0f - from) * 0.3f;
        v3::processMain(c);
        if (c.cancelled) return false;
        ref.v.assign(frames * static_cast<std::size_t>(ch), 0.0f);
        valid.assign(frames, 0);
        const std::size_t got = std::min(frames, c.colValid.size());
        for (std::size_t f = 0; f < got; ++f) {
            valid[f] = c.colValid[f];
            for (int k = 0; k < ch; ++k)
                ref.v[f * static_cast<std::size_t>(ch) + static_cast<std::size_t>(k)] =
                    c.colRef[f * static_cast<std::size_t>(ch) + static_cast<std::size_t>(k)] / kScale;
        }
        engineFrom = c.progressTo;
        char buf[160];
        std::snprintf(buf, sizeof buf, "align:v3 ofs:%d phase:%d BaseOfs:%d Range:%d",
                      c.usedOffset, c.usedPhase ? 1 : 0, c.usedBase, c.usedRange);
        alignInfo = buf;
        res.alignment.offset = c.usedOffset;
        res.alignment.inverted = c.usedPhase;
        res.alignment.driftBase = c.usedBase;
        res.alignment.driftRange = c.usedRange;
        return true;
    };

    if (align == WaveAlign::Gcc) {
        if (prealign) {
            log::Stage st("cross-correlation alignment");
            rc::Audio raw;
            raw.channels = ch;
            raw.v = inst.samples;
            GccAligned g;
            if (!gccAlign(mix, raw, rate, frames, g, err)) {
                st.fail();
                res.error = err;
                return res;
            }
            ref = std::move(g.warped);
            char buf[200];
            std::snprintf(buf, sizeof buf, "align:gcc ofs:%.3f drift:%.3fppm score:%.3f phase:%d",
                          -g.a.offset, -g.a.slope * 1e6, g.a.score, g.inverted ? 1 : 0);
            alignInfo = buf;
            res.alignment.offset = static_cast<int>(std::lround(-g.a.offset));
            res.alignment.inverted = g.inverted;
            engineFrom = from + (1.0f - from) * 0.1f;
            cfg.alignment = 0;
            valid = std::move(g.valid);
        } else {
            cfg.alignment = 1;
            ref.v = inst.samples;
        }
    } else if (align == WaveAlign::Dense) {
        ref.v = inst.samples;
        log::Stage st("dense time map");
        const float to = from + (1.0f - from) * 0.3f;
        bool cancelled = false;
        hp::TimeMap map;
        bool fellBack = false;
        try {
            ref = hp::alignDense(mix, ref, rate, map, hpProgress(progress, progressUser, from, to, cancelled), cancelled);
        } catch (const std::exception& e) {
            log::clearStatus(1);
            st.fail();
            if (!autoPick) {
                log::error(std::string("dense time map: ") + e.what(), log::lastThrowTrace());
                res.error = std::string("dense time map: ") + e.what();
                return res;
            }
            log::warn("  Auto: the dense time map failed (%s); using Utagoe v3 analysis instead", e.what());
            fellBack = true;
        }
        log::clearStatus(1);
        if (cancelled) {
            res.cancelled = true;
            return res;
        }
        if (fellBack) {
            if (!alignV3()) {
                res.cancelled = true;
                return res;
            }
        } else {
            alignInfo = denseInfo(map, frames, rate);
            res.alignment.inverted = map.polarity < 0;
            cfg.alignment = 0;
            valid.assign(frames, 1);
            engineFrom = to;
        }
    } else if (!alignV3()) {
        res.cancelled = true;
        return res;
    }

    if (kick) {
        log::Stage st("kick duck matching");
        try {
            const hp::KickDuck k = hp::kickDuck(mix, ref, rate);
            log::detail("  %d kicks, dip %.2f dB, attack %.1f ms, release %.1f ms, strength %.2f, held-out change %+.2f dB",
                        k.kicks, k.depthDb, k.attackMs, k.releaseMs, k.strength, k.changeDb);
            if (k.strength > 0.0) {
                hp::applyGainDb(ref, k.gainDb);
                char kb[64];
                std::snprintf(kb, sizeof kb, " kickduck:%.2fdBx%.2f", k.depthDb, k.strength);
                alignInfo += kb;
            } else {
                log::detail("  no repeatable kick ducking found; the instrumental is left as it is");
            }
        } catch (const std::exception& e) {
            log::warn("  kick duck matching skipped: %s", e.what());
        }
        if (progress && !progress(engineFrom, progressUser)) {
            res.cancelled = true;
            return res;
        }
    }

    rc::Audio estimate;
    char buf[400];
    bool cancelled = false;
    log::Stage modelStage(name + (autoPick ? "" : " algorithm") + (autoPick ? "" : alignSuffix(align)));
    if (multi) {
        static const std::pair<rc::Method, const char*> rcSet[] = {{rc::Method::Robust, "Robust"}, {rc::Method::Kalman, "Kalman"},
                                                                    {rc::Method::Hammerstein, "Hammerstein"}, {rc::Method::Nmf, "NMF"},
                                                                    {rc::Method::Spatial, "Spatial"}};
        static const int rcRank[] = {0, 2, 3, -1, -1};
        const bool stereo = ch == 2;
        const double units = 5.0 + (stereo ? 3.0 : 0.0);
        const float span = 0.93f - engineFrom;
        std::vector<Candidate> cands;
        int robust = -1;
        for (int i = 0; i < 5; ++i) {
            const float a = engineFrom + span * static_cast<float>(i / units), b = engineFrom + span * static_cast<float>((i + 1) / units);
            log::detail("  running %s", rcSet[i].second);
            rc::Result r;
            try {
                r = rc::separate(mix, ref, valid, rate, cfg, rcSet[i].first, [&](double f) {
                    if (progress && !progress(a + (b - a) * static_cast<float>(f), progressUser)) cancelled = true;
                    return !cancelled;
                });
            } catch (const std::exception& e) {
                r.error = e.what();
            }
            if (cancelled) {
                res.cancelled = true;
                return res;
            }
            if (!r.error.empty()) {
                log::warn("  %s left out: %s", rcSet[i].second, r.error.c_str());
                continue;
            }
            if (rcSet[i].first == rc::Method::Hammerstein && !r.calibration.nonlinearAccepted) {
                log::detail("  Hammerstein left out: its nonlinear part was rejected, so it would only repeat Robust");
                continue;
            }
            Candidate cand{rcSet[i].second, subtractAudio(mix, r.estimate), rcRank[i], rcRank[i] >= 0};
            if (rcSet[i].first == rc::Method::Robust) robust = static_cast<int>(cands.size());
            cands.push_back(std::move(cand));
        }
        if (robust < 0) {
            modelStage.fail();
            res.error = name + ": Robust failed, so there is nothing safe to fall back on";
            return res;
        }
        if (stereo) {
            const float a = engineFrom + span * static_cast<float>(5.0 / units);
            log::detail("  running Rational, Surface, Trend, CTF and Low-rank");
            try {
                hp::Result r = hp::separate(mix, ref, rate, {hp::Method::Rational, hp::Method::Surface, hp::Method::Trend, hp::Method::Ctf, hp::Method::LowRank},
                                            true, hpProgress(progress, progressUser, a, 0.93f, cancelled));
                log::clearStatus(1);
                if (cancelled || r.cancelled) {
                    res.cancelled = true;
                    return res;
                }
                static const std::pair<hp::Method, int> hpRank[] = {{hp::Method::Rational, 1}, {hp::Method::Trend, 4}, {hp::Method::Surface, 5},
                                                                    {hp::Method::Ctf, 6}, {hp::Method::LowRank, 7}};
                for (auto& [m, est] : r.residuals) {
                    int rank = 0;
                    for (const auto& [hm, hr] : hpRank)
                        if (hm == m) rank = hr;
                    cands.push_back({hp::methodName(m), subtractAudio(mix, est), rank, true});
                }
            } catch (const std::exception& e) {
                log::clearStatus(1);
                if (cancelled) {
                    res.cancelled = true;
                    return res;
                }
                log::warn("  Rational, Surface, Trend, CTF and Low-rank left out: %s", e.what());
            }
        }
        if (progress && !progress(0.93f, progressUser)) {
            res.cancelled = true;
            return res;
        }

        std::vector<const rc::Audio*> all;
        for (const auto& cd : cands) all.push_back(&cd.background);
        log::detail("  consensus of %zu", all.size());
        rc::Audio large = consensusOf(mix, all, cands[static_cast<std::size_t>(robust)].background, rate, cfg, kLargeStrength);
        for (const auto& cd : cands)
            log::detail("    %-12s %6.1f dB from the consensus", cd.name.c_str(), distanceDb(cd.background, large));

        std::string pickName = "Ensemble Large";
        if (autoPick) {
            std::vector<const rc::Audio*> small;
            for (const auto& cd : cands)
                if (cd.name == "Robust" || cd.name == "Kalman" || cd.name == "Hammerstein") small.push_back(&cd.background);
            Candidate smallC{"Ensemble Small", consensusOf(mix, small, cands[static_cast<std::size_t>(robust)].background, rate, cfg, cfg.ensembleStrength), 8, true};
            Candidate largeC{"Ensemble Large", std::move(large), 9, true};
            std::vector<Candidate*> pool;
            for (auto& cd : cands)
                if (cd.scored) pool.push_back(&cd);
            pool.push_back(&smallC);
            pool.push_back(&largeC);
            Candidate* pick = &smallC;
            if (stereo) {
                std::vector<double> scores;
                double best = std::numeric_limits<double>::infinity();
                for (Candidate* cd : pool) {
                    scores.push_back(sideLeftoverDb(mix, cd->background, valid));
                    best = std::min(best, scores.back());
                }
                log::detail("  Auto: backing left in the stereo difference (lower is better)");
                int bestRank = 1 << 30;
                for (std::size_t i = 0; i < pool.size(); ++i) {
                    log::detail("    %-14s %7.2f dB", pool[i]->name.c_str(), scores[i]);
                    if (scores[i] <= best + kAutoMarginDb && pool[i]->rank < bestRank) {
                        bestRank = pool[i]->rank;
                        pick = pool[i];
                    }
                }
            } else {
                log::detail("  Auto: mono input has no stereo difference to compare by; using Ensemble Small");
            }
            log::line("  Auto picked %s", pick->name.c_str());
            pickName = pick->name;
            estimate = subtractAudio(mix, pick->background);
        } else {
            estimate = subtractAudio(mix, large);
        }
        std::snprintf(buf, sizeof buf, "algorithm:%s%s%s %s candidates:%zu%s", name.c_str(), autoPick ? "->" : "", autoPick ? pickName.c_str() : "",
                      alignInfo.c_str(), cands.size(), cfg.fitSpans.empty() ? "" : " spans:yes");
    } else if (algorithm) {
        const hp::Method method = static_cast<hp::Method>(static_cast<int>(model) - static_cast<int>(WaveModel::Rational));
        hp::Result r;
        try {
            r = hp::separate(mix, ref, rate, {method}, true, hpProgress(progress, progressUser, engineFrom, 0.97f, cancelled));
        } catch (const std::exception& e) {
            log::clearStatus(1);
            modelStage.fail();
            log::error(name + ": " + e.what(), log::lastThrowTrace());
            res.error = name + ": " + e.what();
            return res;
        }
        log::clearStatus(1);
        if (cancelled || r.cancelled || r.residuals.empty()) {
            res.cancelled = true;
            return res;
        }
        log::detail("  allpass order %d at %.2f Hz, delay %.3f samples, gain %.4f",
                    r.phase.order, r.phase.breakHz, r.phase.delay, r.phase.gain);
        res.alignment.gain = static_cast<float>(r.phase.gain);
        std::snprintf(buf, sizeof buf, "algorithm:%s %s allpass:%.2fHz delay:%.3f gain:%.4f", name.c_str(), alignInfo.c_str(),
                      r.phase.breakHz, r.phase.delay, r.phase.gain);
        estimate = std::move(r.residuals.front().second);
    } else {
        static const rc::Method methods[] = {rc::Method::Robust, rc::Method::Robust, rc::Method::Kalman, rc::Method::Hammerstein,
                                             rc::Method::Nmf, rc::Method::Spatial, rc::Method::Ensemble};
        const rc::Method method = methods[static_cast<int>(model)];
        RcProgress rp{progress, progressUser, engineFrom, 0.97f};
        rc::Result r;
        try {
            r = rc::separate(mix, ref, valid, rate, cfg, method, [&](double f) {
                if (rp.fn && !rp.fn(static_cast<float>(rp.from + (rp.to - rp.from) * f), rp.user)) cancelled = true;
                return !cancelled;
            });
        } catch (const std::exception& e) {
            r.error = e.what();
            log::error(name + ": " + e.what(), log::lastThrowTrace());
        }
        if (cancelled) {
            res.cancelled = true;
            return res;
        }
        if (!r.error.empty()) {
            res.error = name + ": " + r.error;
            return res;
        }
        if (cfg.alignment == 1) {
            std::snprintf(buf, sizeof buf, "align:gcc ofs:%.3f drift:%.3fppm score:%.3f anchors:%d",
                          -r.alignment.offset, -r.alignment.slope * 1e6, r.alignment.score, r.alignment.anchors);
            alignInfo = buf;
            res.alignment.offset = static_cast<int>(std::lround(-r.alignment.offset));
        }
        std::snprintf(buf, sizeof buf, "algorithm:%s %s calib:%d frames coherence:%.3f%s%s", name.c_str(),
                      alignInfo.c_str(), r.calibration.frames, r.calibration.medianCoherence,
                      r.calibration.hasNonlinear ? (r.calibration.nonlinearAccepted ? " nonlinear:accepted" : " nonlinear:rejected") : "",
                      cfg.fitSpans.empty() ? "" : " spans:yes");
        estimate = std::move(r.estimate);
    }
    res.alignment.model = buf;

    // 元の後処理を通して書き出す。
    v3::Context post;
    post.channels = ch;
    post.rate = rate;
    post.quantize = false;
    post.p = c.p;
    post.out.channels = ch;
    post.gpuFast = c.gpuFast;
    v3::Pipeline pipe;
    pipe.init(post, false);
    constexpr std::size_t kPiece = 65536;
    std::vector<double> l(kPiece), rr(kPiece);
    for (std::size_t at = 0; at < frames; at += kPiece) {
        const std::size_t n = std::min(kPiece, frames - at);
        for (std::size_t k = 0; k < n; ++k) {
            l[k] = estimate.at(at + k, 0) * static_cast<double>(kScale);
            rr[k] = ch > 1 ? estimate.at(at + k, 1) * static_cast<double>(kScale) : 0.0;
        }
        pipe.pushOutput(l.data(), rr.data(), static_cast<int>(n));
    }
    pipe.flush();
    finishOutput(post, orig, res);
    if (progress) progress(1.0f, progressUser);
    return res;
}

}

namespace {

// 位置合わせだけを行い、原曲とその時間軸に合わせたインストを返す (v3 にない拡張)。
// 位置合わせは By Waveform の Alignment 設定に従う。v3 の block 解析なら block ごとの drift と極性まで、
// GCC なら offset と clock drift を合わせ、極性は相関の符号で決める。音量は変えない。
ExtractResult extractAlignedPair(const AudioBuffer& orig, const AudioBuffer& inst, const Settings& settings,
                                 v3::Context& c, ExtractResult& res, ProgressFn progress, void* progressUser) {
    const int ch = orig.channels;
    const std::size_t frames = orig.frames();
    res.alignedInst.sampleRate = orig.sampleRate;
    res.alignedInst.channels = ch;
    res.alignedInst.samples.assign(frames * static_cast<std::size_t>(ch), 0.0f);
    std::vector<float>& out = res.alignedInst.samples;
    std::size_t covered = 0;

    if (settings.waveAlign == WaveAlign::Gcc) {
        log::Stage st("cross-correlation alignment");
        rc::Audio mix, ref;
        mix.channels = ref.channels = ch;
        mix.v = orig.samples;
        ref.v = inst.samples;
        if (progress && !progress(c.progressFrom, progressUser)) {
            res.cancelled = true;
            return res;
        }
        GccAligned g;
        std::string err;
        if (!gccAlign(mix, ref, orig.sampleRate, frames, g, err)) {
            st.fail();
            res.error = err;
            return res;
        }
        if (progress && !progress(c.progressFrom + (1.0f - c.progressFrom) * 0.6f, progressUser)) {
            res.cancelled = true;
            return res;
        }
        out = std::move(g.warped.v);
        for (char v : g.valid) covered += v ? 1 : 0;
        res.alignment.offset = static_cast<int>(std::lround(-g.a.offset));
        res.alignment.inverted = g.inverted;
        char buf[200];
        std::snprintf(buf, sizeof buf, "pair align:gcc ofs:%.3f drift:%.3fppm score:%.3f phase:%d",
                      -g.a.offset, -g.a.slope * 1e6, g.a.score, g.inverted ? 1 : 0);
        res.alignment.model = buf;
    } else if (settings.waveAlign == WaveAlign::Dense) {
        log::Stage st("dense time map");
        if (ch != 2) {
            st.fail();
            res.error = "the dense time map needs a stereo original and instrumental";
            return res;
        }
        rc::Audio mix, ref;
        mix.channels = ref.channels = ch;
        mix.v = orig.samples;
        ref.v = inst.samples;
        bool cancelled = false;
        hp::TimeMap map;
        rc::Audio warped;
        try {
            warped = hp::alignDense(mix, ref, orig.sampleRate, map,
                                    hpProgress(progress, progressUser, c.progressFrom, 0.97f, cancelled), cancelled);
        } catch (const std::exception& e) {
            log::clearStatus(1);
            st.fail();
            log::error(std::string("dense time map: ") + e.what(), log::lastThrowTrace());
            res.error = std::string("dense time map: ") + e.what();
            return res;
        }
        log::clearStatus(1);
        if (cancelled) {
            res.cancelled = true;
            return res;
        }
        out = std::move(warped.v);
        covered = frames;
        res.alignment.inverted = map.polarity < 0;
        res.alignment.model = "pair " + denseInfo(map, frames, orig.sampleRate);
    } else {
        // v3 の解析と block ごとの drift 探索。代替モデルと同じく、減算の代わりに位置を合わせたインストを集める。
        // 抽出はしないので、抽出方式の設定に関係なく By Waveform の探索 (oversampling も含む) で揃える。
        c.p.mergeMode = 1;
        c.collect = true;
        v3::processMain(c);
        if (c.cancelled) {
            res.cancelled = true;
            return res;
        }
        const std::size_t got = std::min(frames, c.colValid.size());
        for (std::size_t f = 0; f < got; ++f) {
            if (!c.colValid[f]) continue;
            ++covered;
            for (int k = 0; k < ch; ++k) {
                const std::size_t i = f * static_cast<std::size_t>(ch) + static_cast<std::size_t>(k);
                out[i] = c.colRef[i] / kScale;
            }
        }
        res.alignment.offset = c.usedOffset;
        res.alignment.inverted = c.usedPhase;
        res.alignment.driftBase = c.usedBase;
        res.alignment.driftRange = c.usedRange;
        char buf[200];
        std::snprintf(buf, sizeof buf, "pair align:v3 ofs:%d phase:%d BaseOfs:%d Range:%d",
                      c.usedOffset, c.usedPhase ? 1 : 0, c.usedBase, c.usedRange);
        res.alignment.model = buf;
    }

    // インストが届かない所 (冒頭の探索幅や、インストの方が短い所) は無音になる。
    if (covered < frames)
        log::detail("  the instrumental covers %s of %s; the rest of the aligned instrumental is silent",
                    log::clock(static_cast<double>(covered) / orig.sampleRate).c_str(),
                    log::clock(static_cast<double>(frames) / orig.sampleRate).c_str());
    res.vocal = orig;
    if (progress) progress(1.0f, progressUser);
    return res;
}

}

void alignedPairPaths(const std::string& output, std::string& mainPath, std::string& instPath) {
    const std::size_t slash = output.find_last_of("/\\");
    const std::size_t dot = output.find_last_of('.');
    const bool hasExt = dot != std::string::npos && (slash == std::string::npos || dot > slash);
    const std::string stem = hasExt ? output.substr(0, dot) : output;
    const std::string ext = hasExt ? output.substr(dot) : "";
    mainPath = stem + "_main" + ext;
    instPath = stem + "_inst" + ext;
}

void outputPaths(const std::string& output, OutputKind kind, std::string& first, std::string& second) {
    if (kind != OutputKind::CenterSides && kind != OutputKind::Repeats) {
        alignedPairPaths(output, first, second);
        return;
    }
    const std::size_t slash = output.find_last_of("/\\");
    const std::size_t dot = output.find_last_of('.');
    const bool hasExt = dot != std::string::npos && (slash == std::string::npos || dot > slash);
    const std::string stem = hasExt ? output.substr(0, dot) : output;
    const std::string ext = hasExt ? output.substr(dot) : "";
    if (kind == OutputKind::Repeats) {
        first = stem + "_repeats";
        second = first + (slash == std::string::npos ? std::string("/") : output.substr(slash, 1)) + "repeats.csv";
        return;
    }
    first = stem + "_centre" + ext;
    second = stem + "_sides" + ext;
}

ExtractResult extractCenterSides(const AudioBuffer& original, const Settings& settings, ProgressFn progress, void* progressUser) {
    ExtractResult res;
    if (original.frames() == 0) {
        res.error = "the original input is empty";
        return res;
    }
    if (original.channels < 2) {
        res.error = "Centre + sides needs a stereo file";
        return res;
    }
    const cs::Method method = static_cast<cs::Method>(std::clamp(settings.centerMethod, 0, cs::kMethods - 1));
    const std::size_t frames = original.frames();
    const int ch = original.channels;
    std::vector<float> x(frames * 2);
    for (std::size_t i = 0; i < frames; ++i) {
        x[2 * i] = original.samples[i * static_cast<std::size_t>(ch)];
        x[2 * i + 1] = original.samples[i * static_cast<std::size_t>(ch) + 1];
    }
    if (ch > 2) log::detail("  using the first two of %d channels", ch);
    log::Stage st(std::string("centre + sides (") + cs::methodName(method) + ")");
    bool cancelled = false;
    const cs::Split s = cs::centerOf(x, original.sampleRate, method, [&](double f) {
        log::status(1, false, "  %s", log::progressBar(f).c_str());
        if (progress && !progress(static_cast<float>(f), progressUser)) cancelled = true;
        return !cancelled;
    });
    log::clearStatus(1);
    if (cancelled || s.cancelled) {
        res.cancelled = true;
        return res;
    }
    res.vocal.sampleRate = res.alignedInst.sampleRate = original.sampleRate;
    res.vocal.channels = res.alignedInst.channels = 2;
    res.vocal.samples.resize(frames * 2);
    res.alignedInst.samples.resize(frames * 2);
    double ce = 0.0, se = 0.0, te = 0.0;
    for (std::size_t i = 0; i < frames; ++i) {
        const double c = s.center[i];
        res.vocal.samples[2 * i] = res.vocal.samples[2 * i + 1] = static_cast<float>(c);
        for (int k = 0; k < 2; ++k) {
            const double v = x[2 * i + static_cast<std::size_t>(k)];
            res.alignedInst.samples[2 * i + static_cast<std::size_t>(k)] = static_cast<float>(v - c);
            ce += c * c;
            se += (v - c) * (v - c);
            te += v * v;
        }
    }
    auto db = [&](double e) { return 10.0 * std::log10((e + 1e-30) / (te + 1e-30)); };
    log::detail("  centre %.1f dB, sides %.1f dB of the original, mask mean %.3f", db(ce), db(se), s.maskMean);
    char buf[160];
    std::snprintf(buf, sizeof buf, "centre/sides:%s centre:%.1fdB sides:%.1fdB mask:%.3f", cs::methodName(method), db(ce), db(se), s.maskMean);
    res.alignment.model = buf;
    if (progress) progress(1.0f, progressUser);
    return res;
}

namespace {

std::string clockLabel(double seconds) {
    const double s = std::max(0.0, seconds);
    const int m = static_cast<int>(s / 60);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%dm%05.2fs", m, s - 60.0 * m);
    return buf;
}

AudioBuffer toBuffer(const fw::Audio& a, int sr) {
    AudioBuffer b;
    b.sampleRate = sr;
    b.channels = a.channels;
    b.samples.resize(a.v.size());
    for (std::size_t i = 0; i < a.v.size(); ++i) b.samples[i] = static_cast<float>(a.v[i]);
    return b;
}

}

RepeatsResult extractRepeats(const AudioBuffer& original, const Settings& settings, const std::function<bool(RepeatClip&)>& sink,
                             ProgressFn progress, void* progressUser) {
    RepeatsResult res;
    if (original.frames() == 0) {
        res.error = "the original input is empty";
        return res;
    }
    const int sr = original.sampleRate;
    const int ch = std::min(original.channels, 2);
    const std::size_t frames = original.frames();
    fw::Audio a;
    a.channels = ch;
    a.v.resize(frames * static_cast<std::size_t>(ch));
    for (std::size_t i = 0; i < frames; ++i)
        for (int c = 0; c < ch; ++c) a.at(i, c) = original.samples[i * static_cast<std::size_t>(original.channels) + static_cast<std::size_t>(c)];
    if (original.channels > 2) log::detail("  using the first two of %d channels", original.channels);
    fw::Config cfg;
    static const char* guides[] = {"auto", "full", "side"};
    cfg.guide = guides[std::clamp(settings.repeatGuide, 0, 2)];
    if (ch < 2 && cfg.guide == "side") cfg.guide = "full";
    if (settings.repeatBreadth == 1) {
        cfg.maxMatches = 400;
        cfg.renderLimit = 120;
    }
    log::Stage st("repeats (" + cfg.guide + (settings.repeatBreadth == 1 ? ", broad" : "") + ")");
    bool cancelled = false, failed = false;
    int passed = 0;
    auto report = [&](double f, const std::string&) {
        log::status(1, false, "  %s", log::progressBar(f).c_str());
        if (failed || (progress && !progress(static_cast<float>(f), progressUser))) cancelled = true;
        return !cancelled;
    };
    auto emit = [&](RepeatClip& clip) {
        if (clip.passed) {
            ++passed;
            char num[16];
            std::snprintf(num, sizeof num, "%02d_", passed);
            clip.name = num + clip.name;
            if (!failed && sink && !sink(clip)) failed = true;
        }
        clip.shared = AudioBuffer();
        clip.difference = AudioBuffer();
        res.clips.push_back(std::move(clip));
    };
    fw::Outcome o;
    try {
        o = fw::run(a, sr, cfg, report,
                    [&](const fw::Match& m, fw::Extraction& x) {
                        RepeatClip c;
                        c.name = clockLabel(m.target) + "_from_" + clockLabel(m.source);
                        c.status = x.status;
                        c.quality = x.quality;
                        c.guide = x.guide;
                        c.source = m.source;
                        c.target = m.target;
                        c.duration = static_cast<double>(x.target.frames()) / sr;
                        c.reductionDb = x.heldOut.reductionDb;
                        c.correlation = x.heldOut.correlation;
                        c.delayMs = x.delaySamples * 1000.0 / sr;
                        c.ratePpm = x.ratePpm;
                        c.passed = x.status == "validated_contrast" || x.status == "near_null_repeat";
                        if (c.passed) {
                            c.shared = toBuffer(x.shared, sr);
                            c.difference = toBuffer(x.residual, sr);
                        }
                        emit(c);
                    },
                    [&](fw::JointResult& j) {
                        RepeatClip c;
                        c.joint = true;
                        c.name = clockLabel(j.targetStart) + "_joint";
                        for (const std::string& s : j.sources) c.name += "_" + s.substr(6);
                        c.status = j.accepted ? "validated_joint_contrast" : "joint_not_demonstrably_better";
                        c.quality = j.accepted ? "joint" : "not_verified";
                        c.target = j.targetStart;
                        c.duration = j.duration;
                        c.reductionDb = j.heldOut.reductionDb;
                        c.correlation = j.heldOut.correlation;
                        c.passed = j.accepted;
                        if (c.passed) {
                            c.shared = toBuffer(j.shared, sr);
                            c.difference = toBuffer(j.residual, sr);
                        }
                        emit(c);
                    });
    } catch (const std::exception& e) {
        log::clearStatus(1);
        if (failed) return res;
        res.error = std::string("repeat search failed: ") + e.what();
        return res;
    }
    log::clearStatus(1);
    if (failed) return res;
    if (o.cancelled || cancelled) {
        res.cancelled = true;
        return res;
    }
    res.regions = static_cast<int>(o.matches.size());
    for (const std::string& e : o.errors) log::detail("  skipped %s", e.c_str());
    log::detail("  %d regions checked, %d passed the cancellation test", res.regions, passed);
    if (progress) progress(1.0f, progressUser);
    return res;
}

ExtractResult extract(const AudioBuffer& original,
                      const AudioBuffer& instrumentalIn,
                      const Settings& settings,
                      bool quantize16,
                      ProgressFn progress,
                      void* progressUser) {
    const X87Extended fpu;
    ExtractResult res;
    res.instrumentalRateIn = instrumentalIn.sampleRate;
    res.instrumentalChannelsIn = instrumentalIn.channels;

    if (original.frames() == 0) {
        res.error = "the original input is empty";
        return res;
    }
    if (instrumentalIn.frames() == 0) {
        res.error = "the instrumental input is empty";
        return res;
    }

    AudioBuffer origStereo;
    const AudioBuffer& orig = atMostStereo(original, origStereo);
    const int ch = orig.channels;
    const int rate = orig.sampleRate;

    // インストを原曲の sample rate / channel 数へ合わせる。同じ内容の別形式なので、そろえれば位置合わせできる。
    const AudioBuffer* instPtr = &instrumentalIn;
    AudioBuffer instConformed;
    const bool conform = instrumentalIn.sampleRate != rate || instrumentalIn.channels != ch;
    if (conform) {
        char what[96];
        std::snprintf(what, sizeof what, "convert instrumental %d Hz %d ch -> %d Hz %d ch",
                      instrumentalIn.sampleRate, instrumentalIn.channels, rate, ch);
        log::Stage ls(what);
        Stage st{progress, progressUser, 0.0f, 0.10f, true};
        instConformed = conformAudio(instrumentalIn, rate, ch, &stageTrampoline, &st);
        log::clearStatus(3);
        if (instConformed.frames() == 0) {
            res.cancelled = true;
            return res;
        }
        instPtr = &instConformed;
    }

    const std::vector<float> o = toInt16Scale(orig, quantize16);
    const std::vector<float> i = toInt16Scale(*instPtr, quantize16);

    v3::Context c;
    c.channels = ch;
    c.rate = rate;
    c.quantize = quantize16;
    c.p = v3::toParams(settings, ch);
    c.orig = {o.data(), static_cast<long long>(orig.frames()), ch, 0};
    c.inst = {i.data(), static_cast<long long>(instPtr->frames()), ch, 0};
    c.out.channels = ch;
    c.progress = progress;
    c.progressUser = progressUser;
    c.progressFrom = conform ? 0.10f : 0.0f;
    c.progressTo = 1.0f;
    setupGpu(settings, c, res);

    if (settings.outputKind == OutputKind::AlignedPair)
        return extractAlignedPair(orig, *instPtr, settings, c, res, progress, progressUser);
    if (settings.mergeMode == MergeMode::ByWaveform && settings.waveModel != WaveModel::V3)
        return extractModel(orig, *instPtr, settings, c, res, progress, progressUser);

    const int rc = v3::processMain(c);
    if (c.cancelled) {
        res.cancelled = true;
        return res;
    }
    if (rc != 0) {
        res.error = rc == 1 ? "memory allocation failed" : "failed to write the output";
        return res;
    }

    res.alignment.offset = c.usedOffset;
    res.alignment.inverted = c.usedPhase;
    res.alignment.gain = static_cast<float>(c.usedLevel);
    res.alignment.adaptive = c.adaptiveLevel;
    res.alignment.driftBase = c.usedBase;
    res.alignment.driftRange = c.usedRange;
    res.alignment.residual = c.analysed && c.org != 0.0 ? c.voc / c.org : 0.0;

    finishOutput(c, orig, res);
    if (progress) progress(1.0f, progressUser);
    return res;
}

bool sameFile(const std::string& a, const std::string& b) {
    if (a == b) return true;
    std::error_code ec;
    const bool same = std::filesystem::equivalent(std::filesystem::u8path(a), std::filesystem::u8path(b), ec);
    return !ec && same;
}

ExtractResult extractFiltersOnly(const AudioBuffer& original,
                                 const Settings& settings,
                                 bool quantize16,
                                 ProgressFn progress,
                                 void* progressUser) {
    const X87Extended fpu;
    ExtractResult res;
    res.instrumentalRateIn = original.sampleRate;
    res.instrumentalChannelsIn = original.channels;
    if (original.frames() == 0) {
        res.error = "the original input is empty";
        return res;
    }

    AudioBuffer origStereo;
    const AudioBuffer& orig = atMostStereo(original, origStereo);
    const std::vector<float> o = toInt16Scale(orig, quantize16);

    v3::Context c;
    c.channels = orig.channels;
    c.rate = orig.sampleRate;
    c.quantize = quantize16;
    c.p = v3::toParams(settings, orig.channels);
    c.orig = {o.data(), static_cast<long long>(orig.frames()), orig.channels, 0};
    c.out.channels = orig.channels;
    c.progress = progress;
    c.progressUser = progressUser;
    setupGpu(settings, c, res);

    v3::processAlt(c);
    if (c.cancelled) {
        res.cancelled = true;
        return res;
    }
    finishOutput(c, orig, res);
    if (progress) progress(1.0f, progressUser);
    return res;
}

}
