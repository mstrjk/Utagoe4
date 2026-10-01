// decode.cpp / encode.cpp / mediafoundation.cpp で共有する内部 helper。

#ifndef UTAGOE_CODEC_INTERNAL_H
#define UTAGOE_CODEC_INTERNAL_H

#include "utagoe.h"
#include "fileio.h"

#include <cstdio>
#include <string>

namespace utagoe {

// UTF-8 path を wide 文字列へ。Windows の各 API は wide path で開く。
inline std::wstring widenPath(const std::string& s) {
#ifdef _WIN32
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
#else
    return std::wstring(s.begin(), s.end());
#endif
}

// FILE* の RAII。
struct FileCloser {
    std::FILE* f = nullptr;
    explicit FileCloser(std::FILE* file) : f(file) {}
    ~FileCloser() { if (f) std::fclose(f); }
    FileCloser(const FileCloser&) = delete;
    FileCloser& operator=(const FileCloser&) = delete;
    std::FILE* release() { std::FILE* r = f; f = nullptr; return r; }
};

// Media Foundation 経由の decode / encode。Windows 以外では常に失敗を返す。
// out が nullptr のときは形式情報だけ読み、音声は展開しない。
bool mfDecode(const std::string& path, AudioBuffer* out, AudioInfo& info, std::string& error);
bool mfEncode(const std::string& path, const AudioBuffer& in, OutputFormat format,
              int bits, int bitrateKbps, std::string& error);

}

#endif
