// 処理の記録と stack trace。
//
// stack trace は RtlCaptureStackBackTrace で戻り番地を集め、各番地を module と関数名に直す。
// 関数名は module 自身の COFF symbol table (MinGW は strip しなければ static 関数も含めて残す) から引き、
// abi::__cxa_demangle で C++ の名前に戻す。COFF symbol の無い module (Windows の DLL など) は export 名で代用する。
// C++ 例外は __cxa_throw を link 時に包み (-Wl,--wrap=__cxa_throw)、投げた位置の stack を thread ごとに覚えておく。

#include "log.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cxxabi.h>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace utagoe {
namespace log {
namespace {

std::atomic<Callback> g_fn{nullptr};
std::atomic<void*> g_user{nullptr};

void emit(int kind, int id, const char* text) {
    Callback fn = g_fn.load();
    if (fn) fn(kind, id, text, g_user.load());
}

std::string vformat(const char* fmt, va_list ap) {
    char buf[1024];
    va_list cp;
    va_copy(cp, ap);
    const int n = std::vsnprintf(buf, sizeof buf, fmt, ap);
    if (n < static_cast<int>(sizeof buf)) {
        va_end(cp);
        return std::string(buf, n > 0 ? static_cast<std::size_t>(n) : 0);
    }
    std::string s(static_cast<std::size_t>(n) + 1, '\0');
    std::vsnprintf(s.data(), s.size(), fmt, cp);
    va_end(cp);
    s.resize(static_cast<std::size_t>(n));
    return s;
}

// 進捗行の間引き。id ごとに前回の時刻を持つ。
std::mutex g_statusMutex;
std::map<int, std::chrono::steady_clock::time_point> g_statusTime;


struct Symbol {
    uint32_t rva;
    std::string name;
};

struct ModuleSymbols {
    std::vector<Symbol> symbols;
    bool loaded = false;
};

std::mutex g_symMutex;
std::map<uintptr_t, std::unique_ptr<ModuleSymbols>> g_modules;

std::string narrow(const std::wstring& w) {
    const int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(std::max(len, 0)), '\0');
    if (len > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), len, nullptr, nullptr);
    return s;
}

std::string demangle(const std::string& name) {
    int status = 0;
    char* d = abi::__cxa_demangle(name.c_str(), nullptr, nullptr, &status);
    if (status == 0 && d) {
        std::string s(d);
        std::free(d);
        return s;
    }
    return name;
}

