// Windows Media Foundation 経由の decode / encode。
// AAC/M4A, ALAC, WMA など同梱ライブラリにない形式を OS 標準の codec で扱う。
// Windows N / KN edition は Media Feature Pack がないと MF 自体が使えないので、その場合は分かるエラーを返す。

#include "codec_internal.h"

#ifdef _WIN32

#include <initguid.h>
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#include <algorithm>
#include <type_traits>
#include <cmath>
#include <cstring>

namespace utagoe {
namespace {

// MF の DLL は実行時に読み込む。Windows N edition では存在しないため、静的 link すると
// utagoe_core.dll 自体が読み込めず、MF を使わない WAV / FLAC / MP3 まで使えなくなる。
struct MfApi {
    decltype(&::MFStartup) Startup = nullptr;
    decltype(&::MFShutdown) Shutdown = nullptr;
    decltype(&::MFCreateMediaType) CreateMediaType = nullptr;
    decltype(&::MFCreateAttributes) CreateAttributes = nullptr;
    decltype(&::MFCreateMemoryBuffer) CreateMemoryBuffer = nullptr;
    decltype(&::MFCreateSample) CreateSample = nullptr;
    decltype(&::MFCreateSourceReaderFromURL) CreateSourceReaderFromURL = nullptr;
    decltype(&::MFCreateSinkWriterFromURL) CreateSinkWriterFromURL = nullptr;
    decltype(&::MFTranscodeGetAudioOutputAvailableTypes) TranscodeGetAudioOutputAvailableTypes = nullptr;
    bool ok = false;

    MfApi() {
        HMODULE plat = LoadLibraryExW(L"mfplat.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        HMODULE rw = LoadLibraryExW(L"mfreadwrite.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        HMODULE mf = LoadLibraryExW(L"mf.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!plat || !rw || !mf) return;
        auto get = [](HMODULE m, const char* name, auto& fn) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(
                reinterpret_cast<void*>(GetProcAddress(m, name)));
            return fn != nullptr;
        };
        ok = get(plat, "MFStartup", Startup) && get(plat, "MFShutdown", Shutdown) &&
             get(plat, "MFCreateMediaType", CreateMediaType) && get(plat, "MFCreateAttributes", CreateAttributes) &&
             get(plat, "MFCreateMemoryBuffer", CreateMemoryBuffer) && get(plat, "MFCreateSample", CreateSample) &&
             get(rw, "MFCreateSourceReaderFromURL", CreateSourceReaderFromURL) &&
             get(rw, "MFCreateSinkWriterFromURL", CreateSinkWriterFromURL) &&
             get(mf, "MFTranscodeGetAudioOutputAvailableTypes", TranscodeGetAudioOutputAvailableTypes);
    }
};

const MfApi& api() {
    static const MfApi instance;
    return instance;
}

// COM / MF の初期化を呼び出し元 thread で行い、終了時に対で戻す。
struct MfSession {
    HRESULT co = E_FAIL;
    HRESULT mf = E_FAIL;
    MfSession() {
        if (!api().ok) return;
        co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        mf = api().Startup(MF_VERSION, MFSTARTUP_LITE);
    }
    ~MfSession() {
        if (SUCCEEDED(mf)) api().Shutdown();
        if (SUCCEEDED(co)) CoUninitialize();
    }
    bool ok() const { return SUCCEEDED(mf); }
};

template <class T>
struct ComPtr {
    T* p = nullptr;
    ~ComPtr() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
    T* get() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

std::string hr(const char* what, HRESULT h) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s (0x%08lX)", what, static_cast<unsigned long>(h));
    return buf;
}

const char* noMfMessage() {
    return "Windows Media Foundation is not available on this system "
           "(Windows N editions need the Media Feature Pack).";
}


void describeSubtype(const GUID& sub, AudioInfo& info) {
    struct Name { const GUID* g; const char* name; bool lossless; };
    static const Name names[] = {
        {&MFAudioFormat_AAC, "AAC", false},        {&MFAudioFormat_ALAC, "ALAC", true},
        {&MFAudioFormat_WMAudioV8, "WMA", false},  {&MFAudioFormat_WMAudioV9, "WMA Pro", false},
        {&MFAudioFormat_WMAudio_Lossless, "WMA Lossless", true},
        {&MFAudioFormat_MP3, "MP3", false},        {&MFAudioFormat_MPEG, "MPEG Audio", false},
        {&MFAudioFormat_FLAC, "FLAC", true},       {&MFAudioFormat_Opus, "Opus", false},
        {&MFAudioFormat_Dolby_AC3, "AC-3", false}, {&MFAudioFormat_PCM, "PCM", true},
        {&MFAudioFormat_Float, "PCM Float", true},
    };
    for (const auto& n : names) {
        if (IsEqualGUID(sub, *n.g)) {
            info.codec = n.name;
            info.lossless = n.lossless;
            return;
        }
    }
    info.codec = "Audio";
    info.lossless = false;
}

}

bool mfDecode(const std::string& path, AudioBuffer* out, AudioInfo& info, std::string& error) {
    MfSession session;
    if (!session.ok()) { error = noMfMessage(); return false; }

    ComPtr<IMFSourceReader> reader;
    const std::wstring wpath = widenPath(path);
    HRESULT h = api().CreateSourceReaderFromURL(wpath.c_str(), nullptr, &reader);
    if (FAILED(h)) { error = "unsupported or unreadable audio file"; return false; }

    const DWORD stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
    reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    if (FAILED(reader->SetStreamSelection(stream, TRUE))) {
        error = "the file contains no audio";
        return false;
    }

    // 元の形式情報は native type から読む。
    {
        ComPtr<IMFMediaType> native;
        if (SUCCEEDED(reader->GetNativeMediaType(stream, 0, &native))) {
            GUID sub{};
            native->GetGUID(MF_MT_SUBTYPE, &sub);
            describeSubtype(sub, info);
            UINT32 v = 0;
            if (info.lossless && SUCCEEDED(native->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &v))) info.bits = static_cast<int>(v);
            if (!info.lossless && SUCCEEDED(native->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &v)))
                info.bitrateKbps = static_cast<int>(v * 8 / 1000);
        }
    }

