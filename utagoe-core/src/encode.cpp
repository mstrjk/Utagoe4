// 出力音声の encode。形式ごとに同梱ライブラリか Media Foundation へ振り分ける。
// float から 16-bit へ落とすときは TPDF dither を掛ける。ただし既に 16-bit の格子上にある (v3 互換経路の) 音声には掛けない。

#include "codec_internal.h"

#include "dr_wav.h"
#include "FLAC/stream_encoder.h"
#include "vorbis/vorbisenc.h"
#include "opusenc.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <memory>

namespace utagoe {
namespace {


// 再現性のため dither の乱数は固定 seed の LCG。
struct Dither {
    uint32_t state = 0x12345678u;
    double next() {
        state = state * 1664525u + 1013904223u;
        return static_cast<double>(state >> 8) / 16777216.0;   // 戻り値は [0, 1)。
    }
    double tpdf() { return next() - next(); }                  // [-1, 1) の三角分布にする。
};

// 全 sample が 16-bit の格子上なら dither は不要。
bool onInt16Grid(const AudioBuffer& in) {
    for (float v : in.samples) {
        const float s = v * 32768.0f;
        if (s != std::nearbyint(s)) return false;
    }
    return true;
}

// float -> bits bit 整数。16-bit で必要なときだけ dither する。
std::vector<int32_t> toInts(const AudioBuffer& in, int bits) {
    const double full = std::ldexp(1.0, bits - 1);
    const double maxV = full - 1.0;
    const bool dither = bits == 16 && !onInt16Grid(in);
    Dither d;
    std::vector<int32_t> out(in.samples.size());
    for (std::size_t i = 0; i < out.size(); ++i) {
        double v = static_cast<double>(in.samples[i]) * full;
        if (dither) v += d.tpdf();
        v = std::nearbyint(v);
        out[i] = static_cast<int32_t>(std::min(maxV, std::max(-full, v)));
    }
    return out;
}

int bitsOf(OutputDepth d) {
    switch (d) {
        case OutputDepth::Int24: return 24;
        case OutputDepth::Float32: return 32;
        default: return 16;
    }
}


void put16le(std::vector<uint8_t>& v, uint32_t x) { v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8)); }
void put32le(std::vector<uint8_t>& v, uint32_t x) { put16le(v, x & 0xFFFF); put16le(v, x >> 16); }

uint32_t speakerMask(int channels) {
    switch (channels) {
        case 6: return 0x3F;
        case 8: return 0x63F;
        default: return 0;
    }
}

