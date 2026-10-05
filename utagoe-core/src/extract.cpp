// 形式合わせから元実装の本処理までをまとめる入口。
// 形式合わせ (rate / channel の変換) は v3 にない拡張。そろえた後は engine.cpp の ProcessMain をそのまま流す。
// 内部は int16 scale (full scale = 32768)。

#include "utagoe.h"
#include "algorithms/v3/engine.h"
#include "gpu.h"
#include "algorithms/refcancel/rc.h"
#include "algorithms/hardpair/hp.h"
#include "algorithms/bandwidth/bw.h"
#include "algorithms/lowend/lowend.h"
#include "fileio.h"
#include "parallel.h"
#include "log.h"
#include "stepcache.h"

#include <algorithm>
#include <cctype>
#include <atomic>
#include <cmath>
#include <limits>
#include <cstdio>
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
    case WaveModel::Nmf: return "NMF";
    case WaveModel::Spatial: return "Spatial";
    case WaveModel::Ensemble: return "Ensemble";
    case WaveModel::Surface: return "Surface";
    case WaveModel::LowRank: return "Low-rank";
    default: return "v3";
    }
}

struct RcProgress {
    ProgressFn fn;
    void* user;
    float from, to;
};

}

WaveModel waveModelFromInt(int value) {
    switch (value) {
    case 1: case 3: case 7: case 9: case 10: case 13: return WaveModel::Robust;
    case 2: return WaveModel::Kalman;
    case 4: return WaveModel::Nmf;
    case 5: return WaveModel::Spatial;
    case 6: case 12: return WaveModel::Ensemble;
    case 8: return WaveModel::Surface;
    case 11: return WaveModel::LowRank;
    default: return WaveModel::V3;
    }
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
    return m == WaveModel::Surface || m == WaveModel::LowRank;
}

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

struct GccRun {
    GccAligned g;
    bool ok = false;
    std::string error;
};

std::uint64_t u(long long v) { return static_cast<std::uint64_t>(v); }

void writeAudio(stepcache::Writer& w, const rc::Audio& a) {
    w.i64(a.channels);
    w.floats(a.v);
}

rc::Audio readAudio(stepcache::Reader& r) {
    rc::Audio a;
    a.channels = static_cast<int>(r.i64());
    a.v = r.floats();
    return a;
}

void writeBuffer(stepcache::Writer& w, const AudioBuffer& a) {
    w.i64(a.sampleRate);
    w.i64(a.channels);
    w.floats(a.samples);
}

AudioBuffer readBuffer(stepcache::Reader& r) {
    AudioBuffer a;
    a.sampleRate = static_cast<int>(r.i64());
    a.channels = static_cast<int>(r.i64());
    a.samples = r.floats();
    return a;
}

void reused() { log::detail("  reused from the step cache"); }

GccRun runGcc(const rc::Audio& mix, const rc::Audio& ref, int rate, std::size_t frames) {
    std::string k;
    GccRun r;
    if (stepcache::enabled()) {
        k = stepcache::key("gcc", {stepcache::hash(mix.v), stepcache::hash(ref.v), u(mix.channels), u(rate), u(static_cast<long long>(frames))});
        stepcache::Reader in;
        if (stepcache::load("gcc", stepcache::Source::Original, k, in)) {
            r.ok = in.i64() != 0;
            r.error = in.str();
            r.g.warped = readAudio(in);
            r.g.valid = in.chars();
            r.g.a.mode = static_cast<int>(in.i64());
            r.g.a.offset = in.f64();
            r.g.a.slope = in.f64();
            r.g.a.score = in.f64();
            r.g.a.anchors = static_cast<int>(in.i64());
            r.g.a.anchorMad = in.f64();
            r.g.inverted = in.i64() != 0;
            if (in.ok()) {
                reused();
                return r;
            }
            r = GccRun{};
        }
    }
    r.ok = gccAlign(mix, ref, rate, frames, r.g, r.error);
    if (!k.empty()) {
        stepcache::Writer out;
        out.i64(r.ok);
        out.str(r.error);
        writeAudio(out, r.g.warped);
        out.chars(r.g.valid);
        out.i64(r.g.a.mode);
        out.f64(r.g.a.offset);
        out.f64(r.g.a.slope);
        out.f64(r.g.a.score);
        out.i64(r.g.a.anchors);
        out.f64(r.g.a.anchorMad);
        out.i64(r.g.inverted);
        stepcache::save("gcc", stepcache::Source::Original, k, out);
    }
    return r;
}

