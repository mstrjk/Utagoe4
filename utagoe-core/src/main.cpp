// GUI なしでコアを動かして検証するための CLI。
// C# フロントエンド本体はこの CLI ではなく utagoe_c.h の DLL API を使う。

#include "utagoe.h"
#include "log.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#  include <shellapi.h>
#endif

using namespace utagoe;

namespace {

bool cliProgress(float f, void*) {
    std::printf("\r  %5.1f%%", f * 100.0f);
    std::fflush(stdout);
    return true;
}

// --verbose: core の記録をそのまま表示する。進捗行は同じ行に上書きする。
bool g_statusShown = false;
void cliLog(int32_t kind, int32_t id, const char* text, void*) {
    if (kind == log::Status) {
        if (*text) std::printf("\r%-110.110s", text);
        else if (g_statusShown) std::printf("\r%110s\r", "");
        g_statusShown = *text != 0;
        std::fflush(stdout);
        return;
    }
    if (g_statusShown) {
        std::printf("\r%110s\r", "");
        g_statusShown = false;
    }
    switch (kind) {
    case log::StageBegin: std::printf("> %s\n", text); break;
    case log::StageEnd:
        std::printf(id < 0 ? "x %s (failed after %.2f s)\n" : "  done: %s (%.2f s)\n", text, (id < 0 ? -1 - id : id) / 1000.0);
        break;
    case log::Warning: std::printf("! %s\n", text); break;
    case log::Error: std::printf("ERROR %s\n", text); break;
    default: std::printf("%s\n", text); break;
    }
    std::fflush(stdout);
}

void usage() {
    std::printf(
        "utagoe - extract a vocal by subtracting a known instrumental\n"
        "\n"
        "usage: utagoe <original> <instrumental> <output> [options]\n"
        "\n"
        "Inputs may be WAV, AIFF, FLAC, MP3, Ogg Vorbis, Opus, and (on Windows) AAC/M4A,\n"
        "ALAC or WMA, at any bit depth. They may differ in format, sample rate and channels.\n"
        "The output format follows the output file's extension.\n"
        "\n"
        "  --ini <path>        load settings from a Utagoe .ini\n"
        "  --method freq|wave  extraction method (default: freq)\n"
        "  --level <0-20>      extractable level (default: 6)\n"
        "  --block <ms>        block length: 50/100/200/400/800 (default: 100)\n"
        "  --phase auto|pos|inv\n"
        "  --intro auto|normal|detailed|none\n"
        "  --depth auto|16|24|32f   output bit depth for lossless formats (default: auto)\n"
        "  --bitrate <kbps>    bitrate for lossy formats (default: 320)\n"
        "  --alac              write .m4a as ALAC instead of AAC\n"
        "  --pair              write the original and the time-aligned instrumental\n"
        "                      (<output>_main / <output>_inst) instead of the vocals\n"
        "  --quiet             no progress output\n");
}

std::string describe(const AudioInfo& i) {
    char buf[160];
    if (i.lossless)
        std::snprintf(buf, sizeof buf, "%s  %.3f kHz  %d-bit%s  %d ch", i.codec.c_str(),
                      i.sampleRate / 1000.0, i.bits, i.floatingPoint ? " float" : "", i.channels);
    else
        std::snprintf(buf, sizeof buf, "%s  %.3f kHz  %d kbps  %d ch", i.codec.c_str(),
                      i.sampleRate / 1000.0, i.bitrateKbps, i.channels);
    return buf;
}

// Windows の argv は ANSI code page なので、日本語 path のために UTF-16 から UTF-8 へ取り直す。
std::vector<std::string> utf8Args(int argc, char** argv) {
    std::vector<std::string> out;
#ifdef _WIN32
    int n = 0;
    LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n);
    if (w) {
        for (int i = 0; i < n; ++i) {
            const int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, nullptr, 0, nullptr, nullptr);
            std::string s(static_cast<std::size_t>(len > 0 ? len - 1 : 0), '\0');
            if (len > 1) WideCharToMultiByte(CP_UTF8, 0, w[i], -1, &s[0], len, nullptr, nullptr);
            out.push_back(s);
        }
        LocalFree(w);
        return out;
    }