bool writeWaveExtensible(const std::string& path, const AudioBuffer& in, bool isFloat, int bits, std::string& error) {
    const uint32_t bytesPer = static_cast<uint32_t>(bits / 8);
    const uint32_t dataBytes = static_cast<uint32_t>(in.samples.size() * bytesPer);
    std::vector<uint8_t> h;
    h.insert(h.end(), {'R', 'I', 'F', 'F'});
    put32le(h, 4 + 8 + 40 + 8 + dataBytes + (dataBytes & 1));
    h.insert(h.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put32le(h, 40);
    put16le(h, 0xFFFE);
    put16le(h, static_cast<uint32_t>(in.channels));
    put32le(h, static_cast<uint32_t>(in.sampleRate));
    put32le(h, static_cast<uint32_t>(in.sampleRate) * bytesPer * static_cast<uint32_t>(in.channels));
    put16le(h, bytesPer * static_cast<uint32_t>(in.channels));
    put16le(h, static_cast<uint32_t>(bits));
    put16le(h, 22);
    put16le(h, static_cast<uint32_t>(bits));
    put32le(h, speakerMask(in.channels));
    put16le(h, isFloat ? 3 : 1);
    h.insert(h.end(), {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71});
    h.insert(h.end(), {'d', 'a', 't', 'a'});
    put32le(h, dataBytes);

    std::vector<uint8_t> body;
    if (isFloat) {
        body.resize(dataBytes);
        std::memcpy(body.data(), in.samples.data(), dataBytes);
    } else {
        const auto ints = toInts(in, bits);
        body.resize(dataBytes);
        for (std::size_t i = 0; i < ints.size(); ++i)
            for (uint32_t b = 0; b < bytesPer; ++b)
                body[i * bytesPer + b] = static_cast<uint8_t>(ints[i] >> (8 * b));
    }
    if (dataBytes & 1) body.push_back(0);

    std::FILE* f = openFile(path, "wb");
    if (!f) { error = cannotCreate(path); return false; }
    const bool ok = std::fwrite(h.data(), 1, h.size(), f) == h.size() &&
                    std::fwrite(body.data(), 1, body.size(), f) == body.size();
    const bool closed = std::fclose(f) == 0;
    if (!ok || !closed) { error = "write failed"; return false; }
    return true;
}

bool writeWave(const std::string& path, const AudioBuffer& in, OutputDepth depth, std::string& error) {
    const bool isFloat = depth == OutputDepth::Float32;
    const int bits = bitsOf(depth);
    const uint64_t dataBytes = static_cast<uint64_t>(in.samples.size()) * (bits / 8);
    if (in.channels > 2 && dataBytes <= 0xFFFFFF00ULL) return writeWaveExtensible(path, in, isFloat, bits, error);

    drwav_data_format fmt{};
    // 4 GB を超える場合は RF64 にする。
    fmt.container = dataBytes > 0xFFFFFF00ULL ? drwav_container_rf64 : drwav_container_riff;
    fmt.format = isFloat ? DR_WAVE_FORMAT_IEEE_FLOAT : DR_WAVE_FORMAT_PCM;
    fmt.channels = static_cast<drwav_uint32>(in.channels);
    fmt.sampleRate = static_cast<drwav_uint32>(in.sampleRate);
    fmt.bitsPerSample = static_cast<drwav_uint32>(bits);

    drwav wav;
    if (!drwav_init_file_write_w(&wav, widenPath(path).c_str(), &fmt, nullptr)) {
        error = cannotCreate(path);
        return false;
    }
    std::unique_ptr<drwav, decltype(&drwav_uninit)> guard(&wav, &drwav_uninit);

    const drwav_uint64 frames = in.frames();
    drwav_uint64 written = 0;
    if (isFloat) {
        written = drwav_write_pcm_frames(&wav, frames, in.samples.data());
    } else {
        const auto ints = toInts(in, bits);
        std::vector<uint8_t> bytes(ints.size() * (bits / 8));
        for (std::size_t i = 0; i < ints.size(); ++i)
            for (int b = 0; b < bits / 8; ++b)
                bytes[i * (bits / 8) + b] = static_cast<uint8_t>(ints[i] >> (8 * b));
        written = drwav_write_pcm_frames(&wav, frames, bytes.data());
    }
    if (written != frames) { error = "write failed"; return false; }
    return true;
}


void put16be(std::vector<uint8_t>& v, uint32_t x) { v.push_back(uint8_t(x >> 8)); v.push_back(uint8_t(x)); }
void put32be(std::vector<uint8_t>& v, uint32_t x) { put16be(v, x >> 16); put16be(v, x & 0xFFFF); }

// AIFF の sample rate は 80-bit 拡張精度浮動小数。
void putExtended(std::vector<uint8_t>& v, double x) {
    int exp = 0;
    const double mant = std::frexp(x, &exp);            // x = mant * 2^exp の形に分ける。0.5 <= mant < 1。
    const uint64_t m = static_cast<uint64_t>(std::ldexp(mant, 64));
    put16be(v, static_cast<uint32_t>(exp - 1 + 16383));
    for (int i = 7; i >= 0; --i) v.push_back(static_cast<uint8_t>(m >> (8 * i)));
}

bool writeAiff(const std::string& path, const AudioBuffer& in, OutputDepth depth, std::string& error) {
    const int bits = depth == OutputDepth::Int16 ? 16 : 24;   // AIFF は整数 PCM のみ
    const int bytesPer = bits / 8;
    const auto ints = toInts(in, bits);
    const uint32_t dataBytes = static_cast<uint32_t>(ints.size() * bytesPer);

    std::vector<uint8_t> h;
    h.insert(h.end(), {'F', 'O', 'R', 'M'});
    put32be(h, 4 + 8 + 18 + 8 + 8 + dataBytes);
    h.insert(h.end(), {'A', 'I', 'F', 'F', 'C', 'O', 'M', 'M'});
    put32be(h, 18);
    put16be(h, static_cast<uint32_t>(in.channels));
    put32be(h, static_cast<uint32_t>(in.frames()));
    put16be(h, static_cast<uint32_t>(bits));
    putExtended(h, in.sampleRate);
    h.insert(h.end(), {'S', 'S', 'N', 'D'});
    put32be(h, 8 + dataBytes);
    put32be(h, 0);
    put32be(h, 0);

    std::vector<uint8_t> data(dataBytes);
    for (std::size_t i = 0; i < ints.size(); ++i)
        for (int b = 0; b < bytesPer; ++b)
            data[i * bytesPer + b] = static_cast<uint8_t>(ints[i] >> (8 * (bytesPer - 1 - b)));

    FileCloser f(openFile(path, "wb"));
    if (!f.f) { error = cannotCreate(path); return false; }
    if (std::fwrite(h.data(), 1, h.size(), f.f) != h.size() ||
        std::fwrite(data.data(), 1, data.size(), f.f) != data.size()) {
        error = "write failed";
        return false;
    }
    return true;
}


bool writeFlac(const std::string& path, const AudioBuffer& in, OutputDepth depth, std::string& error) {
    const int bits = depth == OutputDepth::Int16 ? 16 : 24;   // FLAC は整数 PCM のみ
    std::unique_ptr<FLAC__StreamEncoder, decltype(&FLAC__stream_encoder_delete)>
        enc(FLAC__stream_encoder_new(), &FLAC__stream_encoder_delete);
    FLAC__stream_encoder_set_channels(enc.get(), static_cast<unsigned>(in.channels));
    FLAC__stream_encoder_set_bits_per_sample(enc.get(), static_cast<unsigned>(bits));
    FLAC__stream_encoder_set_sample_rate(enc.get(), static_cast<unsigned>(in.sampleRate));
    FLAC__stream_encoder_set_compression_level(enc.get(), 5);
    FLAC__stream_encoder_set_total_samples_estimate(enc.get(), in.frames());

    std::FILE* f = openFile(path, "wb");
    if (!f) { error = cannotCreate(path); return false; }
    // init_FILE は FILE* の所有権を持つ。
    if (FLAC__stream_encoder_init_FILE(enc.get(), f, nullptr, nullptr) != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
        std::fclose(f);
        error = "FLAC encoder could not start";
        return false;
    }
    const auto ints = toInts(in, bits);
    const std::size_t chunk = 8192;
    for (std::size_t pos = 0; pos < in.frames(); pos += chunk) {
        const std::size_t take = std::min(chunk, in.frames() - pos);
        if (!FLAC__stream_encoder_process_interleaved(enc.get(), ints.data() + pos * in.channels,
                                                      static_cast<unsigned>(take))) {
            FLAC__stream_encoder_finish(enc.get());
            error = "FLAC encoding failed";
            return false;
        }
    }
    if (!FLAC__stream_encoder_finish(enc.get())) { error = "FLAC encoding failed"; return false; }
    return true;
}


bool writeVorbis(const std::string& path, const AudioBuffer& in, int kbps, std::string& error) {
    FileCloser f(openFile(path, "wb"));
    if (!f.f) { error = cannotCreate(path); return false; }

    vorbis_info vi;
    vorbis_info_init(&vi);
    // 平均 bitrate 指定 (ABR)。範囲外なら同程度の品質の VBR へ切り替える。
    if (vorbis_encode_init(&vi, in.channels, in.sampleRate, -1, kbps * 1000L, -1) != 0) {
        vorbis_info_clear(&vi);
        vorbis_info_init(&vi);
        const float q = std::min(1.0f, std::max(-0.1f, (kbps - 64) / 256.0f));
        if (vorbis_encode_init_vbr(&vi, in.channels, in.sampleRate, q) != 0) {
            vorbis_info_clear(&vi);
            error = "Vorbis does not support this sample rate / channel count";
            return false;
        }
    }

    vorbis_comment vc;
    vorbis_comment_init(&vc);
    vorbis_comment_add_tag(&vc, "ENCODER", "Utagoe");
    vorbis_dsp_state vd;
    vorbis_block vb;
    vorbis_analysis_init(&vd, &vi);
    vorbis_block_init(&vd, &vb);
    ogg_stream_state os;
    ogg_stream_init(&os, 0x55544147);   // serial は任意

    bool ok = true;
    auto writePages = [&](bool flush) {
        ogg_page og;
        while (flush ? ogg_stream_flush(&os, &og) : ogg_stream_pageout(&os, &og)) {
            if (std::fwrite(og.header, 1, og.header_len, f.f) != static_cast<std::size_t>(og.header_len) ||
                std::fwrite(og.body, 1, og.body_len, f.f) != static_cast<std::size_t>(og.body_len))
                ok = false;
        }
    };

    ogg_packet hdr, hdrComm, hdrCode;
    vorbis_analysis_headerout(&vd, &vc, &hdr, &hdrComm, &hdrCode);
    ogg_stream_packetin(&os, &hdr);
    ogg_stream_packetin(&os, &hdrComm);
    ogg_stream_packetin(&os, &hdrCode);
    writePages(true);

    const std::size_t frames = in.frames();
    const std::size_t chunk = 4096;
    // 最後は take = 0 で vorbis_analysis_wrote を呼び、stream を終端する。
    for (std::size_t pos = 0; ok; pos += chunk) {
        const std::size_t take = pos < frames ? std::min(chunk, frames - pos) : 0;
        if (take > 0) {
            float** buf = vorbis_analysis_buffer(&vd, static_cast<int>(take));
            for (std::size_t i = 0; i < take; ++i)
                for (int c = 0; c < in.channels; ++c) buf[c][i] = in.samples[(pos + i) * in.channels + c];
        }
        vorbis_analysis_wrote(&vd, static_cast<int>(take));   // 0 で終端
        while (vorbis_analysis_blockout(&vd, &vb) == 1) {
            vorbis_analysis(&vb, nullptr);
            vorbis_bitrate_addblock(&vb);
            ogg_packet op;
            while (vorbis_bitrate_flushpacket(&vd, &op)) {
                ogg_stream_packetin(&os, &op);
                writePages(false);
            }
        }
        if (take == 0) break;
    }
    writePages(true);

    ogg_stream_clear(&os);
    vorbis_block_clear(&vb);
    vorbis_dsp_clear(&vd);
    vorbis_comment_clear(&vc);
    vorbis_info_clear(&vi);
    if (!ok) error = "write failed";
    return ok;
}


bool writeOpus(const std::string& path, const AudioBuffer& in, int kbps, std::string& error) {
    std::unique_ptr<OggOpusComments, decltype(&ope_comments_destroy)>
        comments(ope_comments_create(), &ope_comments_destroy);
    ope_comments_add(comments.get(), "ENCODER", "Utagoe");

    // libopusenc の path は Windows でも UTF-8。入力 rate は内部で 48 kHz へ変換される。
    int err = 0;
    const int family = in.channels <= 2 ? 0 : 1;
    std::unique_ptr<OggOpusEnc, decltype(&ope_encoder_destroy)> enc(
        ope_encoder_create_file(path.c_str(), comments.get(), in.sampleRate, in.channels, family, &err),
        &ope_encoder_destroy);
    if (!enc) { error = std::string("Opus encoder could not start: ") + ope_strerror(err); return false; }

    ope_encoder_ctl(enc.get(), OPUS_SET_BITRATE(kbps * 1000));
    const std::size_t chunk = 4096;
    for (std::size_t pos = 0; pos < in.frames(); pos += chunk) {
        const std::size_t take = std::min(chunk, in.frames() - pos);
        if (ope_encoder_write_float(enc.get(), in.samples.data() + pos * in.channels, static_cast<int>(take)) != OPE_OK) {
            error = "Opus encoding failed";
            return false;
        }
    }
    if (ope_encoder_drain(enc.get()) != OPE_OK) { error = "Opus encoding failed"; return false; }
    return true;
}

std::string lowerExt(const std::string& path) {
    const auto dot = path.find_last_of('.');
    const auto sep = path.find_last_of("/\\");
    if (dot == std::string::npos || (sep != std::string::npos && dot < sep)) return {};
    std::string e = path.substr(dot);
    for (auto& ch : e) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return e;
}

}