struct XcorrRun {
    rc::Alignment a;
    rc::Audio warped;
    std::vector<char> valid;
};

XcorrRun runXcorr(const rc::Audio& mix, const rc::Audio& ref, int rate, const rc::Config& cfg) {
    const std::string k = stepcache::key("xcorr", {stepcache::hash(mix.v), stepcache::hash(ref.v), u(mix.channels), u(rate),
                                                   u(static_cast<long long>(mix.frames()))});
    XcorrRun r;
    stepcache::Reader in;
    if (stepcache::load("xcorr", stepcache::Source::Original, k, in)) {
        r.a.mode = static_cast<int>(in.i64());
        r.a.offset = in.f64();
        r.a.slope = in.f64();
        r.a.score = in.f64();
        r.a.anchors = static_cast<int>(in.i64());
        r.a.anchorMad = in.f64();
        r.warped = readAudio(in);
        r.valid = in.chars();
        if (in.ok()) {
            reused();
            return r;
        }
        r = XcorrRun{};
    }
    r.a = rc::estimateAlignment(mix, ref, rate, cfg);
    if (r.a.score >= cfg.minAlignmentScore) r.warped = rc::warpReference(ref, mix.frames(), r.a, cfg.sincTaps, r.valid);
    stepcache::Writer out;
    out.i64(r.a.mode);
    out.f64(r.a.offset);
    out.f64(r.a.slope);
    out.f64(r.a.score);
    out.i64(r.a.anchors);
    out.f64(r.a.anchorMad);
    writeAudio(out, r.warped);
    out.chars(r.valid);
    stepcache::save("xcorr", stepcache::Source::Original, k, out);
    return r;
}

struct DenseRun {
    rc::Audio warped;
    hp::TimeMap map;
};