// module のファイルから COFF の関数 symbol を読む。
void loadCoff(const std::wstring& path, ModuleSymbols& m) {
    std::FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return;
    std::vector<char> d;
    char chunk[65536];
    for (std::size_t got; (got = std::fread(chunk, 1, sizeof chunk, f)) > 0;) d.insert(d.end(), chunk, chunk + got);
    std::fclose(f);
    auto rd = [&](std::size_t off, void* dst, std::size_t n) {
        if (off + n > d.size()) return false;
        std::memcpy(dst, d.data() + off, n);
        return true;
    };
    IMAGE_DOS_HEADER dos{};
    if (!rd(0, &dos, sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE) return;
    const std::size_t pe = static_cast<std::size_t>(dos.e_lfanew);
    IMAGE_FILE_HEADER fh{};
    if (!rd(pe + 4, &fh, sizeof fh)) return;
    if (fh.PointerToSymbolTable == 0 || fh.NumberOfSymbols == 0) return;
    const std::size_t secOff = pe + 4 + sizeof(IMAGE_FILE_HEADER) + fh.SizeOfOptionalHeader;
    std::vector<IMAGE_SECTION_HEADER> secs(fh.NumberOfSections);
    if (!rd(secOff, secs.data(), secs.size() * sizeof(IMAGE_SECTION_HEADER))) return;
    const std::size_t symOff = fh.PointerToSymbolTable;
    const std::size_t strOff = symOff + static_cast<std::size_t>(fh.NumberOfSymbols) * 18;
    for (DWORD i = 0; i < fh.NumberOfSymbols; ++i) {
        unsigned char rec[18];
        if (!rd(symOff + static_cast<std::size_t>(i) * 18, rec, 18)) break;
        uint32_t value;
        int16_t section;
        uint16_t type;
        std::memcpy(&value, rec + 8, 4);
        std::memcpy(&section, rec + 12, 2);
        std::memcpy(&type, rec + 14, 2);
        const uint8_t aux = rec[17];
        if (type == 0x20 && section > 0 && section <= static_cast<int>(secs.size())) {
            std::string name;
            uint32_t zero, off;
            std::memcpy(&zero, rec, 4);
            if (zero == 0) {
                std::memcpy(&off, rec + 4, 4);
                const std::size_t at = strOff + off;
                if (at < d.size()) name = std::string(d.data() + at, strnlen(d.data() + at, d.size() - at));
            } else {
                name = std::string(reinterpret_cast<char*>(rec), strnlen(reinterpret_cast<char*>(rec), 8));
            }
            m.symbols.push_back({secs[static_cast<std::size_t>(section - 1)].VirtualAddress + value, name});
        }
        i += aux;
    }
    std::sort(m.symbols.begin(), m.symbols.end(), [](const Symbol& a, const Symbol& b) { return a.rva < b.rva; });
}

// COFF symbol の無い module は export 表から近い名前を探す。
void loadExports(HMODULE mod, ModuleSymbols& m) {
    auto base = reinterpret_cast<const unsigned char*>(mod);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress) return;
    auto exp = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress);
    auto names = reinterpret_cast<const DWORD*>(base + exp->AddressOfNames);
    auto ords = reinterpret_cast<const WORD*>(base + exp->AddressOfNameOrdinals);
    auto funcs = reinterpret_cast<const DWORD*>(base + exp->AddressOfFunctions);
    for (DWORD i = 0; i < exp->NumberOfNames; ++i) {
        const DWORD rva = funcs[ords[i]];
        if (rva >= dir.VirtualAddress && rva < dir.VirtualAddress + dir.Size) continue;
        m.symbols.push_back({rva, reinterpret_cast<const char*>(base + names[i])});
    }
    std::sort(m.symbols.begin(), m.symbols.end(), [](const Symbol& a, const Symbol& b) { return a.rva < b.rva; });
}

std::string describe(void* addr) {
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(addr), &mod) || !mod) {
        char buf[48];
        std::snprintf(buf, sizeof buf, "0x%p", addr);
        return buf;
    }
    wchar_t pathBuf[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(mod, pathBuf, static_cast<DWORD>(std::size(pathBuf)));
    const std::wstring path(pathBuf, n);
    const std::wstring file = path.substr(path.find_last_of(L"\\/") + 1);
    const uintptr_t base = reinterpret_cast<uintptr_t>(mod);
    const uint32_t rva = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(addr) - base);

    std::string name;
    uint32_t symRva = 0;
    {
        std::lock_guard<std::mutex> lk(g_symMutex);
        auto& slot = g_modules[base];
        if (!slot) slot = std::make_unique<ModuleSymbols>();
        if (!slot->loaded) {
            loadCoff(path, *slot);
            if (slot->symbols.empty()) loadExports(mod, *slot);
            slot->loaded = true;
        }
        const auto& syms = slot->symbols;
        auto it = std::upper_bound(syms.begin(), syms.end(), rva, [](uint32_t v, const Symbol& s) { return v < s.rva; });
        if (it != syms.begin()) {
            --it;
            name = it->name;
            symRva = it->rva;
        }
    }
    char buf[96];
    std::snprintf(buf, sizeof buf, "  [%s+0x%x]", narrow(file).c_str(), rva);
    if (name.empty()) return std::string("<unknown>") + buf;
    char off[32];
    std::snprintf(off, sizeof off, "+0x%x", rva - symRva);
    return demangle(name) + off + buf;
}

std::string format(void* const* frames, int n) {
    std::string s;
    for (int i = 0; i < n; ++i) {
        // 戻り番地は call 命令の次を指すので、1 引いて呼び出し元の行に寄せる。
        s += "   at " + describe(static_cast<char*>(frames[i]) - 1) + "\n";
    }
    return s;
}

thread_local void* t_throwFrames[62];
thread_local int t_throwCount = 0;