OutputFormat formatFromExtension(const std::string& path, OutputFormat fallback) {
    const std::string e = lowerExt(path);
    if (e == ".wav" || e == ".wave") return OutputFormat::Wav;
    if (e == ".aif" || e == ".aiff") return OutputFormat::Aiff;
    if (e == ".flac") return OutputFormat::Flac;
    if (e == ".mp3") return OutputFormat::Mp3;
    if (e == ".m4a" || e == ".mp4") return fallback == OutputFormat::Alac ? OutputFormat::Alac : OutputFormat::Aac;
    if (e == ".ogg" || e == ".oga") return OutputFormat::Vorbis;
    if (e == ".opus") return OutputFormat::Opus;
    if (e == ".wma") return OutputFormat::Wma;
    return fallback;
}

const char* extensionOf(OutputFormat f) {
    switch (f) {
        case OutputFormat::Aiff: return ".aiff";
        case OutputFormat::Flac: return ".flac";
        case OutputFormat::Alac:
        case OutputFormat::Aac:  return ".m4a";
        case OutputFormat::Mp3:  return ".mp3";
        case OutputFormat::Vorbis: return ".ogg";
        case OutputFormat::Opus: return ".opus";
        case OutputFormat::Wma:  return ".wma";
        default:                 return ".wav";
    }
}