    // 出力は float PCM に固定し、decoder の選択と変換は MF に任せる。
    ComPtr<IMFMediaType> want;
    api().CreateMediaType(&want);
    want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    want->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
    h = reader->SetCurrentMediaType(stream, nullptr, want.get());
    if (FAILED(h)) {
        error = "no decoder is installed for this audio format";
        return false;
    }

    ComPtr<IMFMediaType> cur;
    reader->GetCurrentMediaType(stream, &cur);
    UINT32 ch = 0, rate = 0;
    cur->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch);
    cur->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
    info.channels = static_cast<int>(ch);
    info.sampleRate = static_cast<int>(rate);

    PROPVARIANT var;
    PropVariantInit(&var);
    if (SUCCEEDED(reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE),
                                                   MF_PD_DURATION, &var))) {
        info.frames = static_cast<int64_t>(var.uhVal.QuadPart * rate / 10000000ULL);
    }
    PropVariantClear(&var);

    if (ch == 0 || rate == 0) { error = "unsupported audio stream"; return false; }
    if (!out) return true;

    out->sampleRate = info.sampleRate;
    out->channels = info.channels;
    if (info.frames > 0) out->samples.reserve(static_cast<std::size_t>(info.frames) * ch);

    for (;;) {
        DWORD flags = 0;
        ComPtr<IMFSample> sample;
        h = reader->ReadSample(stream, 0, nullptr, &flags, nullptr, &sample);
        if (FAILED(h)) { error = hr("audio decode failed", h); return false; }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (!sample) continue;

        ComPtr<IMFMediaBuffer> buf;
        if (FAILED(sample->ConvertToContiguousBuffer(&buf))) continue;
        BYTE* data = nullptr;
        DWORD len = 0;
        if (SUCCEEDED(buf->Lock(&data, nullptr, &len))) {
            const float* f = reinterpret_cast<const float*>(data);
            out->samples.insert(out->samples.end(), f, f + len / sizeof(float));
            buf->Unlock();
        }
    }
    return true;
}


