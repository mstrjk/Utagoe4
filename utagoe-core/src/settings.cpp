// 元実装互換の INI 読み書き。
// schema は load/save routine から復元。キー名と型を元実装に合わせ、双方で設定ファイルを往復できるようにする。

#include "utagoe.h"
#include "fileio.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>

namespace utagoe {
namespace {

const char* kSection = "V30_Option";
// v3 にない拡張設定。元実装の section とは分けて保存する。
const char* kOutputSection = "Output";
const char* kGpuSection = "GPU";
const char* kModelSection = "WaveModel";
const char* kUiSection = "UI";

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

using Map = std::map<std::string, std::string>;

int getInt(const Map& m, const char* key, int dflt) {
    auto it = m.find(key);
    if (it == m.end()) return dflt;
    try { return std::stoi(it->second); } catch (...) { return dflt; }
}

bool getBool(const Map& m, const char* key, bool dflt) {
    auto it = m.find(key);
    if (it == m.end()) return dflt;
    // VCL の bool 保存形式に合わせて 0/1 を使う。
    return it->second != "0";
}

std::string getStr(const Map& m, const char* key, const std::string& dflt) {
    auto it = m.find(key);
    return it == m.end() ? dflt : it->second;
}

}

bool Settings::load(const std::string& path) {
    std::FILE* f = openFile(path, "rb");
    if (!f) return false;

    Map kv, out, gpu, model, ui;
    Map* target = nullptr;
    char line[1024];
    while (std::fgets(line, sizeof line, f)) {
        std::string s = trim(line);
        if (s.empty() || s[0] == ';') continue;
        if (s.front() == '[' && s.back() == ']') {
            const std::string name = s.substr(1, s.size() - 2);
            target = name == kSection ? &kv : name == kOutputSection ? &out : name == kGpuSection ? &gpu : name == kModelSection ? &model : name == kUiSection ? &ui : nullptr;
            continue;
        }
        if (!target) continue;
        const auto eq = s.find('=');
        if (eq == std::string::npos) continue;
        (*target)[trim(s.substr(0, eq))] = trim(s.substr(eq + 1));
    }
    std::fclose(f);

    procMode  = static_cast<ProcMode>(getInt(kv, "ProcMode",  static_cast<int>(procMode)));
    mergeMode = static_cast<MergeMode>(getInt(kv, "MergeMode", static_cast<int>(mergeMode)));
    introMode = static_cast<IntroMode>(getInt(kv, "IntroMode", static_cast<int>(introMode)));
    levelAdpt = static_cast<LevelAdpt>(getInt(kv, "LevelAdpt", static_cast<int>(levelAdpt)));
    adptMode  = static_cast<AdptMode>(getInt(kv, "AdptMode",  static_cast<int>(adptMode)));
    krkPhase  = static_cast<KrkPhase>(getInt(kv, "KrkPhase",  static_cast<int>(krkPhase)));
    soundQty  = static_cast<SoundQty>(getInt(kv, "SoundQty",  static_cast<int>(soundQty)));

    oversample    = getBool(kv, "OvspFlg", oversample);
    oversampleMul = getInt (kv, "OvspMx",  oversampleMul);
    blockSizeMs   = getInt (kv, "BlkSize", blockSizeMs);
    adptRange     = getInt (kv, "AdptNum", adptRange);

    centralize    = getBool(kv, "CntrFlg", centralize);
    centralizePos = getInt (kv, "CntrPos", centralizePos);

    lowPass       = getBool(kv, "LPFFlg",  lowPass);
    lowPassPos    = getInt (kv, "LPFPos",  lowPassPos);
    highPass      = getBool(kv, "HPFFlg",  highPass);
    highPassPos   = getInt (kv, "HPFPos",  highPassPos);

    extractLevel  = getInt (kv, "KvolPos", extractLevel);
    instLevel     = getInt (kv, "KlvlPos", instLevel);

    searchInstFile = getBool(kv, "KnameFlg", searchInstFile);
    autoNameOutput = getBool(kv, "VnameFlg", autoNameOutput);
    outputSuffix   = getStr (kv, "VnameTxt", outputSuffix);

    outputFormat  = static_cast<OutputFormat>(getInt(out, "Format", static_cast<int>(outputFormat)));
    outputDepth   = static_cast<OutputDepth>(getInt(out, "Depth", static_cast<int>(outputDepth)));
    outputBitrate = getInt(out, "Bitrate", outputBitrate);
    {
        const int k = getInt(out, "Kind", static_cast<int>(outputKind));
        outputKind = (k >= 0 && k <= 3) ? static_cast<OutputKind>(k) : OutputKind::Vocal;
    }
    centerMethod  = std::clamp(getInt(out, "CentreMethod", centerMethod), 0, 6);
    repeatGuide   = std::clamp(getInt(out, "RepeatGuide", repeatGuide), 0, 2);
    repeatBreadth = std::clamp(getInt(out, "RepeatBreadth", repeatBreadth), 0, 1);
    outputFolder  = getStr(out, "Folder", outputFolder);
    overwriteOutput = getBool(out, "Overwrite", overwriteOutput);

    useGpu          = getBool(gpu, "Use", useGpu);
    gpuMode         = static_cast<GpuMode>(getInt(gpu, "Mode", static_cast<int>(gpuMode)) == 1 ? 1 : 0);
    gpuNoticeHidden = getBool(gpu, "HideNotice", gpuNoticeHidden);

    const int wm = getInt(model, "Model", static_cast<int>(waveModel));
    waveModel = (wm >= 0 && wm <= 13) ? static_cast<WaveModel>(wm) : WaveModel::V3;
    const int wa = getInt(model, "Align", static_cast<int>(waveAlign));
    waveAlign = (wa >= 0 && wa <= 2) ? static_cast<WaveAlign>(wa) : WaveAlign::V3Block;
    fitSpans  = getStr(model, "FitSpans", fitSpans);
    kickDuck  = getBool(model, "KickDuck", kickDuck);

    appIcon = getStr(ui, "Icon", appIcon);
    uiLanguage = getStr(ui, "Language", uiLanguage);

    return true;
}

bool Settings::save(const std::string& path) const {
    std::FILE* f = openFile(path, "wb");
    if (!f) return false;

    std::ostringstream o;
    o << "[" << kSection << "]\r\n";
    o << "ProcMode="  << static_cast<int>(procMode)  << "\r\n";
    o << "MergeMode=" << static_cast<int>(mergeMode) << "\r\n";
    o << "OvspFlg="   << (oversample ? 1 : 0)        << "\r\n";
    o << "OvspMx="    << oversampleMul               << "\r\n";
    o << "BlkSize="   << blockSizeMs                 << "\r\n";
    o << "AdptMode="  << static_cast<int>(adptMode)  << "\r\n";
    o << "AdptNum="   << adptRange                   << "\r\n";
    o << "CntrFlg="   << (centralize ? 1 : 0)        << "\r\n";
    o << "CntrPos="   << centralizePos               << "\r\n";
    o << "IntroMode=" << static_cast<int>(introMode) << "\r\n";
    o << "LevelAdpt=" << static_cast<int>(levelAdpt) << "\r\n";
    o << "LPFFlg="    << (lowPass ? 1 : 0)           << "\r\n";
    o << "LPFPos="    << lowPassPos                  << "\r\n";
    o << "HPFFlg="    << (highPass ? 1 : 0)          << "\r\n";
    o << "HPFPos="    << highPassPos                 << "\r\n";
    o << "KvolPos="   << extractLevel                << "\r\n";
    o << "KlvlPos="   << instLevel                   << "\r\n";
    o << "KnameFlg="  << (searchInstFile ? 1 : 0)    << "\r\n";
    o << "VnameFlg="  << (autoNameOutput ? 1 : 0)    << "\r\n";
    o << "VnameTxt="  << outputSuffix                << "\r\n";
    o << "KrkPhase="  << static_cast<int>(krkPhase)  << "\r\n";
    o << "SoundQty="  << static_cast<int>(soundQty)  << "\r\n";
    o << "\r\n[" << kOutputSection << "]\r\n";
    o << "Format="    << static_cast<int>(outputFormat) << "\r\n";
    o << "Depth="     << static_cast<int>(outputDepth)  << "\r\n";
    o << "Bitrate="   << outputBitrate                  << "\r\n";
    o << "Kind="      << static_cast<int>(outputKind)   << "\r\n";
    o << "CentreMethod=" << centerMethod                << "\r\n";
    o << "RepeatGuide=" << repeatGuide                  << "\r\n";
    o << "RepeatBreadth=" << repeatBreadth              << "\r\n";
    o << "Folder="    << outputFolder                   << "\r\n";
    o << "Overwrite=" << (overwriteOutput ? 1 : 0)      << "\r\n";
    o << "\r\n[" << kGpuSection << "]\r\n";
    o << "Use="        << (useGpu ? 1 : 0)             << "\r\n";
    o << "Mode="       << static_cast<int>(gpuMode)    << "\r\n";
    o << "HideNotice=" << (gpuNoticeHidden ? 1 : 0)    << "\r\n";
    o << "\r\n[" << kModelSection << "]\r\n";
    o << "Model="      << static_cast<int>(waveModel)  << "\r\n";
    o << "Align="      << static_cast<int>(waveAlign)  << "\r\n";
    o << "FitSpans="   << fitSpans                     << "\r\n";
    o << "KickDuck="   << (kickDuck ? 1 : 0)           << "\r\n";
    o << "\r\n[" << kUiSection << "]\r\n";
    o << "Icon="       << appIcon                      << "\r\n";
    o << "Language="   << uiLanguage                   << "\r\n";

    const std::string s = o.str();
    const bool ok = std::fwrite(s.data(), 1, s.size(), f) == s.size();
    std::fclose(f);
    return ok;
}

}
