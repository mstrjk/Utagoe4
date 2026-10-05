// C++ コアを C ABI で包む薄いラッパー。

#include "utagoe_c.h"
#include "gpu.h"
#include "log.h"
#include "parallel.h"
#include "fileio.h"
#include "stepcache.h"

#include <FLAC/format.h>
#include <opus.h>
#include <opusenc.h>
#include <vorbis/codec.h>
#include <dr_wav.h>
#include <dr_mp3.h>

#include <chrono>
#include <exception>
#include "utagoe.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <string>

using namespace utagoe;

namespace {

void copyString(char* dst, std::size_t cap, const std::string& src) {
    if (!dst || cap == 0) return;
    const std::size_t n = std::min(src.size(), cap - 1);
    std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

void toCpp(const UtagoeSettings& c, Settings& s) {
    s.procMode  = static_cast<ProcMode>(c.procMode);
    s.mergeMode = static_cast<MergeMode>(c.mergeMode);
    s.introMode = static_cast<IntroMode>(c.introMode);
    s.levelAdpt = static_cast<LevelAdpt>(c.levelAdpt);
    s.adptMode  = static_cast<AdptMode>(c.adptMode);
    s.krkPhase  = static_cast<KrkPhase>(c.krkPhase);
    s.soundQty  = static_cast<SoundQty>(c.soundQty);

    s.oversample    = c.oversample != 0;
    s.oversampleMul = c.oversampleMul;
    s.blockSizeMs   = c.blockSizeMs;
    s.adptRange     = c.adptRange;

    s.centralize    = c.centralize != 0;
    s.centralizePos = c.centralizePos;

    s.lowPass     = c.lowPass != 0;
    s.lowPassPos  = c.lowPassPos;
    s.highPass    = c.highPass != 0;
    s.highPassPos = c.highPassPos;

    s.extractLevel = c.extractLevel;
    s.instLevel    = c.instLevel;

    s.searchInstFile = c.searchInstFile != 0;
    s.autoNameOutput = c.autoNameOutput != 0;
    s.outputSuffix.assign(c.outputSuffix,
                          strnlen(c.outputSuffix, UTAGOE_SUFFIX_MAX));

    s.outputFormat  = static_cast<OutputFormat>(c.outputFormat);
    s.outputDepth   = static_cast<OutputDepth>(c.outputDepth);
    s.outputBitrate = c.outputBitrate;

    s.useGpu          = c.useGpu != 0;
    s.gpuMode         = c.gpuMode == 1 ? GpuMode::Fastest : GpuMode::Exact;
    s.gpuNoticeHidden = c.gpuNoticeHidden != 0;

    s.waveModel = waveModelFromInt(c.waveModel);
    s.waveAlign = (c.waveAlign >= 0 && c.waveAlign <= 2) ? static_cast<WaveAlign>(c.waveAlign) : WaveAlign::V3Block;
    s.fitSpans.assign(c.fitSpans, strnlen(c.fitSpans, UTAGOE_SPANS_MAX));
    s.outputKind = (c.outputKind >= 0 && c.outputKind <= 1) ? static_cast<OutputKind>(c.outputKind) : OutputKind::Vocal;
    s.outputFolder.assign(c.outputFolder, strnlen(c.outputFolder, UTAGOE_PATH_MAX));
    s.appIcon.assign(c.appIcon, strnlen(c.appIcon, UTAGOE_NAME_MAX));
    s.uiLanguage.assign(c.uiLanguage, strnlen(c.uiLanguage, UTAGOE_NAME_MAX));
    s.overwriteOutput = c.overwriteOutput != 0;
    s.matchBandwidth = c.matchBandwidth != 0;
    s.normalizeOutput = c.normalizeOutput != 0;
    s.matchLowEnd = c.matchLowEnd != 0;
    s.removeSubsonic = c.removeSubsonic != 0;
    s.waveFft = validFft(c.waveFft);
    s.cacheSteps = c.cacheSteps != 0;
    s.freqModel = (c.freqModel >= 0 && c.freqModel <= 2) ? c.freqModel : 0;
    s.saveMask = (c.saveMask & (kSaveDefault | kSaveAligned | kSaveRaw | kSaveMembers)) ? c.saveMask & 15 : kSaveDefault;
}

void toC(const Settings& s, UtagoeSettings& c) {
    c.procMode  = static_cast<int32_t>(s.procMode);
    c.mergeMode = static_cast<int32_t>(s.mergeMode);
    c.introMode = static_cast<int32_t>(s.introMode);
    c.levelAdpt = static_cast<int32_t>(s.levelAdpt);
    c.adptMode  = static_cast<int32_t>(s.adptMode);
    c.krkPhase  = static_cast<int32_t>(s.krkPhase);
    c.soundQty  = static_cast<int32_t>(s.soundQty);

    c.oversample    = s.oversample ? 1 : 0;
    c.oversampleMul = s.oversampleMul;
    c.blockSizeMs   = s.blockSizeMs;
    c.adptRange     = s.adptRange;

    c.centralize    = s.centralize ? 1 : 0;
    c.centralizePos = s.centralizePos;

    c.lowPass     = s.lowPass ? 1 : 0;
    c.lowPassPos  = s.lowPassPos;
    c.highPass    = s.highPass ? 1 : 0;
    c.highPassPos = s.highPassPos;

    c.extractLevel = s.extractLevel;
    c.instLevel    = s.instLevel;

    c.searchInstFile = s.searchInstFile ? 1 : 0;
    c.autoNameOutput = s.autoNameOutput ? 1 : 0;
    copyString(c.outputSuffix, UTAGOE_SUFFIX_MAX, s.outputSuffix);

    c.outputFormat  = static_cast<int32_t>(s.outputFormat);
    c.outputDepth   = static_cast<int32_t>(s.outputDepth);
    c.outputBitrate = s.outputBitrate;

    c.useGpu          = s.useGpu ? 1 : 0;
    c.gpuMode         = static_cast<int32_t>(s.gpuMode);
    c.gpuNoticeHidden = s.gpuNoticeHidden ? 1 : 0;

    c.waveModel = static_cast<int32_t>(s.waveModel);
    c.waveAlign = static_cast<int32_t>(s.waveAlign);
    copyString(c.fitSpans, UTAGOE_SPANS_MAX, s.fitSpans);
    c.outputKind = static_cast<int32_t>(s.outputKind);
    copyString(c.outputFolder, UTAGOE_PATH_MAX, s.outputFolder);
    copyString(c.appIcon, UTAGOE_NAME_MAX, s.appIcon);
    copyString(c.uiLanguage, UTAGOE_NAME_MAX, s.uiLanguage);
    c.overwriteOutput = s.overwriteOutput ? 1 : 0;
    c.matchBandwidth = s.matchBandwidth ? 1 : 0;
    c.normalizeOutput = s.normalizeOutput ? 1 : 0;
    c.matchLowEnd = s.matchLowEnd ? 1 : 0;
    c.removeSubsonic = s.removeSubsonic ? 1 : 0;
    c.waveFft = s.waveFft;
    c.cacheSteps = s.cacheSteps ? 1 : 0;
    c.freqModel = s.freqModel;
    c.saveMask = s.saveMask;
}

void setError(char* buf, int32_t len, const std::string& msg) {
    if (len > 0) copyString(buf, static_cast<std::size_t>(len), msg);
}

struct ProgressBridge {
    UtagoeProgress fn;
    void* user;
    float from = 0.0f, to = 1.0f;
};

bool beforeWrite(UtagoeProgress progress, void* user) {
    return !progress || progress(-1.0f, user) != 0;
}

void normalise(const Settings& s, OutputFormat format, std::initializer_list<AudioBuffer*> outs, const char* what) {
    if (!s.normalizeOutput) return;
    float peak = 0.0f;
    for (const AudioBuffer* b : outs)
        for (float v : b->samples) peak = std::max(peak, std::abs(v));
    const float ceiling = isLossless(format) ? 0.989f : 0.891f;
    if (!(peak > ceiling) || !std::isfinite(peak)) return;
    const float gain = ceiling / peak;
    for (AudioBuffer* b : outs)
        for (float& v : b->samples) v *= gain;
    log::line("normalised %s: peak %+.2f dBFS, turned down %.2f dB", what, 20.0 * std::log10(peak), -20.0 * std::log10(gain));
}

bool progressTrampoline(float fraction, void* user) {
    auto* b = static_cast<ProgressBridge*>(user);
    if (b && fraction >= 0.0f) fraction = b->from + (b->to - b->from) * fraction;
    return !b || !b->fn || b->fn(fraction, b->user) != 0;
}

}

extern "C" {

UTAGOE_API void UTAGOE_CALL utagoe_default_settings(UtagoeSettings* s) {
    if (!s) return;
    std::memset(s, 0, sizeof *s);
    toC(Settings{}, *s);
}

namespace {

std::string describe(const AudioInfo& i, const AudioBuffer& a) {
    char buf[256];
    const double secs = a.sampleRate ? static_cast<double>(a.frames()) / a.sampleRate : 0.0;
    std::snprintf(buf, sizeof buf, "%s, %d Hz, %s%s, %d ch, %s", i.codec.c_str(), i.sampleRate,
                  i.lossless ? (i.floatingPoint ? "32-bit float" : (std::to_string(i.bits) + "-bit").c_str())
                             : (std::to_string(i.bitrateKbps) + " kbps").c_str(),
                  i.lossless ? "" : " lossy", i.channels, log::clock(secs).c_str());
    return buf;
}

const char* methodName(const Settings& s) {
    if (s.mergeMode == MergeMode::ByFrequency) return "Frequency";
    switch (s.waveModel) {
    case WaveModel::Robust: return "Waveform / Robust";
    case WaveModel::Kalman: return "Waveform / Kalman";
    case WaveModel::Nmf: return "Waveform / NMF";
    case WaveModel::Spatial: return "Waveform / Spatial";
    case WaveModel::Ensemble: return "Waveform / Ensemble";
    case WaveModel::Surface: return "Waveform / Surface";
    case WaveModel::LowRank: return "Waveform / Low-rank";
    default: return "Waveform";
    }
}

void writeInfo(stepcache::Writer& w, const AudioInfo& i) {
    w.str(i.codec);
    w.i64(i.sampleRate);
    w.i64(i.channels);
    w.i64(i.bits);
    w.i64(i.floatingPoint);
    w.i64(i.lossless);
    w.i64(i.bitrateKbps);
    w.i64(i.frames);
}

AudioInfo readInfo(stepcache::Reader& r) {
    AudioInfo i;
    i.codec = r.str();
    i.sampleRate = static_cast<int>(r.i64());
    i.channels = static_cast<int>(r.i64());
    i.bits = static_cast<int>(r.i64());
    i.floatingPoint = r.i64() != 0;
    i.lossless = r.i64() != 0;
    i.bitrateKbps = static_cast<int>(r.i64());
    i.frames = r.i64();
    return i;
}

bool decodeCached(const std::string& path, stepcache::Source from, AudioBuffer& out, AudioInfo& info, std::string& err) {
    std::string k;
    if (stepcache::enabled()) {
        std::error_code ec;
#ifdef _WIN32
        const std::filesystem::path p(widenUtf8(path));
#else
        const std::filesystem::path p(path);
#endif
        const auto size = std::filesystem::file_size(p, ec);
        const auto stamp = ec ? 0 : std::filesystem::last_write_time(p, ec).time_since_epoch().count();
        if (!ec) {
            k = stepcache::key("decode", {static_cast<std::uint64_t>(size), static_cast<std::uint64_t>(stamp)}, path);
            stepcache::Reader in;
            if (stepcache::load("decode", from, k, in)) {
                info = readInfo(in);
                out.sampleRate = static_cast<int>(in.i64());
                out.channels = static_cast<int>(in.i64());
                out.samples = in.floats();
                if (in.ok() && out.frames() > 0) {
                    log::detail("  reused from the step cache");
                    return true;
                }
            }
        }
    }
    if (!decodeAudio(path, out, &info, err)) return false;
    if (!k.empty()) {
        stepcache::Writer w;
        writeInfo(w, info);
        w.i64(out.sampleRate);
        w.i64(out.channels);
        w.floats(out.samples);
        stepcache::save("decode", from, k, w);
    }
    return true;
}

int32_t extractFileImpl(
    const char* originalPath,
    const char* instrumentalPath,
    const char* outputPath,
    const UtagoeSettings* settings,
    UtagoeProgress progress,
    void* progressUser,
    UtagoeResult* result,
    char* errorBuf,
    int32_t errorLen) {

    if (!originalPath || !instrumentalPath || !outputPath || !settings) {
        setError(errorBuf, errorLen, "null argument");
        return 1;
    }

    Settings s;
    toCpp(*settings, s);
    stepcache::configure(s.cacheSteps);
    stepcache::setSources(originalPath, instrumentalPath);

    log::line("original     %s", originalPath);
    log::line("instrumental %s", instrumentalPath);
    if (s.outputKind == OutputKind::Vocal) {
        int m = s.saveMask & (kSaveDefault | kSaveAligned | kSaveRaw | kSaveMembers);
        const bool members = s.mergeMode == MergeMode::ByWaveform &&
                             s.waveModel == WaveModel::Ensemble;
        if (!members) m &= ~kSaveMembers;
        s.saveMask = m ? m : kSaveDefault;
        if (s.saveMask == kSaveAligned) s.outputKind = OutputKind::AlignedPair;
    }
    const bool pair = s.outputKind == OutputKind::AlignedPair;
    std::string mainOut, instOut;
    if (pair) {
        outputPaths(outputPath, s.outputKind, mainOut, instOut);
        log::line("output       %s", mainOut.c_str());
        log::line("             %s", instOut.c_str());
    } else {
        log::line("output       %s", outputPath);
    }
    log::detail("method: %s | %s | intro %d | level %d | drift %s | phase %d | block %d ms%s%s%s",
                methodName(s), s.procMode == ProcMode::Normal ? "normal" : s.procMode == ProcMode::LRDifference ? "L-R difference" : "mono",
                static_cast<int>(s.introMode), static_cast<int>(s.levelAdpt), s.adptMode == AdptMode::Automatic ? "auto" : "manual",
                static_cast<int>(s.krkPhase), s.blockSizeMs, s.oversample ? (" | oversample x" + std::to_string(s.oversampleMul)).c_str() : "",
                s.centralize ? " | centralization" : "", (s.lowPass || s.highPass) ? " | filters" : "");

    AudioBuffer orig, inst;
    AudioInfo origInfo, instInfo;
    std::string err;
    {
        log::Stage st("decode original");
        if (!decodeCached(originalPath, stepcache::Source::Original, orig, origInfo, err)) {
            st.fail();
            setError(errorBuf, errorLen, "Original: " + err);
            log::error("cannot read the original: " + err, "");
            return 2;
        }
        log::detail("original: %s", describe(origInfo, orig).c_str());
    }
    // 元実装に合わせて、両方に同じファイルを指定したら後処理だけを掛ける。
    const bool same = sameFile(originalPath, instrumentalPath);
    if (same && pair) {
        setError(errorBuf, errorLen, "the aligned pair needs two different files (the original and its instrumental)");
        log::error("the aligned pair needs two different files", "");
        return 3;
    }
    if (same) {
        instInfo = origInfo;
        log::line("the same file is used for both inputs: post-processing only");
    } else {
        log::Stage st("decode instrumental");
        if (!decodeCached(instrumentalPath, stepcache::Source::Instrumental, inst, instInfo, err)) {
            st.fail();
            setError(errorBuf, errorLen, "Instrumental: " + err);
            log::error("cannot read the instrumental: " + err, "");
            return 3;
        }
        log::detail("instrumental: %s", describe(instInfo, inst).c_str());
    }

    const OutputFormat format = formatFromExtension(outputPath, s.outputFormat);
    const OutputDepth depth = resolveDepth(s.outputDepth, format, origInfo, instInfo);
    // v3 と同じ 16-bit 入出力のときだけ、各段を元実装どおり 16-bit へ丸める。
    auto is16 = [](const AudioInfo& i) { return i.lossless && !i.floatingPoint && i.bits > 0 && i.bits <= 16; };
    const bool quantize16 = depth == OutputDepth::Int16 && is16(origInfo) && is16(instInfo);

    const bool wantMain = pair || (s.saveMask & kSaveDefault) != 0 || same;
    const bool wantAligned = !pair && !same && (s.saveMask & kSaveAligned) != 0;
    ProgressBridge bridge{progress, progressUser, 0.0f, wantAligned ? 0.8f : 1.0f};
    ExtractResult r = same
        ? extractFiltersOnly(orig, s, quantize16, progress ? &progressTrampoline : nullptr, progress ? &bridge : nullptr)
        : extract(orig, inst, s, quantize16, progress ? &progressTrampoline : nullptr, progress ? &bridge : nullptr, &origInfo, &instInfo);

    if (r.cancelled) { setError(errorBuf, errorLen, "cancelled"); log::warn("cancelled"); return 4; }
    if (!r.error.empty()) { setError(errorBuf, errorLen, r.error); log::error(r.error, ""); return 5; }
    log::detail("alignment:%s", r.alignment.debugString().c_str());

    ExtractResult aligned;
    if (wantAligned) {
        Settings ps = s;
        ps.outputKind = OutputKind::AlignedPair;
        ProgressBridge pairBridge{progress, progressUser, 0.8f, 1.0f};
        log::line("aligned pair");
        aligned = extract(orig, inst, ps, quantize16, progress ? &progressTrampoline : nullptr, progress ? &pairBridge : nullptr, &origInfo, &instInfo);
        if (aligned.cancelled) { setError(errorBuf, errorLen, "cancelled"); log::warn("cancelled"); return 4; }
        if (!aligned.error.empty()) { setError(errorBuf, errorLen, aligned.error); log::error(aligned.error, ""); return 5; }
    }

    if (!beforeWrite(progress, progressUser)) { setError(errorBuf, errorLen, "cancelled"); log::warn("cancelled"); return 4; }
    if ((s.saveMask & kSaveMembers) && !pair && !same && std::none_of(r.extras.begin(), r.extras.end(), [](const auto& e) { return e.first != "_raw"; }))
        log::warn("no ensemble members to save: only Ensemble has them");
    EncodeOptions enc;
    enc.format = format;
    enc.depth = depth;
    enc.bitrate = s.outputBitrate;
    struct Target { std::string path; AudioBuffer* audio; std::string what; };
    std::vector<Target> targets;
    if (pair) {
        normalise(s, format, {&r.vocal, &r.alignedInst}, "aligned pair");
        targets = {{mainOut, &r.vocal, "original"}, {instOut, &r.alignedInst, "aligned instrumental"}};
    } else {
        const std::size_t slash = std::string(outputPath).find_last_of("/\\");
        const std::size_t dot = std::string(outputPath).find_last_of('.');
        const bool hasExt = dot != std::string::npos && (slash == std::string::npos || dot > slash);
        const std::string stem = hasExt ? std::string(outputPath).substr(0, dot) : std::string(outputPath);
        const std::string ext = hasExt ? std::string(outputPath).substr(dot) : "";
        const bool keepMain = wantMain || r.extras.empty();
        std::vector<AudioBuffer*> vocals;
        if (keepMain) {
            targets.push_back({outputPath, &r.vocal, "vocals"});
            vocals.push_back(&r.vocal);
        }
        for (auto& [suffix, audio] : r.extras) {
            targets.push_back({stem + suffix + ext, &audio, suffix.substr(1)});
            vocals.push_back(&audio);
        }
        if (s.normalizeOutput) {
            float peak = 0.0f;
            for (const AudioBuffer* b : vocals)
                for (float v : b->samples) peak = std::max(peak, std::abs(v));
            const float ceiling = isLossless(format) ? 0.989f : 0.891f;
            if (peak > ceiling && std::isfinite(peak)) {
                const float gain = ceiling / peak;
                for (AudioBuffer* b : vocals)
                    for (float& v : b->samples) v *= gain;
                log::line("normalised vocals: peak %+.2f dBFS, turned down %.2f dB", 20.0 * std::log10(peak), -20.0 * std::log10(gain));
            }
        }
        if (wantAligned) {
            normalise(s, format, {&aligned.vocal, &aligned.alignedInst}, "aligned pair");
            std::string pm, pi;
            alignedPairPaths(outputPath, pm, pi);
            targets.push_back({pm, &aligned.vocal, "original"});
            targets.push_back({pi, &aligned.alignedInst, "aligned instrumental"});
        }
    }
    std::string written;
    for (const Target& t : targets) {
        log::Stage st("encode " + t.what + " " + extensionOf(format));
        if (!encodeAudio(t.path, *t.audio, enc, err)) {
            st.fail();
            setError(errorBuf, errorLen, "Output: " + err);
            log::error("cannot write " + t.path + ": " + err, "");
            return 6;
        }
        written += t.path + "\n";
    }

    if (result) {
        std::memset(result, 0, sizeof *result);
        result->offset   = r.alignment.offset;
        result->inverted = r.alignment.inverted ? 1 : 0;
        result->gain     = r.alignment.gain;
        result->residual = r.alignment.residual;
        copyString(result->debug, sizeof result->debug, r.alignment.debugString());
        result->outputFormat   = static_cast<int32_t>(format);
        result->outputDepth    = static_cast<int32_t>(depth);
        result->outputRate     = r.vocal.sampleRate;
        result->outputChannels = r.vocal.channels;
        result->instrumentalRateIn     = r.instrumentalRateIn;
        result->instrumentalChannelsIn = r.instrumentalChannelsIn;
        result->gpuUsed = r.gpuUsed ? 1 : 0;
        copyString(result->gpuAdapter, sizeof result->gpuAdapter, r.gpuAdapter);
        copyString(result->written, sizeof result->written, written);
    }
    return 0;
}

}

UTAGOE_API int32_t UTAGOE_CALL utagoe_extract_file(
    const char* originalPath, const char* instrumentalPath, const char* outputPath,
    const UtagoeSettings* settings, UtagoeProgress progress, void* progressUser,
    UtagoeResult* result, char* errorBuf, int32_t errorLen) {
    // 例外は DLL の境界を越えさせない。投げた位置の stack trace をつけて記録し、error として返す。
    try {
        const int32_t rc = extractFileImpl(originalPath, instrumentalPath, outputPath, settings, progress,
                                           progressUser, result, errorBuf, errorLen);
        return rc;
    } catch (const std::exception& e) {
        const std::string trace = log::lastThrowTrace();
        log::error(std::string("unexpected error: ") + e.what(), trace);
        setError(errorBuf, errorLen, std::string("unexpected error: ") + e.what() + " (details in the Help terminal)");
    } catch (...) {
        log::error("unexpected error of unknown type", log::lastThrowTrace());
        setError(errorBuf, errorLen, "unexpected error (details in the Help terminal)");
    }
    return 7;
}

UTAGOE_API void UTAGOE_CALL utagoe_aligned_pair_paths(const char* outputPath, char* mainBuf, char* instBuf, int32_t len) {
    if (!outputPath || len <= 0) return;
    std::string m, i;
    alignedPairPaths(outputPath, m, i);
    if (mainBuf) copyString(mainBuf, static_cast<std::size_t>(len), m);
    if (instBuf) copyString(instBuf, static_cast<std::size_t>(len), i);
}

UTAGOE_API int32_t UTAGOE_CALL utagoe_probe_audio(const char* path, UtagoeAudioInfo* info,
                                                  char* errorBuf, int32_t errorLen) {
    if (!path || !info) { setError(errorBuf, errorLen, "null argument"); return 1; }
    AudioInfo a;
    std::string err;
    if (!probeAudio(path, a, err)) { setError(errorBuf, errorLen, err); return 2; }
    std::memset(info, 0, sizeof *info);
    copyString(info->codec, UTAGOE_CODEC_MAX, a.codec);
    info->sampleRate    = a.sampleRate;
    info->channels      = a.channels;
    info->bits          = a.bits;
    info->floatingPoint = a.floatingPoint ? 1 : 0;
    info->lossless      = a.lossless ? 1 : 0;
    info->bitrateKbps   = a.bitrateKbps;
    info->frames        = a.frames;
    return 0;
}

UTAGOE_API int32_t UTAGOE_CALL utagoe_format_from_path(const char* path, int32_t fallback) {
    if (!path) return fallback;
    return static_cast<int32_t>(formatFromExtension(path, static_cast<OutputFormat>(fallback)));
}

UTAGOE_API const char* UTAGOE_CALL utagoe_format_extension(int32_t format) {
    return extensionOf(static_cast<OutputFormat>(format));
}

UTAGOE_API int32_t UTAGOE_CALL utagoe_transcode_file(const char* inputPath, const char* outputPath,
                                                     int32_t format, int32_t depth, int32_t bitrateKbps,
                                                     char* errorBuf, int32_t errorLen) {
    if (!inputPath || !outputPath) { setError(errorBuf, errorLen, "null argument"); return 1; }
    AudioBuffer audio;
    AudioInfo info;
    std::string err;
    if (!decodeAudio(inputPath, audio, &info, err)) { setError(errorBuf, errorLen, err); return 2; }

    EncodeOptions enc;
    enc.format = format < 0 ? formatFromExtension(outputPath, OutputFormat::Wav) : static_cast<OutputFormat>(format);
    enc.depth = resolveDepth(static_cast<OutputDepth>(depth), enc.format, info, info);
    enc.bitrate = bitrateKbps;
    if (!encodeAudio(outputPath, audio, enc, err)) { setError(errorBuf, errorLen, err); return 3; }
    return 0;
}

UTAGOE_API int32_t UTAGOE_CALL utagoe_load_ini(const char* path, UtagoeSettings* s) {
    if (!path || !s) return 1;
    Settings cpp;
    toCpp(*s, cpp);
    if (!cpp.load(path)) return 2;
    toC(cpp, *s);
    return 0;
}

UTAGOE_API int32_t UTAGOE_CALL utagoe_save_ini(const char* path, const UtagoeSettings* s) {
    if (!path || !s) return 1;
    Settings cpp;
    toCpp(*s, cpp);
    return cpp.save(path) ? 0 : 2;
}

UTAGOE_API void UTAGOE_CALL utagoe_gpu_status(UtagoeGpuStatus* status) {
    if (!status) return;
    std::memset(status, 0, sizeof *status);
    const gpu::Status g = gpu::status();
    status->available = g.available ? 1 : 0;
    copyString(status->adapter, sizeof status->adapter, g.adapter);
    copyString(status->reason, sizeof status->reason, g.reason);
}

UTAGOE_API int32_t UTAGOE_CALL utagoe_check_spans(const char* text, char* errorBuf, int32_t errorLen) {
    std::vector<std::pair<double, double>> spans;
    std::string err;
    if (parseTimeSpans(text ? text : "", spans, err)) return 0;
    setError(errorBuf, errorLen, err);
    return 1;
}

UTAGOE_API void UTAGOE_CALL utagoe_set_log(UtagoeLog fn, void* user) {
    log::setCallback(fn, user);
}

UTAGOE_API void UTAGOE_CALL utagoe_clear_step_cache(void) {
    stepcache::clear();
}

UTAGOE_API void UTAGOE_CALL utagoe_set_log_dir(const char* dir) {
    log::setCrashDirectory(dir ? dir : "");
}

UTAGOE_API const char* UTAGOE_CALL utagoe_build_info(void) {
    static const std::string info = [] {
        char buf[1024];
        std::snprintf(buf, sizeof buf,
                      "compiler   GCC %s, %d threads\n"
                      "libFLAC    %s\n"
                      "libvorbis  %s\n"
                      "libopus    %s\n"
                      "libopusenc %s\n"
                      "libogg     1.3.6, opusfile 0.12\n"
                      "dr_wav     %s, dr_mp3 %s\n"
                      "AAC/ALAC/WMA via Windows Media Foundation",
                      __VERSION__, workerCount(), FLAC__VERSION_STRING, vorbis_version_string(),
                      opus_get_version_string(), ope_get_version_string(), DRWAV_VERSION_STRING, DRMP3_VERSION_STRING);
        return std::string(buf);
    }();
    return info.c_str();
}

UTAGOE_API const char* UTAGOE_CALL utagoe_version(void) {
    return "utagoe-core 0.6";
}

}