namespace {

struct Target {
    GUID subtype;
    GUID container;
    bool lossless;
};

bool targetFor(OutputFormat f, Target& t) {
    switch (f) {
        case OutputFormat::Mp3:  t = {MFAudioFormat_MP3,  MFTranscodeContainerType_MP3,   false}; return true;
        case OutputFormat::Aac:  t = {MFAudioFormat_AAC,  MFTranscodeContainerType_MPEG4, false}; return true;
        case OutputFormat::Alac: t = {MFAudioFormat_ALAC, MFTranscodeContainerType_MPEG4, true};  return true;
        case OutputFormat::Wma:  t = {MFAudioFormat_WMAudioV8, MFTranscodeContainerType_ASF, false}; return true;
        default: return false;
    }
}

const char* nameOf(OutputFormat f) {
    switch (f) {
        case OutputFormat::Mp3: return "MP3";
        case OutputFormat::Aac: return "AAC";
        case OutputFormat::Alac: return "ALAC";
        case OutputFormat::Wma: return "WMA";
        default: return "audio";
    }
}

// encoder が出せる形式の一覧から、rate / channel / bitrate が最も近いものを選ぶ。
// rate が一致するものを最優先し、なければ 48 kHz -> 44.1 kHz の順で妥協する。
bool pickLossyType(const GUID& subtype, int rate, int channels, int kbps,
                   ComPtr<IMFMediaType>& chosen, int& chosenRate, std::string& error) {
    ComPtr<IMFCollection> types;
    HRESULT h = api().TranscodeGetAudioOutputAvailableTypes(subtype, MFT_ENUM_FLAG_ALL, nullptr, &types);
    DWORD count = 0;
    if (FAILED(h) || !types || FAILED(types->GetElementCount(&count)) || count == 0) {
        error = "no encoder for this format is installed";
        return false;
    }

    const int wantBytes = kbps * 1000 / 8;
    long long bestScore = -1;
    for (DWORD i = 0; i < count; ++i) {
        ComPtr<IUnknown> unk;
        if (FAILED(types->GetElement(i, &unk))) continue;
        ComPtr<IMFMediaType> mt;
        if (FAILED(unk->QueryInterface(IID_IMFMediaType, reinterpret_cast<void**>(&mt)))) continue;

        UINT32 r = 0, c = 0, bytes = 0;
        mt->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &r);
        mt->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &c);
        mt->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &bytes);
        if (static_cast<int>(c) != channels) continue;

        long long score = 0;
        if (static_cast<int>(r) == rate) score += 1LL << 40;
        else if (r == 48000) score += 1LL << 39;
        else if (r == 44100) score += 1LL << 38;
        score -= std::llabs(static_cast<long long>(bytes) - wantBytes);
        if (score > bestScore) {
            bestScore = score;
            if (chosen.p) chosen.p->Release();
            chosen.p = mt.p;
            mt.p = nullptr;
            chosenRate = static_cast<int>(r);
        }
    }
    if (!chosen) {
        error = "the encoder does not support this channel count";
        return false;
    }
    return true;
}

}