#endif
    for (int i = 0; i < argc; ++i) out.emplace_back(argv[i]);
    return out;
}

}

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    const std::vector<std::string> args = utf8Args(argc, argv);
    if (args.size() < 4) { usage(); return args.size() == 1 ? 0 : 1; }

    const std::string& origPath = args[1];
    const std::string& instPath = args[2];
    const std::string& outPath  = args[3];

    Settings s;
    bool quiet = false;

    for (std::size_t i = 4; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto next = [&](const char* what) -> std::string {
            if (i + 1 >= args.size()) {
                std::fprintf(stderr, "error: %s needs a value\n", what);
                std::exit(1);
            }
            return args[++i];
        };

        if (a == "--ini") {
            const std::string p = next("--ini");
            if (!s.load(p)) {
                std::fprintf(stderr, "error: cannot read %s\n", p.c_str());
                return 1;
            }
        } else if (a == "--method") {
            s.mergeMode = next("--method") == "wave" ? MergeMode::ByWaveform : MergeMode::ByFrequency;
        } else if (a == "--level") {
            s.extractLevel = std::stoi(next("--level"));
        } else if (a == "--block") {
            s.blockSizeMs = std::stoi(next("--block"));
        } else if (a == "--phase") {
            const std::string v = next("--phase");
            s.krkPhase = v == "pos" ? KrkPhase::Positive : v == "inv" ? KrkPhase::Inverted : KrkPhase::Automatic;
        } else if (a == "--intro") {
            const std::string v = next("--intro");
            s.introMode = v == "normal" ? IntroMode::Normal : v == "detailed" ? IntroMode::Detailed
                        : v == "none" ? IntroMode::None : IntroMode::Automatic;
        } else if (a == "--depth") {
            const std::string v = next("--depth");
            s.outputDepth = v == "16" ? OutputDepth::Int16 : v == "24" ? OutputDepth::Int24
                          : v == "32f" ? OutputDepth::Float32 : OutputDepth::Auto;
        } else if (a == "--bitrate") {
            s.outputBitrate = std::stoi(next("--bitrate"));
        } else if (a == "--alac") {
            s.outputFormat = OutputFormat::Alac;
        } else if (a == "--pair") {
            s.outputKind = OutputKind::AlignedPair;
        } else if (a == "--quiet") {
            quiet = true;
        } else if (a == "--verbose") {
            log::setCallback(&cliLog, nullptr);
            quiet = true;
        } else {
            std::fprintf(stderr, "error: unknown option %s\n", a.c_str());
            return 1;
        }
    }
    AudioBuffer orig, inst;
    AudioInfo origInfo, instInfo;
    std::string err;

    if (!decodeAudio(origPath, orig, &origInfo, err)) {
        std::fprintf(stderr, "error reading original: %s\n", err.c_str());
        return 1;
    }
    // 元実装に合わせて、両方に同じファイルを指定したら後処理だけを掛ける。
    const bool same = sameFile(origPath, instPath);
    if (same && s.outputKind == OutputKind::AlignedPair) {
        std::fprintf(stderr, "error: the aligned pair needs two different files\n");
        return 1;
    }
    if (same) {
        instInfo = origInfo;
    } else if (!decodeAudio(instPath, inst, &instInfo, err)) {
        std::fprintf(stderr, "error reading instrumental: %s\n", err.c_str());
        return 1;
    }

    const OutputFormat format = formatFromExtension(outPath, s.outputFormat);
    const OutputDepth depth = resolveDepth(s.outputDepth, format, origInfo, instInfo);
    auto is16 = [](const AudioInfo& i) { return i.lossless && !i.floatingPoint && i.bits > 0 && i.bits <= 16; };
    const bool quantize16 = depth == OutputDepth::Int16 && is16(origInfo) && is16(instInfo);

    if (!quiet) {
        std::printf("original     %s\n  %s\n", origPath.c_str(), describe(origInfo).c_str());
        std::printf("instrumental %s\n  %s\n", instPath.c_str(), describe(instInfo).c_str());
        std::printf("extracting...\n");
    }

    ExtractResult r = same ? extractFiltersOnly(orig, s, quantize16, quiet ? nullptr : &cliProgress, nullptr)
                           : extract(orig, inst, s, quantize16, quiet ? nullptr : &cliProgress, nullptr);
    if (!quiet) std::printf("\r        \r");

    if (!r) {
        std::fprintf(stderr, "error: %s\n", r.cancelled ? "cancelled" : r.error.c_str());
        return 1;
    }

    EncodeOptions enc;
    enc.format = format;
    enc.depth = depth;
    enc.bitrate = s.outputBitrate;
    std::vector<std::pair<std::string, const AudioBuffer*>> targets;
    if (s.outputKind == OutputKind::AlignedPair) {
        std::string m, i;
        alignedPairPaths(outPath, m, i);
        targets = {{m, &r.vocal}, {i, &r.alignedInst}};
    } else {
        targets = {{outPath, &r.vocal}};
    }
    for (const auto& t : targets) {
        if (!encodeAudio(t.first, *t.second, enc, err)) {
            std::fprintf(stderr, "error writing %s: %s\n", t.first.c_str(), err.c_str());
            return 1;
        }
    }

    if (!quiet) {
        if (r.instrumentalRateIn != r.vocal.sampleRate || r.instrumentalChannelsIn != r.vocal.channels)
            std::printf("instrumental converted: %d Hz %d ch -> %d Hz %d ch\n", r.instrumentalRateIn,
                        r.instrumentalChannelsIn, r.vocal.sampleRate, r.vocal.channels);
        std::printf("alignment:%s\n", r.alignment.debugString().c_str());
        for (const auto& t : targets) std::printf("wrote %s\n", t.first.c_str());
    }
    return 0;
}
