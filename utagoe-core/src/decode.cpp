// 入力音声の decode。拡張子ではなく中身の magic で形式を判定する。
// 同梱ライブラリで読める形式はそれを使い、それ以外 (AAC/M4A, ALAC, WMA など) は Media Foundation に任せる。
// 同梱側で失敗した場合も Media Foundation で再試行する。

#include "codec_internal.h"

#include "dr_wav.h"
#include "dr_mp3.h"
#include "FLAC/stream_decoder.h"
#define OV_EXCLUDE_STATIC_CALLBACKS
#include "vorbis/vorbisfile.h"
#include "opusfile.h"

#include <cstring>
#include <memory>

namespace utagoe {
namespace {

enum class Kind { Unknown, Wave, Flac, Mp3, Vorbis, Opus };

// ID3v2 tag の長さ。無ければ 0。
std::size_t id3Length(const unsigned char* b, std::size_t n) {
    if (n < 10 || std::memcmp(b, "ID3", 3) != 0) return 0;
    const std::size_t size = (std::size_t(b[6] & 0x7f) << 21) | (std::size_t(b[7] & 0x7f) << 14) |
                             (std::size_t(b[8] & 0x7f) << 7) | std::size_t(b[9] & 0x7f);
    return 10 + size + ((b[5] & 0x10) ? 10 : 0);
}

bool contains(const unsigned char* b, std::size_t n, const char* needle, std::size_t len) {
    for (std::size_t i = 0; i + len <= n; ++i)
        if (std::memcmp(b + i, needle, len) == 0) return true;
    return false;
}

Kind sniff(const std::string& path) {
    FileCloser f(openFile(path, "rb"));
    if (!f.f) return Kind::Unknown;

    unsigned char head[512];
    std::size_t n = std::fread(head, 1, sizeof head, f.f);

    // 先頭の ID3v2 は MP3 / FLAC のどちらにも付き得るので読み飛ばす。
    const std::size_t skip = id3Length(head, n);
    if (skip > 0) {
        if (std::fseek(f.f, static_cast<long>(skip), SEEK_SET) != 0) return Kind::Unknown;
        n = std::fread(head, 1, sizeof head, f.f);
    }
    if (n < 4) return Kind::Unknown;

    if (!std::memcmp(head, "RIFF", 4) || !std::memcmp(head, "RIFX", 4) || !std::memcmp(head, "RF64", 4) ||
        !std::memcmp(head, "riff", 4) || !std::memcmp(head, "FORM", 4))
        return Kind::Wave;
    if (!std::memcmp(head, "fLaC", 4)) return Kind::Flac;
    if (!std::memcmp(head, "OggS", 4)) {
        if (contains(head, n, "\x01vorbis", 7)) return Kind::Vorbis;
        if (contains(head, n, "OpusHead", 8)) return Kind::Opus;
        return Kind::Unknown;
    }
    // MPEG audio の frame sync。layer bits が 0 のものは ADTS AAC なので対象外。dr_mp3 は Layer III だけ扱う。
    if (head[0] == 0xFF && (head[1] & 0xE0) == 0xE0 && ((head[1] >> 1) & 3) == 1) return Kind::Mp3;
    return Kind::Unknown;
}

void reserveFor(AudioBuffer* out, int64_t frames, int channels) {
    if (out && frames > 0) out->samples.reserve(static_cast<std::size_t>(frames) * channels);
}


bool decodeWave(const std::string& path, AudioBuffer* out, AudioInfo& info, std::string& error) {
    drwav wav;
    if (!drwav_init_file_w(&wav, widenPath(path).c_str(), nullptr)) {
        error = "not a readable WAV/AIFF file";
        return false;
    }
    std::unique_ptr<drwav, decltype(&drwav_uninit)> guard(&wav, &drwav_uninit);

    switch (wav.container) {
        case drwav_container_w64:  info.codec = "W64";  break;
        case drwav_container_rf64: info.codec = "RF64"; break;
        case drwav_container_aiff: info.codec = "AIFF"; break;
        default:                   info.codec = "WAV";  break;
    }
    const auto tag = wav.translatedFormatTag;
    info.sampleRate    = static_cast<int>(wav.sampleRate);
    info.channels      = wav.channels;
    info.bits          = wav.bitsPerSample;
    info.floatingPoint = tag == DR_WAVE_FORMAT_IEEE_FLOAT;
    info.lossless      = tag == DR_WAVE_FORMAT_PCM || tag == DR_WAVE_FORMAT_IEEE_FLOAT;
    info.frames        = static_cast<int64_t>(wav.totalPCMFrameCount);
    if (!info.lossless) {
        info.codec += tag == DR_WAVE_FORMAT_ALAW ? " A-law" : tag == DR_WAVE_FORMAT_MULAW ? " µ-law" : " ADPCM";
        info.bits = 0;
    }
    if (!out) return true;

    out->sampleRate = info.sampleRate;
    out->channels = info.channels;
    out->samples.resize(static_cast<std::size_t>(wav.totalPCMFrameCount) * wav.channels);
    const drwav_uint64 got = drwav_read_pcm_frames_f32(&wav, wav.totalPCMFrameCount, out->samples.data());
    out->samples.resize(static_cast<std::size_t>(got) * wav.channels);
    return true;
}


struct FlacState {
    AudioBuffer* out = nullptr;
    AudioInfo* info = nullptr;
    bool failed = false;
};

FLAC__StreamDecoderWriteStatus flacWrite(const FLAC__StreamDecoder*, const FLAC__Frame* frame,
                                         const FLAC__int32* const buffer[], void* user) {
    auto* st = static_cast<FlacState*>(user);
    const unsigned ch = frame->header.channels;
    const unsigned bps = frame->header.bits_per_sample;
    if (static_cast<int>(ch) != st->out->channels) {
        st->failed = true;   // 途中で channel 数が変わる stream は扱わない
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }
    const float scale = 1.0f / static_cast<float>(1u << (bps - 1));
    auto& s = st->out->samples;
    const std::size_t base = s.size();
    s.resize(base + static_cast<std::size_t>(frame->header.blocksize) * ch);
    for (unsigned i = 0; i < frame->header.blocksize; ++i)
        for (unsigned c = 0; c < ch; ++c)
            s[base + static_cast<std::size_t>(i) * ch + c] = static_cast<float>(buffer[c][i]) * scale;
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

void flacMetadata(const FLAC__StreamDecoder*, const FLAC__StreamMetadata* m, void* user) {
    if (m->type != FLAC__METADATA_TYPE_STREAMINFO) return;
    auto* st = static_cast<FlacState*>(user);
    const auto& si = m->data.stream_info;
    st->info->sampleRate = static_cast<int>(si.sample_rate);
    st->info->channels   = static_cast<int>(si.channels);
    st->info->bits       = static_cast<int>(si.bits_per_sample);
    st->info->frames     = static_cast<int64_t>(si.total_samples);
    if (st->out) {
        st->out->sampleRate = st->info->sampleRate;
        st->out->channels = st->info->channels;
        reserveFor(st->out, st->info->frames, st->info->channels);
    }
}

void flacError(const FLAC__StreamDecoder*, FLAC__StreamDecoderErrorStatus, void* user) {
    static_cast<FlacState*>(user)->failed = true;
}

bool decodeFlac(const std::string& path, AudioBuffer* out, AudioInfo& info, std::string& error) {
    std::FILE* f = openFile(path, "rb");
    if (!f) { error = "cannot open " + path; return false; }

    std::unique_ptr<FLAC__StreamDecoder, decltype(&FLAC__stream_decoder_delete)>
        dec(FLAC__stream_decoder_new(), &FLAC__stream_decoder_delete);
    FlacState st;
    st.out = out;
    st.info = &info;
    info.codec = "FLAC";
    info.lossless = true;

    // init_FILE は FILE* の所有権を持ち、finish 時に閉じる。
    if (FLAC__stream_decoder_init_FILE(dec.get(), f, flacWrite, flacMetadata, flacError, &st) !=
        FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        std::fclose(f);
        error = "not a readable FLAC file";
        return false;
    }
    const bool ok = out ? FLAC__stream_decoder_process_until_end_of_stream(dec.get())
                        : FLAC__stream_decoder_process_until_end_of_metadata(dec.get());
    FLAC__stream_decoder_finish(dec.get());
    if (!ok || st.failed || info.sampleRate == 0) {
        error = "FLAC decode failed";
        return false;
    }
    return true;
}


bool decodeMp3(const std::string& path, AudioBuffer* out, AudioInfo& info, std::string& error) {
    drmp3 mp3;
    if (!drmp3_init_file_w(&mp3, widenPath(path).c_str(), nullptr)) {
        error = "not a readable MP3 file";
        return false;
    }
    std::unique_ptr<drmp3, decltype(&drmp3_uninit)> guard(&mp3, &drmp3_uninit);

    info.codec = "MP3";
    info.lossless = false;
    info.sampleRate = static_cast<int>(mp3.sampleRate);
    info.channels = static_cast<int>(mp3.channels);
    info.frames = static_cast<int64_t>(drmp3_get_pcm_frame_count(&mp3));
    drmp3_seek_to_pcm_frame(&mp3, 0);

    // 平均 bitrate はファイルサイズと長さから見積もる。
    if (info.frames > 0 && info.sampleRate > 0) {
        FileCloser f(openFile(path, "rb"));
        if (f.f && std::fseek(f.f, 0, SEEK_END) == 0) {
            const double seconds = static_cast<double>(info.frames) / info.sampleRate;
            info.bitrateKbps = static_cast<int>(std::ftell(f.f) * 8.0 / seconds / 1000.0 + 0.5);
        }
    }
    if (!out) return true;

    out->sampleRate = info.sampleRate;
    out->channels = info.channels;
    reserveFor(out, info.frames, info.channels);
    float buf[4096 * 2];
    const drmp3_uint64 chunk = 4096;
    for (;;) {
        const drmp3_uint64 got = drmp3_read_pcm_frames_f32(&mp3, chunk, buf);
        if (got == 0) break;
        out->samples.insert(out->samples.end(), buf, buf + got * mp3.channels);
    }
    return true;
}


size_t vorbisRead(void* ptr, size_t size, size_t count, void* f) { return std::fread(ptr, size, count, static_cast<std::FILE*>(f)); }
int vorbisSeek(void* f, ogg_int64_t offset, int whence) { return f ? _fseeki64(static_cast<std::FILE*>(f), offset, whence) : -1; }
int vorbisClose(void* f) { return std::fclose(static_cast<std::FILE*>(f)); }
long vorbisTell(void* f) { return static_cast<long>(_ftelli64(static_cast<std::FILE*>(f))); }
const ov_callbacks kVorbisFile = {vorbisRead, vorbisSeek, vorbisClose, vorbisTell};

bool decodeVorbis(const std::string& path, AudioBuffer* out, AudioInfo& info, std::string& error) {
    std::FILE* f = openFile(path, "rb");
    if (!f) { error = "cannot open " + path; return false; }

    OggVorbis_File vf;
    // 既定 callback は ov_clear で FILE* も閉じる。
    if (ov_open_callbacks(f, &vf, nullptr, 0, kVorbisFile) != 0) {
        std::fclose(f);
        error = "not a readable Ogg Vorbis file";
        return false;
    }
    std::unique_ptr<OggVorbis_File, decltype(&ov_clear)> guard(&vf, &ov_clear);

    const vorbis_info* vi = ov_info(&vf, -1);
    info.codec = "Ogg Vorbis";
    info.lossless = false;
    info.sampleRate = static_cast<int>(vi->rate);
    info.channels = vi->channels;
    info.frames = static_cast<int64_t>(ov_pcm_total(&vf, -1));
    info.bitrateKbps = static_cast<int>(ov_bitrate(&vf, -1) / 1000);
    if (!out) return true;

    out->sampleRate = info.sampleRate;
    out->channels = info.channels;
    reserveFor(out, info.frames, info.channels);
    for (;;) {
        float** pcm = nullptr;
        int section = 0;
        const long got = ov_read_float(&vf, &pcm, 4096, &section);
        if (got == OV_HOLE) continue;
        if (got <= 0) break;
        if (ov_info(&vf, section)->channels != info.channels) break;   // chain 途中の形式変更は打ち切る
        for (long i = 0; i < got; ++i)
            for (int c = 0; c < info.channels; ++c) out->samples.push_back(pcm[c][i]);
    }
    return true;
}


bool decodeOpus(const std::string& path, AudioBuffer* out, AudioInfo& info, std::string& error) {
    // opusfile の path は Windows でも UTF-8 として扱われる。
    int err = 0;
    std::unique_ptr<OggOpusFile, decltype(&op_free)> of(op_open_file(path.c_str(), &err), &op_free);
    if (!of) {
        error = "not a readable Opus file";
        return false;
    }
    info.codec = "Opus";
    info.lossless = false;
    info.sampleRate = 48000;    // Opus は常に 48 kHz で decode される
    info.channels = op_channel_count(of.get(), -1);
    info.frames = static_cast<int64_t>(op_pcm_total(of.get(), -1));
    info.bitrateKbps = static_cast<int>(op_bitrate(of.get(), -1) / 1000);
    if (!out) return true;

    out->sampleRate = info.sampleRate;
    out->channels = info.channels;
    reserveFor(out, info.frames, info.channels);
    std::vector<float> buf(5760 * 8);
    for (;;) {
        int link = 0;
        const int got = op_read_float(of.get(), buf.data(), static_cast<int>(buf.size()), &link);
        if (got < 0 && got != OP_HOLE) break;
        if (got == OP_HOLE) continue;
        if (got == 0) break;
        if (op_channel_count(of.get(), link) != info.channels) break;
        out->samples.insert(out->samples.end(), buf.begin(), buf.begin() + got * info.channels);
    }
    return true;
}

bool decodeWith(Kind kind, const std::string& path, AudioBuffer* out, AudioInfo& info, std::string& error) {
    switch (kind) {
        case Kind::Wave:   return decodeWave(path, out, info, error);
        case Kind::Flac:   return decodeFlac(path, out, info, error);
        case Kind::Mp3:    return decodeMp3(path, out, info, error);
        case Kind::Vorbis: return decodeVorbis(path, out, info, error);
        case Kind::Opus:   return decodeOpus(path, out, info, error);
        default:           return false;
    }
}

bool decodeAny(const std::string& path, AudioBuffer* out, AudioInfo& info, std::string& error) {
    {
        FileCloser f(openFile(path, "rb"));
        if (!f.f) { error = "cannot open " + path; return false; }
    }
    const Kind kind = sniff(path);
    std::string ownError;
    if (kind != Kind::Unknown) {
        AudioInfo tmp;
        if (decodeWith(kind, path, out, tmp, ownError)) {
            info = tmp;
            return true;
        }
        if (out) out->samples.clear();
    }

    std::string mfError;
    if (mfDecode(path, out, info, mfError)) return true;
    error = !ownError.empty() ? ownError : mfError;
    return false;
}

}

bool probeAudio(const std::string& path, AudioInfo& info, std::string& error) {
    info = AudioInfo{};
    return decodeAny(path, nullptr, info, error);
}

bool decodeAudio(const std::string& path, AudioBuffer& out, AudioInfo* info, std::string& error) {
    AudioInfo local;
    out = AudioBuffer{};
    if (!decodeAny(path, &out, local, error)) return false;
    if (out.channels < 1 || out.sampleRate <= 0 || out.frames() == 0) {
        error = "the file contains no audio";
        return false;
    }
    local.frames = static_cast<int64_t>(out.frames());
    if (info) *info = local;
    return true;
}

}