bool mfEncode(const std::string& path, const AudioBuffer& inAudio, OutputFormat format,
              int bits, int bitrateKbps, std::string& error) {
    Target t;
    if (!targetFor(format, t)) { error = "not a Media Foundation format"; return false; }

    MfSession session;
    if (!session.ok()) { error = noMfMessage(); return false; }

    // lossy は encoder が受け付ける形式から選ぶ。ALAC は入力と同じ rate / channel で作る。
    const AudioBuffer* src = &inAudio;
    AudioBuffer converted;
    ComPtr<IMFMediaType> outType;
    int rate = inAudio.sampleRate;
    const int ch = inAudio.channels;
    const int pcmBits = (t.lossless && bits >= 24) ? 24 : 16;

    if (t.lossless) {
        api().CreateMediaType(&outType);
        outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        outType->SetGUID(MF_MT_SUBTYPE, t.subtype);
        outType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, static_cast<UINT32>(rate));
        outType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, static_cast<UINT32>(ch));
        outType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, static_cast<UINT32>(pcmBits));
    } else {
        std::string why;
        if (!pickLossyType(t.subtype, rate, ch, bitrateKbps, outType, rate, why)) {
            error = std::string(nameOf(format)) + ": " + why;
            return false;
        }
        if (rate != inAudio.sampleRate) {
            converted = conformAudio(inAudio, rate, ch);
            src = &converted;
        }
    }

    ComPtr<IMFAttributes> attrs;
    api().CreateAttributes(&attrs, 1);
    attrs->SetGUID(MF_TRANSCODE_CONTAINERTYPE, t.container);

    ComPtr<IMFSinkWriter> writer;
    const std::wstring wpath = widenPath(path);
    HRESULT h = api().CreateSinkWriterFromURL(wpath.c_str(), nullptr, attrs.get(), &writer);
    if (FAILED(h)) { error = hr("cannot create the output file", h); return false; }

    DWORD streamIndex = 0;
    h = writer->AddStream(outType.get(), &streamIndex);
    if (FAILED(h)) {
        error = std::string(nameOf(format)) + ": " + hr("no encoder for this format is installed", h);
        return false;
    }

    const int blockAlign = ch * pcmBits / 8;
    ComPtr<IMFMediaType> inType;
    api().CreateMediaType(&inType);
    inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    inType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    inType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, static_cast<UINT32>(ch));
    inType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, static_cast<UINT32>(rate));
    inType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, static_cast<UINT32>(pcmBits));
    inType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, static_cast<UINT32>(blockAlign));
    inType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, static_cast<UINT32>(blockAlign * rate));
    inType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    h = writer->SetInputMediaType(streamIndex, inType.get(), nullptr);
    if (FAILED(h)) { error = hr("the encoder rejected the input format", h); return false; }

    h = writer->BeginWriting();
    if (FAILED(h)) { error = hr("cannot start encoding", h); return false; }

    const std::size_t frames = src->frames();
    const std::size_t chunk = 8192;
    const double fullScale = pcmBits == 24 ? 8388608.0 : 32768.0;
    const double maxVal = fullScale - 1.0;

    for (std::size_t pos = 0; pos < frames; pos += chunk) {
        const std::size_t take = std::min(chunk, frames - pos);
        const DWORD bytes = static_cast<DWORD>(take * blockAlign);

        ComPtr<IMFMediaBuffer> buf;
        api().CreateMemoryBuffer(bytes, &buf);
        BYTE* data = nullptr;
        buf->Lock(&data, nullptr, nullptr);
        for (std::size_t i = 0; i < take * ch; ++i) {
            double v = std::nearbyint(static_cast<double>(src->samples[pos * ch + i]) * fullScale);
            v = std::min(maxVal, std::max(-fullScale, v));
            const int32_t s = static_cast<int32_t>(v);
            if (pcmBits == 16) {
                reinterpret_cast<int16_t*>(data)[i] = static_cast<int16_t>(s);
            } else {
                data[i * 3] = static_cast<BYTE>(s);
                data[i * 3 + 1] = static_cast<BYTE>(s >> 8);
                data[i * 3 + 2] = static_cast<BYTE>(s >> 16);
            }
        }
        buf->Unlock();
        buf->SetCurrentLength(bytes);

        ComPtr<IMFSample> sample;
        api().CreateSample(&sample);
        sample->AddBuffer(buf.get());
        sample->SetSampleTime(static_cast<LONGLONG>(pos * 10000000ULL / rate));
        sample->SetSampleDuration(static_cast<LONGLONG>(take * 10000000ULL / rate));
        h = writer->WriteSample(streamIndex, sample.get());
        if (FAILED(h)) { error = hr("encoding failed", h); return false; }
    }

    h = writer->Finalize();
    if (FAILED(h)) { error = hr("could not finish the output file", h); return false; }
    return true;
}

}

#else

namespace utagoe {
bool mfDecode(const std::string&, AudioBuffer*, AudioInfo&, std::string& error) {
    error = "unsupported audio format";
    return false;
}
bool mfEncode(const std::string&, const AudioBuffer&, OutputFormat, int, int, std::string& error) {
    error = "this output format is only available on Windows";
    return false;
}
}

#endif
