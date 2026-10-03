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

#ifdef _WIN32
inline std::wstring widenUtf8(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

inline bool isLongPath(const std::wstring& w) { return w.rfind(L"\\\\?\\", 0) == 0; }

inline std::wstring longPath(std::wstring w) {
    if (w.size() < 240 || isLongPath(w)) return w;
    const DWORD need = GetFullPathNameW(w.c_str(), 0, nullptr, nullptr);
    if (need > 0) {
        std::wstring full(need, L'\0');
        const DWORD got = GetFullPathNameW(w.c_str(), need, &full[0], nullptr);
        if (got > 0 && got < need) {
            full.resize(got);
            w = full;
        }
    }
    for (wchar_t& c : w)
        if (c == L'/') c = L'\\';
    if (w.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + w.substr(2);
    return L"\\\\?\\" + w;
}
#endif

inline std::string cannotCreate(const std::string& path) {
#ifdef _WIN32
    const DWORD e = GetLastError();
    const char* why = e == ERROR_SHARING_VIOLATION || e == ERROR_LOCK_VIOLATION ? "the file is open in another program"
                    : e == ERROR_ACCESS_DENIED ? "access denied (the file may be read-only, open elsewhere, or in a protected folder)"
                    : e == ERROR_PATH_NOT_FOUND ? "the folder does not exist"
                    : e == ERROR_DISK_FULL || e == ERROR_HANDLE_DISK_FULL ? "the disk is full"
                    : nullptr;
    if (why) return "cannot create " + path + ": " + why;
    if (e != 0) return "cannot create " + path + " (Windows error " + std::to_string(e) + ")";
#endif
    return "cannot create " + path;
}

inline std::FILE* openFile(const std::string& utf8Path, const char* mode) {
#ifdef _WIN32
    return _wfopen(longPath(widenUtf8(utf8Path)).c_str(), widenUtf8(mode).c_str());
#else
    return std::fopen(utf8Path.c_str(), mode);
#endif
}

}

#endif
