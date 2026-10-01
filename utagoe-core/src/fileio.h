// UTF-8 パスを安全に開くための file open helper。
// Windows の fopen(char*) は ANSI code page 解釈なので、日本語など非 ASCII のファイル名で失敗する。_wfopen 経由にする。
// 元実装は ANSI build でこの制約があったが、ここは意図的に直している。

#ifndef UTAGOE_FILEIO_H
#define UTAGOE_FILEIO_H

#include <cstdio>
#include <string>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace utagoe {

inline std::FILE* openFile(const std::string& utf8Path, const char* mode) {
#ifdef _WIN32
    auto widen = [](const std::string& s) {
        if (s.empty()) return std::wstring();
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring w(static_cast<std::size_t>(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
        return w;
    };
    return _wfopen(widen(utf8Path).c_str(), widen(mode).c_str());
#else
    return std::fopen(utf8Path.c_str(), mode);
#endif
}

}

#endif