std::string g_crashDir;
std::atomic<bool> g_crashWritten{false};
PVOID g_handler = nullptr;

bool ourModule(void* addr) {
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(addr), &mod) || !mod)
        return false;
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(mod, buf, MAX_PATH);
    std::wstring file(buf, n);
    file = file.substr(file.find_last_of(L"\\/") + 1);
    std::transform(file.begin(), file.end(), file.begin(), ::towlower);
    return file.rfind(L"utagoe", 0) == 0 && file.find(L".dll") != std::wstring::npos;
}

LONG CALLBACK crashHandler(EXCEPTION_POINTERS* ep) {
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    const bool fatal = code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION ||
                       code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == EXCEPTION_IN_PAGE_ERROR ||
                       code == EXCEPTION_PRIV_INSTRUCTION || code == EXCEPTION_ARRAY_BOUNDS_EXCEEDED;
    // .NET の NullReferenceException も内部ではアクセス違反なので、この DLL 群の中で起きたものだけを扱う。
    if (!fatal || !ourModule(ep->ExceptionRecord->ExceptionAddress) || g_crashWritten.exchange(true))
        return EXCEPTION_CONTINUE_SEARCH;
    void* frames[62];
    const int n = RtlCaptureStackBackTrace(0, 62, frames, nullptr);
    char head[256];
    std::snprintf(head, sizeof head, "native crash: exception 0x%08lX at %s\n",
                  static_cast<unsigned long>(code), describe(ep->ExceptionRecord->ExceptionAddress).c_str());
    std::string text = head;
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
        char av[128];
        std::snprintf(av, sizeof av, "  %s of address 0x%p\n",
                      ep->ExceptionRecord->ExceptionInformation[0] == 0 ? "read" : ep->ExceptionRecord->ExceptionInformation[0] == 1 ? "write" : "execute",
                      reinterpret_cast<void*>(ep->ExceptionRecord->ExceptionInformation[1]));
        text += av;
    }
    text += format(frames, n);
    if (!g_crashDir.empty()) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        char name[64];
        std::snprintf(name, sizeof name, "\\crash-%04d%02d%02d-%02d%02d%02d.txt", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        const std::string p = g_crashDir + name;
        const int wl = MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, nullptr, 0);
        std::wstring wp(static_cast<std::size_t>(wl), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, wp.data(), wl);
        HANDLE h = CreateFileW(wp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD w = 0;
            WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &w, nullptr);
            CloseHandle(h);
        }
    }
    emit(Error, 0, text.c_str());
    return EXCEPTION_CONTINUE_SEARCH;
}

}

void setCallback(Callback fn, void* user) {
    g_user.store(user);
    g_fn.store(fn);
}

bool enabled() { return g_fn.load() != nullptr; }

void line(const char* fmt, ...) {
    if (!enabled()) return;
    va_list ap;
    va_start(ap, fmt);
    const std::string s = vformat(fmt, ap);
    va_end(ap);
    emit(Line, 0, s.c_str());
}

void detail(const char* fmt, ...) {
    if (!enabled()) return;
    va_list ap;
    va_start(ap, fmt);
    const std::string s = vformat(fmt, ap);
    va_end(ap);
    emit(Detail, 0, s.c_str());
}

void warn(const char* fmt, ...) {
    if (!enabled()) return;
    va_list ap;
    va_start(ap, fmt);
    const std::string s = vformat(fmt, ap);
    va_end(ap);
    emit(Warning, 0, s.c_str());
}

void error(const std::string& message, const std::string& stack) {
    if (!enabled()) return;
    emit(Error, 0, (stack.empty() ? message : message + "\n" + stack).c_str());
}

void status(int id, bool force, const char* fmt, ...) {
    if (!enabled()) return;
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lk(g_statusMutex);
        auto it = g_statusTime.find(id);
        if (!force && it != g_statusTime.end() && now - it->second < std::chrono::milliseconds(100)) return;
        g_statusTime[id] = now;
    }
    va_list ap;
    va_start(ap, fmt);
    const std::string s = vformat(fmt, ap);
    va_end(ap);
    emit(Status, id, s.c_str());
}

