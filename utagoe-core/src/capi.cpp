// C++ コアを C ABI で包む薄いラッパー。

#include "utagoe_c.h"
#include "gpu.h"
#include "log.h"
#include "parallel.h"
#include "algorithms/upmix/upmix.h"
#include "fileio.h"

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

    s.waveModel = (c.waveModel >= 0 && c.waveModel <= 13) ? static_cast<WaveModel>(c.waveModel) : WaveModel::V3;
    s.waveAlign = (c.waveAlign >= 0 && c.waveAlign <= 2) ? static_cast<WaveAlign>(c.waveAlign) : WaveAlign::V3Block;
    s.fitSpans.assign(c.fitSpans, strnlen(c.fitSpans, UTAGOE_SPANS_MAX));
    s.outputKind = (c.outputKind >= 0 && c.outputKind <= 4) ? static_cast<OutputKind>(c.outputKind) : OutputKind::Vocal;
    s.centerMethod = (c.centerMethod >= 0 && c.centerMethod <= 6) ? c.centerMethod : 1;
    s.outputFolder.assign(c.outputFolder, strnlen(c.outputFolder, UTAGOE_PATH_MAX));
    s.appIcon.assign(c.appIcon, strnlen(c.appIcon, UTAGOE_NAME_MAX));
    s.uiLanguage.assign(c.uiLanguage, strnlen(c.uiLanguage, UTAGOE_NAME_MAX));
    s.repeatGuide = (c.repeatGuide >= 0 && c.repeatGuide <= 2) ? c.repeatGuide : 0;
    s.repeatBreadth = (c.repeatBreadth >= 0 && c.repeatBreadth <= 1) ? c.repeatBreadth : 0;
    s.overwriteOutput = c.overwriteOutput != 0;
    s.matchBandwidth = c.matchBandwidth != 0;
    s.upmixMethod = (c.upmixMethod >= 0 && c.upmixMethod <= 9) ? c.upmixMethod : 3;
    s.upmixSevenOne = c.upmixSevenOne != 0;
    s.upmixLfe = c.upmixLfe != 0;
    s.normalizeOutput = c.normalizeOutput != 0;
    s.matchLowEnd = c.matchLowEnd != 0;
    s.removeSubsonic = c.removeSubsonic != 0;
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
    c.centerMethod = s.centerMethod;
    c.repeatGuide = s.repeatGuide;
    c.repeatBreadth = s.repeatBreadth;
    c.overwriteOutput = s.overwriteOutput ? 1 : 0;
    c.matchBandwidth = s.matchBandwidth ? 1 : 0;
    c.upmixMethod = s.upmixMethod;
    c.upmixSevenOne = s.upmixSevenOne ? 1 : 0;
    c.upmixLfe = s.upmixLfe ? 1 : 0;
    c.normalizeOutput = s.normalizeOutput ? 1 : 0;
    c.matchLowEnd = s.matchLowEnd ? 1 : 0;
    c.removeSubsonic = s.removeSubsonic ? 1 : 0;
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
    case WaveModel::Hammerstein: return "Waveform / Hammerstein";
    case WaveModel::Nmf: return "Waveform / NMF";
    case WaveModel::Spatial: return "Waveform / Spatial";
    case WaveModel::Ensemble: return "Waveform / Ensemble Small";
    case WaveModel::Rational: return "Waveform / Rational";
    case WaveModel::Surface: return "Waveform / Surface";
    case WaveModel::Trend: return "Waveform / Trend";
    case WaveModel::Ctf: return "Waveform / CTF";
    case WaveModel::LowRank: return "Waveform / Low-rank";
    case WaveModel::EnsembleLarge: return "Waveform / Ensemble Large";
    case WaveModel::Auto: return "Waveform / Auto";
    default: return "Waveform";
    }
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

    log::line("original     %s", originalPath);
    const bool split = s.outputKind == OutputKind::CenterSides;
    const bool repeats = s.outputKind == OutputKind::Repeats;
    const bool upmixKind = s.outputKind == OutputKind::Upmix;
    const bool upmixRef = upmixKind && upmix::needsReference(static_cast<upmix::Method>(std::clamp(s.upmixMethod, 0, upmix::kMethods - 1)));
    if ((!split && !repeats && !upmixKind) || upmixRef) log::line("instrumental %s", instrumentalPath);
    if (s.outputKind == OutputKind::Vocal) {
        int m = s.saveMask & (kSaveDefault | kSaveAligned | kSaveRaw | kSaveMembers);
        const bool members = s.mergeMode == MergeMode::ByWaveform &&
                             (s.waveModel == WaveModel::Ensemble || s.waveModel == WaveModel::EnsembleLarge || s.waveModel == WaveModel::Auto);
        if (!members) m &= ~kSaveMembers;
        s.saveMask = m ? m : kSaveDefault;
        if (s.saveMask == kSaveAligned) s.outputKind = OutputKind::AlignedPair;
    }
    const bool pair = s.outputKind == OutputKind::AlignedPair;
    std::string mainOut, instOut;
    if (upmixKind) {
        outputPaths(outputPath, s.outputKind, mainOut, instOut);
        log::line("output       %s", mainOut.c_str());
    } else if (pair || split || repeats) {
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
        if (!decodeAudio(originalPath, orig, &origInfo, err)) {
            st.fail();
            setError(errorBuf, errorLen, "Original: " + err);
            log::error("cannot read the original: " + err, "");
            return 2;
        }
        log::detail("original: %s", describe(origInfo, orig).c_str());
    }
    if (split) {
        const OutputFormat format = formatFromExtension(outputPath, s.outputFormat);
        const OutputDepth depth = resolveDepth(s.outputDepth, format, origInfo, origInfo);
        ProgressBridge bridge{progress, progressUser};
        ExtractResult r = extractCenterSides(orig, s, progress ? &progressTrampoline : nullptr, progress ? &bridge : nullptr);
        if (r.cancelled) { setError(errorBuf, errorLen, "cancelled"); log::warn("cancelled"); return 4; }
        if (!r.error.empty()) { setError(errorBuf, errorLen, r.error); log::error(r.error, ""); return 5; }
        if (!beforeWrite(progress, progressUser)) { setError(errorBuf, errorLen, "cancelled"); log::warn("cancelled"); return 4; }
        normalise(s, format, {&r.vocal, &r.alignedInst}, "centre + sides");
        EncodeOptions enc;
        enc.format = format;
        enc.depth = depth;
        enc.bitrate = s.outputBitrate;
        const std::pair<const std::string*, const AudioBuffer*> outs[] = {{&mainOut, &r.vocal}, {&instOut, &r.alignedInst}};
        const char* what[] = {"centre", "sides"};
        for (int k = 0; k < 2; ++k) {
            log::Stage st(std::string("encode ") + what[k] + " " + extensionOf(format));
            if (!encodeAudio(*outs[k].first, *outs[k].second, enc, err)) {
                st.fail();
                setError(errorBuf, errorLen, "Output: " + err);
                log::error("cannot write " + *outs[k].first + ": " + err, "");
                return 6;
            }
        }
        if (result) {
            std::memset(result, 0, sizeof *result);
            copyString(result->debug, sizeof result->debug, r.alignment.debugString());
            result->outputFormat   = static_cast<int32_t>(format);
            result->outputDepth    = static_cast<int32_t>(depth);
            result->outputRate     = r.vocal.sampleRate;
            result->outputChannels = r.vocal.channels;
            result->instrumentalRateIn     = r.vocal.sampleRate;
            result->instrumentalChannelsIn = r.vocal.channels;
        }
        return 0;
    }

    if (upmixKind) {
        AudioBuffer inst;
        AudioInfo instInfo;
        if (upmixRef) {
            log::Stage st("decode instrumental");
            if (!decodeAudio(instrumentalPath, inst, &instInfo, err)) {
                st.fail();
                setError(errorBuf, errorLen, "Instrumental: " + err);
                log::error("cannot read the instrumental: " + err, "");
                return 3;
            }
            log::detail("instrumental: %s", describe(instInfo, inst).c_str());
        }
        ProgressBridge bridge{progress, progressUser};
        ExtractResult r = extractUpmix(orig, upmixRef ? &inst : nullptr, s, progress ? &progressTrampoline : nullptr, progress ? &bridge : nullptr,
                                       &origInfo, upmixRef ? &instInfo : nullptr);
        if (r.cancelled) { setError(errorBuf, errorLen, "cancelled"); log::warn("cancelled"); return 4; }
        if (!r.error.empty()) { setError(errorBuf, errorLen, r.error); log::error(r.error, ""); return 5; }
        const OutputFormat format = formatFromExtension(mainOut, OutputFormat::Wav);
        const OutputDepth depth = resolveDepth(s.outputDepth, format, origInfo, origInfo);
        if (!beforeWrite(progress, progressUser)) { setError(errorBuf, errorLen, "cancelled"); log::warn("cancelled"); return 4; }
        normalise(s, format, {&r.vocal}, "upmix");
        EncodeOptions enc;
        enc.format = format;
        enc.depth = depth;
        enc.bitrate = s.outputBitrate;
        {
            log::Stage st(std::string("encode ") + std::to_string(r.vocal.channels) + "-channel upmix " + extensionOf(format));
            if (!encodeAudio(mainOut, r.vocal, enc, err)) {
                st.fail();
                setError(errorBuf, errorLen, "Output: " + err);
                log::error("cannot write " + mainOut + ": " + err, "");
                return 6;
            }
        }
        if (result) {
            std::memset(result, 0, sizeof *result);
            copyString(result->debug, sizeof result->debug, r.alignment.debugString());
            result->outputFormat   = static_cast<int32_t>(format);
            result->outputDepth    = static_cast<int32_t>(depth);
            result->outputRate     = r.vocal.sampleRate;
            result->outputChannels = r.vocal.channels;
            result->instrumentalRateIn     = r.vocal.sampleRate;
            result->instrumentalChannelsIn = r.vocal.channels;
        }
        return 0;
    }

    if (repeats) {
        const OutputFormat format = formatFromExtension(outputPath, s.outputFormat);
        const OutputDepth depth = resolveDepth(s.outputDepth, format, origInfo, origInfo);
        const std::string ext = extensionOf(format);
        const std::wstring wdir = longPath(widenUtf8(mainOut));
        if (!beforeWrite(progress, progressUser)) { setError(errorBuf, errorLen, "cancelled"); log::warn("cancelled"); return 4; }
        if (!CreateDirectoryW(wdir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
            setError(errorBuf, errorLen, "Output: cannot create the folder " + mainOut);
            log::error("cannot create " + mainOut, "");
            return 6;
        }
        {
            WIN32_FIND_DATAW fd;
            const HANDLE h = FindFirstFileW((wdir + L"\\*").c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    std::wstring name = fd.cFileName;
                    const std::size_t dot = name.find_last_of(L'.');
                    const std::wstring stem = dot == std::wstring::npos ? name : name.substr(0, dot);
                    auto ends = [&](const std::wstring& t) { return stem.size() >= t.size() && stem.compare(stem.size() - t.size(), t.size(), t) == 0; };
                    const bool ours = stem.size() > 3 && iswdigit(stem[0]) && iswdigit(stem[1]) && stem[2] == L'_' && (ends(L"_shared") || ends(L"_difference"));
                    if (ours) DeleteFileW((wdir + L"\\" + name).c_str());
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
        }
        EncodeOptions enc;
        enc.format = format;
        enc.depth = depth;
        enc.bitrate = s.outputBitrate;
        std::string writeError;
        auto sink = [&](RepeatClip& c) {
            normalise(s, format, {&c.shared, &c.difference}, c.name.c_str());
            const std::pair<const char*, const AudioBuffer*> outs[] = {{"_shared", &c.shared}, {"_difference", &c.difference}};
            for (const auto& [suffix, buf] : outs) {
                const std::string path = mainOut + "\\" + c.name + suffix + ext;
                if (!encodeAudio(path, *buf, enc, err)) {
                    writeError = "cannot write " + path + ": " + err;
                    return false;
                }
            }
            log::detail("  %s  %.2f dB", c.name.c_str(), c.reductionDb);
            return true;
        };
        ProgressBridge bridge{progress, progressUser};
        RepeatsResult r = extractRepeats(orig, s, sink, progress ? &progressTrampoline : nullptr, progress ? &bridge : nullptr);
        if (!writeError.empty()) { setError(errorBuf, errorLen, "Output: " + writeError); log::error(writeError, ""); return 6; }
        if (r.cancelled) { setError(errorBuf, errorLen, "cancelled"); log::warn("cancelled"); return 4; }
        if (!r.error.empty()) { setError(errorBuf, errorLen, r.error); log::error(r.error, ""); return 5; }
        std::FILE* f = openFile(instOut, "wb");
        if (!f) {
            setError(errorBuf, errorLen, "Output: cannot write " + instOut);
            log::error("cannot write " + instOut, "");
            return 6;
        }
        std::fprintf(f, "file,status,quality,later_start_s,earlier_start_s,duration_s,held_out_reduction_db,correlation,guide,delay_ms,rate_ppm\r\n");
        int passed = 0;
        for (const RepeatClip& c : r.clips) {
            passed += c.passed;
            char earlier[32] = "";
            if (!c.joint) std::snprintf(earlier, sizeof earlier, "%.3f", c.source);
            std::fprintf(f, "%s,%s,%s,%.3f,%s,%.3f,%.3f,%.4f,%s,%.3f,%.2f\r\n", c.passed ? c.name.c_str() : "", c.status.c_str(), c.quality.c_str(), c.target,
                         earlier, c.duration, c.reductionDb, c.correlation, c.guide.c_str(), c.delayMs, c.ratePpm);
        }
        std::fclose(f);
        if (result) {
            std::memset(result, 0, sizeof *result);
            char buf[160];
            std::snprintf(buf, sizeof buf, "repeats: %d checked, %d passed", static_cast<int>(r.clips.size()), passed);
            copyString(result->debug, sizeof result->debug, buf);
            result->outputFormat   = static_cast<int32_t>(format);
            result->outputDepth    = static_cast<int32_t>(depth);
            result->outputRate     = orig.sampleRate;
            result->outputChannels = std::min(orig.channels, 2);
            result->instrumentalRateIn     = result->outputRate;
            result->instrumentalChannelsIn = result->outputChannels;
        }
        return 0;
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
        if (!decodeAudio(instrumentalPath, inst, &instInfo, err)) {
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
        log::warn("no ensemble members to save: only Ensemble Small, Ensemble Large and Auto have them");
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

UTAGOE_API void UTAGOE_CALL utagoe_output_paths(const char* outputPath, int32_t kind, char* firstBuf, char* secondBuf, int32_t len) {
    if (!outputPath || len <= 0) return;
    std::string a, b;
    outputPaths(outputPath, kind == 2 ? OutputKind::CenterSides : kind == 3 ? OutputKind::Repeats : kind == 4 ? OutputKind::Upmix : OutputKind::AlignedPair, a, b);
    if (firstBuf) copyString(firstBuf, static_cast<std::size_t>(len), a);
    if (secondBuf) copyString(secondBuf, static_cast<std::size_t>(len), b);
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