bool isLossless(OutputFormat f) {
    return f == OutputFormat::Wav || f == OutputFormat::Aiff || f == OutputFormat::Flac || f == OutputFormat::Alac;
}

OutputDepth resolveDepth(OutputDepth requested, OutputFormat format,
                         const AudioInfo& a, const AudioInfo& b) {
    // float を保存できるのは WAV だけ。
    const bool floatOk = format == OutputFormat::Wav;
    if (!isLossless(format)) return OutputDepth::Float32;   // lossy は encoder 側で扱うので精度は落とさない
    if (requested != OutputDepth::Auto)
        return (requested == OutputDepth::Float32 && !floatOk) ? OutputDepth::Int24 : requested;

    auto is16 = [](const AudioInfo& i) { return i.lossless && !i.floatingPoint && i.bits > 0 && i.bits <= 16; };
    if (is16(a) && is16(b)) return OutputDepth::Int16;
    if ((a.floatingPoint || b.floatingPoint) && floatOk) return OutputDepth::Float32;
    return OutputDepth::Int24;
}

bool encodeAudio(const std::string& path, const AudioBuffer& in, const EncodeOptions& opt, std::string& error) {
    if (in.channels < 1 || in.sampleRate <= 0) { error = "nothing to write"; return false; }
    const OutputDepth depth = opt.depth == OutputDepth::Auto ? OutputDepth::Int24 : opt.depth;
    const int kbps = std::max(32, opt.bitrate);

    switch (opt.format) {
        case OutputFormat::Wav:    return writeWave(path, in, depth, error);
        case OutputFormat::Aiff:   return writeAiff(path, in, depth, error);
        case OutputFormat::Flac:   return writeFlac(path, in, depth, error);
        case OutputFormat::Vorbis: return writeVorbis(path, in, kbps, error);
        case OutputFormat::Opus:   return writeOpus(path, in, kbps, error);
        case OutputFormat::Alac:   return mfEncode(path, in, opt.format, bitsOf(depth), kbps, error);
        default:                   return mfEncode(path, in, opt.format, 16, kbps, error);
    }
}

}