void clearStatus(int id) {
    if (!enabled()) return;
    {
        std::lock_guard<std::mutex> lk(g_statusMutex);
        g_statusTime.erase(id);
    }
    emit(Status, id, "");
}

std::string progressBar(double fraction, int width) {
    fraction = std::clamp(fraction, 0.0, 1.0);
    const int full = static_cast<int>(fraction * width + 0.5);
    std::string s = "[";
    for (int i = 0; i < width; ++i) s += i < full ? '#' : '.';
    char pct[16];
    std::snprintf(pct, sizeof pct, "] %5.1f%%", fraction * 100.0);
    return s + pct;
}

std::string clock(double seconds) {
    if (seconds < 0) return "--:--";
    const long long t = static_cast<long long>(seconds + 0.5);
    char buf[32];
    if (t >= 3600) std::snprintf(buf, sizeof buf, "%lld:%02lld:%02lld", t / 3600, (t / 60) % 60, t % 60);
    else std::snprintf(buf, sizeof buf, "%02lld:%02lld", t / 60, t % 60);
    return buf;
}

Stage::Stage(const std::string& name) : name_(name), start_(std::chrono::steady_clock::now()) {
    emit(StageBegin, 0, name_.c_str());
}

Stage::~Stage() {
    // id に所要時間 (ms) を入れる。失敗した段階は負にする。
    const int ms = static_cast<int>(seconds() * 1000.0);
    emit(StageEnd, failed_ ? -1 - ms : ms, name_.c_str());
}

double Stage::seconds() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
}

namespace {
thread_local Bar* g_bar = nullptr;
}

Bar::Bar(int id) : id_(id), previous_(g_bar) { g_bar = this; }

Bar::~Bar() {
    g_bar = previous_;
    clearStatus(id_);
}

void Bar::range(double from, double to) {
    from_ = std::clamp(from, 0.0, 1.0);
    to_ = std::clamp(to, from_, 1.0);
}

void Bar::update(double fraction, const std::string& what) {
    std::string label;
    double f;
    {
        std::lock_guard<std::mutex> g(lock_);
        f = std::max(shown_, from_ + (to_ - from_) * std::clamp(fraction, 0.0, 1.0));
        shown_ = f;
        if (!what.empty()) what_ = what;
        label = what_.empty() ? std::string() : " | " + what_;
    }
    status(id_, false, "  %s%s | %s elapsed | ~%s left", progressBar(f).c_str(), label.c_str(),
           clock(eta_.elapsed()).c_str(), clock(eta_.remaining(f)).c_str());
}

void Bar::report(double fraction, const std::string& what) {
    if (g_bar) g_bar->update(fraction, what);
}

void Bar::report(double done, double total, const std::string& what) {
    if (g_bar && total > 0) g_bar->update(done / total, what);
}

Bar* Bar::current() { return g_bar; }

double Eta::elapsed() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
}

double Eta::remaining(double fraction) const {
    const double e = elapsed();
    if (fraction < 0.02 || e < 0.5) return -1.0;
    return e * (1.0 - fraction) / fraction;
}

std::string stackTrace(int skip) {
    void* frames[62];
    const int n = RtlCaptureStackBackTrace(static_cast<DWORD>(skip + 1), 62, frames, nullptr);
    return format(frames, n);
}

std::string lastThrowTrace() { return format(t_throwFrames, t_throwCount); }

void setCrashDirectory(const std::string& utf8Dir) {
    g_crashDir = utf8Dir;
    if (!g_handler && !utf8Dir.empty()) g_handler = AddVectoredExceptionHandler(1, crashHandler);
}

// 投げた位置の stack を覚えてから本来の __cxa_throw を呼ぶ。link 時に -Wl,--wrap=__cxa_throw で差し込む。
extern "C" void throwHook() {
    t_throwCount = static_cast<int>(RtlCaptureStackBackTrace(2, 62, t_throwFrames, nullptr));
}

}
}

extern "C" [[noreturn]] void __real___cxa_throw(void* obj, std::type_info* type, void (*dest)(void*));
extern "C" [[noreturn]] void __wrap___cxa_throw(void* obj, std::type_info* type, void (*dest)(void*)) {
    utagoe::log::throwHook();
    __real___cxa_throw(obj, type, dest);
}