DenseRun runDense(const rc::Audio& mix, const rc::Audio& ref, int rate, const hp::Progress& progress, bool& cancelled) {
    std::string k;
    DenseRun r;
    if (stepcache::enabled()) {
        k = stepcache::key("dense", {stepcache::hash(mix.v), stepcache::hash(ref.v), u(mix.channels), u(rate)});
        stepcache::Reader in;
        if (stepcache::load("dense", stepcache::Source::Original, k, in)) {
            r.warped = readAudio(in);
            r.map.polarity = static_cast<int>(in.i64());
            const long long stages = in.i64();
            for (long long i = 0; i < stages && in.ok(); ++i) {
                hp::SplineStage st;
                st.knots = in.doubles();
                st.coef = in.doubles();
                st.start = in.f64();
                st.end = in.f64();
                r.map.stages.push_back(std::move(st));
            }
            if (in.ok()) {
                reused();
                return r;
            }
            r = DenseRun{};
        }
    }
    r.warped = hp::alignDense(mix, ref, rate, r.map, progress, cancelled);
    if (!cancelled && !k.empty()) {
        stepcache::Writer out;
        writeAudio(out, r.warped);
        out.i64(r.map.polarity);
        out.i64(static_cast<long long>(r.map.stages.size()));
        for (const hp::SplineStage& st : r.map.stages) {
            out.doubles(st.knots);
            out.doubles(st.coef);
            out.f64(st.start);
            out.f64(st.end);
        }
        stepcache::save("dense", stepcache::Source::Original, k, out);
    }
    return r;
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

AudioBuffer bufferOf(const rc::Audio& a, int rate) {
    AudioBuffer b;
    b.sampleRate = rate;
    b.channels = a.channels;
    b.samples = a.v;
    return b;
}

std::string slug(const std::string& name) {
    std::string out = "_";
    for (char ch : name) out += (ch == ' ' || ch == '-') ? '_' : static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

ExtractResult extractModel(const AudioBuffer& orig, const AudioBuffer& inst, const Settings& settings,
                           v3::Context& c, ExtractResult& res, ProgressFn progress, void* progressUser) {
    const WaveModel model = settings.waveModel;
    const bool algorithm = isAlgorithm(model);
    const std::string name = modelName(model);
    rc::Config cfg;
    if (settings.waveFft > 0 && settings.mergeMode == MergeMode::ByWaveform) {
        cfg.nFft = settings.waveFft;
        cfg.hop = settings.waveFft / 4;
    }
    std::string err;
    if (!algorithm && !parseTimeSpans(settings.fitSpans, cfg.fitSpans, err)) {
        res.error = err;
        return res;
    }
    const int ch = orig.channels, rate = orig.sampleRate;
    const std::size_t frames = orig.frames();
    const float from = c.progressFrom;
    const WaveAlign align = settings.waveAlign;
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
    const bool keepRaw = (settings.saveMask & kSaveRaw) != 0;
    const bool keepMembers = (settings.saveMask & kSaveMembers) != 0;
    cfg.keepMembers = keepMembers;
    std::vector<std::pair<std::string, AudioBuffer>> members;
    const bool prealign = algorithm;
    rc::Alignment cachedAlign;
    bool haveCachedAlign = false;

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
            const auto run = runGcc(mix, raw, rate, frames);
            if (!run.ok) {
                st.fail();
                res.error = run.error;
                return res;
            }
            const GccAligned& g = run.g;
            ref = g.warped;
            char buf[200];
            std::snprintf(buf, sizeof buf, "align:gcc ofs:%.3f drift:%.3fppm score:%.3f phase:%d",
                          -g.a.offset, -g.a.slope * 1e6, g.a.score, g.inverted ? 1 : 0);
            alignInfo = buf;
            res.alignment.offset = static_cast<int>(std::lround(-g.a.offset));
            res.alignment.inverted = g.inverted;
            engineFrom = from + (1.0f - from) * 0.1f;
            cfg.alignment = 0;
            valid = g.valid;
        } else {
            ref.v = inst.samples;
            if (stepcache::enabled() && mix.frames() > static_cast<std::size_t>(cfg.nFft) * 2) {
                log::Stage st("cross-correlation alignment");
                cfg.alignment = 1;
                XcorrRun run = runXcorr(mix, ref, rate, cfg);
                if (run.a.score < cfg.minAlignmentScore) {
                    char msg[200];
                    std::snprintf(msg, sizeof msg, "the alignment confidence %.3f is below %.3f; the instrumental may not match this original",
                                  run.a.score, cfg.minAlignmentScore);
                    st.fail();
                    res.error = name + ": " + msg;
                    return res;
                }
                log::detail("  cross-correlation: offset %.3f samples, drift %+.3f ppm, score %.3f, %d anchors (spread %.3f)",
                            -run.a.offset, -run.a.slope * 1e6, run.a.score, run.a.anchors, run.a.anchorMad);
                ref = std::move(run.warped);
                valid = std::move(run.valid);
                cachedAlign = run.a;
                haveCachedAlign = true;
                cfg.alignment = 0;
            } else {
                cfg.alignment = 1;
            }
        }
    } else if (align == WaveAlign::Dense) {
        ref.v = inst.samples;
        log::Stage st("dense time map");
        const float to = from + (1.0f - from) * 0.3f;
        bool cancelled = false;
        hp::TimeMap map;
        try {
            const auto run = runDense(mix, ref, rate, hpProgress(progress, progressUser, from, to, cancelled), cancelled);
            ref = run.warped;
            map = run.map;
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
        alignInfo = denseInfo(map, frames, rate);
        res.alignment.inverted = map.polarity < 0;
        cfg.alignment = 0;
        valid.assign(frames, 1);
        engineFrom = to;
    } else if (!alignV3()) {
        res.cancelled = true;
        return res;
    }

    rc::Audio estimate;
    char buf[400];
    bool cancelled = false;
    log::Stage modelStage(name + " algorithm" + alignSuffix(align));
    if (algorithm) {
        const hp::Method method = model == WaveModel::Surface ? hp::Method::Surface : hp::Method::LowRank;
        hp::Result r;
        try {
            r = hp::separate(mix, ref, rate, {method}, true, hpProgress(progress, progressUser, engineFrom, 0.97f, cancelled), settings.waveFft);
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
        static const rc::Method methods[] = {rc::Method::Hammerstein, rc::Method::Hammerstein, rc::Method::Kalman, rc::Method::Hammerstein,
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
        if (cfg.alignment == 1 || haveCachedAlign) {
            const rc::Alignment& al = haveCachedAlign ? cachedAlign : r.alignment;
            std::snprintf(buf, sizeof buf, "align:gcc ofs:%.3f drift:%.3fppm score:%.3f anchors:%d",
                          -al.offset, -al.slope * 1e6, al.score, al.anchors);
            alignInfo = buf;
            res.alignment.offset = static_cast<int>(std::lround(-al.offset));
        }
        std::snprintf(buf, sizeof buf, "algorithm:%s %s calib:%d frames coherence:%.3f%s%s", name.c_str(),
                      alignInfo.c_str(), r.calibration.frames, r.calibration.medianCoherence,
                      r.calibration.hasNonlinear ? (r.calibration.nonlinearAccepted ? " nonlinear:accepted" : " nonlinear:rejected") : "",
                      cfg.fitSpans.empty() ? "" : " spans:yes");
        estimate = std::move(r.estimate);
        for (auto& [memberName, audio] : r.members) members.emplace_back(slug(memberName), bufferOf(audio, rate));
    }
    res.alignment.model = buf;
    if (keepRaw) res.extras.emplace_back("_raw", bufferOf(estimate, rate));
    for (auto& m : members) res.extras.push_back(std::move(m));

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
        const auto run = runGcc(mix, ref, orig.sampleRate, frames);
        if (!run.ok) {
            st.fail();
            res.error = run.error;
            return res;
        }
        if (progress && !progress(c.progressFrom + (1.0f - c.progressFrom) * 0.6f, progressUser)) {
            res.cancelled = true;
            return res;
        }
        const GccAligned& g = run.g;
        out = g.warped.v;
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
            const auto run = runDense(mix, ref, orig.sampleRate, hpProgress(progress, progressUser, c.progressFrom, 0.97f, cancelled), cancelled);
            warped = run.warped;
            map = run.map;
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

void outputPaths(const std::string& output, OutputKind, std::string& first, std::string& second) {
    alignedPairPaths(output, first, second);
}

namespace {

bw::Worse matchBandwidth(const AudioBuffer& orig, const AudioBuffer& inst, const AudioInfo* oi, const AudioInfo* ii, AudioBuffer& modified) {
    const double sr = orig.sampleRate;
    const bw::Profile po = bw::analyse(orig.samples, orig.channels, sr), pi = bw::analyse(inst.samples, inst.channels, sr);
    bw::Source so, si;
    if (oi) { so.lossy = !oi->lossless && oi->bitrateKbps <= 1000; so.kbps = oi->bitrateKbps; }
    if (ii) { si.lossy = !ii->lossless && ii->bitrateKbps <= 1000; si.kbps = ii->bitrateKbps; }
    const bw::Report r = bw::decide(po, pi, so, si);
    double score = 0;
    const long long lag = bw::estimateLag(po, pi, score);
    auto nativeRate = [&](const AudioInfo* info) { return info && info->sampleRate > 0 ? info->sampleRate : orig.sampleRate; };
    auto pct = [](double share) { return static_cast<int>(std::lround(share * 100)); };
    auto describe = [&](const char* who, const AudioInfo* info, double hz, double share, double evidence) {
        char kb[32] = "";
        if (info && !info->lossless && info->bitrateKbps > 0) std::snprintf(kb, sizeof kb, " %d kbps", info->bitrateKbps);
        std::string what = "no cutoff found";
        if (share >= 0.3) {
            what = std::to_string(static_cast<int>(std::lround(hz))) + " Hz cutoff in " + std::to_string(pct(share)) +
                   (evidence < 1.0 ? "% of the frames with content up there" : "% of frames");
            if (hz >= 0.475 * nativeRate(info)) what += " (the limit of its " + std::to_string(nativeRate(info)) + " Hz sample rate)";
        }
        log::detail("  %s: %s%s, %s", who, info ? info->codec.c_str() : "?", kb, what.c_str());
    };
    describe("original", oi, po.typicalHz, po.cliffShare, po.evidenceShare);
    describe("instrumental", ii, pi.typicalHz, pi.cliffShare, pi.evidenceShare);
    log::detail("  timing: instrumental frame lag %.2f s (match %.2f)", static_cast<double>(lag) * po.hop / sr, score);
    if (r.worse == bw::Worse::None) {
        log::detail("  both files carry about the same amount of real data; nothing was changed");
        return bw::Worse::None;
    }
    const bool origWorse = r.worse == bw::Worse::Original;
    const char* worseName = origWorse ? "original" : "instrumental";
    const char* betterName = origWorse ? "instrumental" : "original";
    const bw::Profile& worse = origWorse ? po : pi;
    const bw::Profile& better = origWorse ? pi : po;
    const double need = origWorse ? 0.3 : 0.5;
    if (worse.cliffShare < need) {
        log::detail("  the %s has less real data, but its cutoff shows in only %d%% of frames (%d%% needed), too unsteady to follow; the %s was left unchanged",
                    worseName, pct(worse.cliffShare), pct(need), betterName);
        return bw::Worse::None;
    }
    modified = origWorse ? inst : orig;
    const std::vector<double> curve = bw::curveFor(worse, better, r.worse, lag);
    const double removed = bw::lowpassToCurve(modified.samples, modified.channels, sr, curve, better.nFft, better.hop);
    if (removed <= -299) {
        log::detail("  the %s's cutoff sits at the top of the band; the %s was left unchanged", worseName, betterName);
        return bw::Worse::None;
    }
    const double lo = *std::min_element(curve.begin(), curve.end()), hi = *std::max_element(curve.begin(), curve.end());
    log::detail("  the %s has less real data; lowpassed the %s to follow its cutoff (%.0f-%.0f Hz); what was removed sits %.1f dB below the %s",
                worseName, betterName, lo, hi, -removed, betterName);
    return r.worse;
}

}

ExtractResult extract(const AudioBuffer& original,
                      const AudioBuffer& instrumentalIn,
                      const Settings& settings,
                      bool quantize16,
                      ProgressFn progress,
                      void* progressUser,
                      const AudioInfo* originalInfo,
                      const AudioInfo* instrumentalInfo) {
    const X87Extended fpu;
    ExtractResult res;
    gpu::allow(settings.useGpu);
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
        std::string k;
        bool hit = false;
        if (stepcache::enabled()) {
            k = stepcache::key("conform", {stepcache::hash(instrumentalIn.samples), u(instrumentalIn.sampleRate), u(instrumentalIn.channels), u(rate), u(ch)});
            stepcache::Reader in;
            if (stepcache::load("conform", stepcache::Source::Instrumental, k, in)) {
                instConformed = readBuffer(in);
                hit = in.ok() && instConformed.frames() > 0;
                if (hit) reused();
            }
        }
        if (!hit) {
            instConformed = conformAudio(instrumentalIn, rate, ch, &stageTrampoline, &st);
            log::clearStatus(3);
            if (!k.empty() && instConformed.frames() > 0) {
                stepcache::Writer out;
                writeBuffer(out, instConformed);
                stepcache::save("conform", stepcache::Source::Instrumental, k, out);
            }
        }
        if (instConformed.frames() == 0) {
            res.cancelled = true;
            return res;
        }
        instPtr = &instConformed;
    }

    AudioBuffer matched;
    const AudioBuffer* origPtr = &orig;
    if (settings.matchBandwidth && settings.outputKind == OutputKind::Vocal) {
        log::Stage ls("match lowest bandwidth");
        std::string k;
        bool hit = false;
        bw::Worse w = bw::Worse::None;
        if (stepcache::enabled()) {
            auto describeInfo = [](const AudioInfo* i) {
                return i ? i->codec + "/" + std::to_string(i->sampleRate) + "/" + std::to_string(i->bitrateKbps) + "/" + (i->lossless ? "1" : "0") : std::string("-");
            };
            k = stepcache::key("bandwidth", {stepcache::hash(orig.samples), stepcache::hash(instPtr->samples), u(ch), u(rate)},
                               describeInfo(originalInfo) + "|" + describeInfo(instrumentalInfo));
            stepcache::Reader in;
            if (stepcache::load("bandwidth", stepcache::Source::Original, k, in)) {
                w = static_cast<bw::Worse>(in.i64());
                matched = readBuffer(in);
                hit = in.ok();
                if (hit) reused();
            }
        }
        if (!hit) {
            matched = AudioBuffer{};
            w = matchBandwidth(orig, *instPtr, originalInfo, instrumentalInfo, matched);
            if (!k.empty()) {
                stepcache::Writer out;
                out.i64(static_cast<long long>(w));
                writeBuffer(out, w == bw::Worse::None ? AudioBuffer{} : matched);
                stepcache::save("bandwidth", stepcache::Source::Original, k, out);
            }
        }
        if (w == bw::Worse::Original) instPtr = &matched;
        else if (w == bw::Worse::Instrumental) origPtr = &matched;
    }
    const AudioBuffer& src = *origPtr;

    AudioBuffer prealigned;
    if (settings.mergeMode == MergeMode::ByFrequency && settings.freqModel == 0 && settings.waveAlign != WaveAlign::V3Block &&
        settings.outputKind == OutputKind::Vocal) {
        const bool dense = settings.waveAlign == WaveAlign::Dense;
        if (dense && ch != 2) {
            log::warn("  the dense time map needs a stereo original and instrumental; using Utagoe v3 analysis");
        } else {
            log::Stage ls(dense ? "dense time map" : "cross-correlation alignment");
            rc::Audio mixA, refA;
            mixA.channels = refA.channels = ch;
            mixA.v = src.samples;
            refA.v = instPtr->samples;
            bool cancelled = false;
            bool ok = true;
            if (dense) {
                try {
                    refA = runDense(mixA, refA, rate, hpProgress(progress, progressUser, 0.0f, 0.1f, cancelled), cancelled).warped;
                } catch (const std::exception& e) {
                    log::warn("  dense time map failed (%s); using Utagoe v3 analysis", e.what());
                    ok = false;
                }
                log::clearStatus(1);
            } else {
                const auto run = runGcc(mixA, refA, rate, src.frames());
                if (run.ok) {
                    refA = run.g.warped;
                } else {
                    log::warn("  cross-correlation alignment failed (%s); using Utagoe v3 analysis", run.error.c_str());
                    ok = false;
                }
            }
            if (cancelled) {
                res.cancelled = true;
                return res;
            }
            if (ok) {
                prealigned.sampleRate = rate;
                prealigned.channels = ch;
                prealigned.samples = std::move(refA.v);
                prealigned.samples.resize(src.samples.size(), 0.0f);
                instPtr = &prealigned;
            }
        }
    }

    AudioBuffer lowMatched;
    std::string cachedReason;
    const AudioBuffer* instBeforeLowEnd = instPtr;
    float lowTo = -1.0f;
    const bool cutSubsonic = settings.removeSubsonic && settings.outputKind == OutputKind::Vocal;
    if (settings.matchLowEnd && settings.outputKind == OutputKind::Vocal) {
        log::Stage ls("match low end");
        lowMatched.sampleRate = instPtr->sampleRate;
        lowMatched.channels = instPtr->channels;
        const float lowFrom = conform ? 0.10f : 0.0f;
        lowTo = lowFrom + 0.15f;
        Stage st{progress, progressUser, lowFrom, lowTo, true};
        std::string k;
        bool hit = false;
        lowend::Report lr;
        if (stepcache::enabled()) {
            k = stepcache::key("lowend", {stepcache::hash(src.samples), stepcache::hash(instPtr->samples), u(ch), u(rate)});
            stepcache::Reader in;
            if (stepcache::load("lowend", stepcache::Source::Original, k, in)) {
                lr.applied = in.i64() != 0;
                lr.segments = static_cast<int>(in.i64());
                lr.lagSamples = in.f64();
                lr.driftPpm = in.f64();
                lr.phase30 = in.f64();
                lr.phase60 = in.f64();
                lr.phase100 = in.f64();
                lr.phase200 = in.f64();
                lr.gain30Db = in.f64();
                lr.gain60Db = in.f64();
                lr.gain100Db = in.f64();
                lr.predictedLeakDb = in.f64();
                lr.rode = in.i64() != 0;
                lr.ride5Db = in.f64();
                lr.ride95Db = in.f64();
                cachedReason = in.str();
                lr.reason = cachedReason.c_str();
                lowMatched.samples = in.floats();
                hit = in.ok();
                if (hit) reused();
            }
        }
        if (!hit) {
            lowMatched.samples.clear();
            lr = lowend::match(src.samples, instPtr->samples, ch, rate, lowMatched.samples,
                               [&](double f) { return st.report(static_cast<float>(f)); });
            log::clearStatus(3);
            if (!k.empty() && !lr.cancelled) {
                stepcache::Writer out;
                out.i64(lr.applied);
                out.i64(lr.segments);
                out.f64(lr.lagSamples);
                out.f64(lr.driftPpm);
                out.f64(lr.phase30);
                out.f64(lr.phase60);
                out.f64(lr.phase100);
                out.f64(lr.phase200);
                out.f64(lr.gain30Db);
                out.f64(lr.gain60Db);
                out.f64(lr.gain100Db);
                out.f64(lr.predictedLeakDb);
                out.i64(lr.rode);
                out.f64(lr.ride5Db);
                out.f64(lr.ride95Db);
                out.str(lr.reason ? lr.reason : "");
                out.floats(lr.applied ? lowMatched.samples : std::vector<float>{});
                stepcache::save("lowend", stepcache::Source::Original, k, out);
            }
        }
        if (lr.cancelled) {
            res.cancelled = true;
            return res;
        }
        if (lr.applied) {
            log::detail("  low end lined up: phase %+.1f / %+.1f / %+.1f / %+.1f deg at 30 / 60 / 100 / 200 Hz, gain %+.2f / %+.2f / %+.2f dB; "
                        "kick and bass would have leaked at %.1f dB (%d segments, lag %.1f samples, drift %.2f ppm); bass ride %s %+.2f..%+.2f dB",
                        lr.phase30, lr.phase60, lr.phase100, lr.phase200, lr.gain30Db, lr.gain60Db, lr.gain100Db, lr.predictedLeakDb, lr.segments, lr.lagSamples, lr.driftPpm, lr.rode ? "followed" : "not needed", lr.ride5Db, lr.ride95Db);
            instPtr = &lowMatched;
        } else {
            log::detail("  low end left as is: %s", lr.reason);
        }
    }

    const std::vector<float> o = toInt16Scale(src, quantize16);
    const std::vector<float> i = toInt16Scale(*instPtr, quantize16);
    const std::vector<float> iAnalysis = instPtr == &lowMatched ? toInt16Scale(*instBeforeLowEnd, quantize16) : std::vector<float>{};

    v3::Context c;
    c.channels = ch;
    c.rate = rate;
    c.quantize = quantize16;
    c.p = v3::toParams(settings, ch);
    c.orig = {o.data(), static_cast<long long>(src.frames()), ch, 0};
    c.inst = {i.data(), static_cast<long long>(instPtr->frames()), ch, 0};
    if (!iAnalysis.empty()) {
        c.analysisInst = {iAnalysis.data(), static_cast<long long>(instBeforeLowEnd->frames()), ch, 0};
        c.hasAnalysisInst = true;
    }
    c.out.channels = ch;
    c.progress = progress;
    c.progressUser = progressUser;
    c.progressFrom = lowTo >= 0.0f ? lowTo : (conform ? 0.10f : 0.0f);
    c.progressTo = 1.0f;
    setupGpu(settings, c, res);

    if (settings.outputKind == OutputKind::AlignedPair)
        return extractAlignedPair(src, *instPtr, settings, c, res, progress, progressUser);
    auto finish = [&](ExtractResult& r) {
        if (cutSubsonic && r && r.vocal.frames() > 0) {
            log::detail("  removed sub-bass rumble below 20 Hz");
            lowend::subsonicCut(r.vocal.samples, r.vocal.channels, r.vocal.sampleRate);
        }
    };
    if (settings.mergeMode == MergeMode::ByWaveform && settings.waveModel != WaveModel::V3) {
        ExtractResult r = extractModel(src, *instPtr, settings, c, res, progress, progressUser);
        finish(r);
        return r;
    }
    if (settings.mergeMode == MergeMode::ByFrequency && settings.freqModel > 0) {
        Settings asModel = settings;
        asModel.waveModel = settings.freqModel == 1 ? WaveModel::Nmf : WaveModel::Spatial;
        ExtractResult r = extractModel(src, *instPtr, asModel, c, res, progress, progressUser);
        finish(r);
        return r;
    }

    c.tapRaw = (settings.saveMask & kSaveRaw) != 0 && settings.outputKind == OutputKind::Vocal;
    const int rc = v3::processMain(c);
    if (c.cancelled) {
        res.cancelled = true;
        return res;
    }
    if (c.tapRaw) {
        AudioBuffer raw;
        raw.sampleRate = rate;
        raw.channels = ch;
        raw.samples.resize(src.samples.size(), 0.0f);
        const std::size_t got = std::min(raw.samples.size(), c.rawTap.size());
        for (std::size_t i = 0; i < got; ++i) raw.samples[i] = c.rawTap[i] / kScale;
        c.rawTap = {};
        res.extras.emplace_back("_raw", std::move(raw));
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

    finishOutput(c, src, res);
    finish(res);
    if (res.cancelled) return res;
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
