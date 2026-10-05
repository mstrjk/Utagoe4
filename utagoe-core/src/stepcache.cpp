#include "stepcache.h"
#include "fileio.h"

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <system_error>

#ifdef _WIN32
#  include <shlobj.h>
#endif

namespace utagoe {
namespace stepcache {
namespace fs = std::filesystem;
namespace {

constexpr char kMagic[8] = {'U', 'T', 'G', 'S', 'T', 'E', 'P', '1'};
constexpr std::uintmax_t kCapacity = std::uintmax_t(4) << 30;

std::mutex lock;
bool on = true;
thread_local std::string originalPath, instrumentalPath;

fs::path folderPath() {
#ifdef _WIN32
    wchar_t custom[1024];
    const DWORD got = GetEnvironmentVariableW(L"UTAGOE_CACHE_DIR", custom, 1024);
    if (got > 0 && got < 1024) return fs::path(custom);
    PWSTR music = nullptr;
    fs::path base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Music, 0, nullptr, &music)) && music) base = fs::path(music);
    if (music) CoTaskMemFree(music);
    if (base.empty()) return {};
    return base / L"Utagoe" / L"cache";
#else
    return {};
#endif
}

std::wstring pick(const std::wstring& s, bool head) {
    std::wstring out = head ? s.substr(0, std::min<std::size_t>(3, s.size())) : s.substr(s.size() - std::min<std::size_t>(3, s.size()));
    for (wchar_t& c : out)
        if (c < 32 || std::wcschr(L"\\/:*?\"<>|", c)) c = L'_';
    return out;
}

fs::path fileFor(const char* step, Source from) {
    const std::string& src = from == Source::Original ? originalPath : instrumentalPath;
    if (src.empty()) return {};
    const fs::path dir = folderPath();
    if (dir.empty()) return {};
#ifdef _WIN32
    const fs::path p(widenUtf8(src));
#else
    const fs::path p(src);
#endif
    std::wstring stem = p.stem().wstring(), ext = p.extension().wstring();
    if (!ext.empty() && ext[0] == L'.') ext.erase(0, 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    std::wstring name;
    for (const char* c = step; *c; ++c) name += static_cast<wchar_t>(*c);
    name += L"_" + pick(stem, true) + L"-" + pick(stem, false) + L"-" + pick(ext, true) + L".ucache";
    return dir / name;
}

void trim(const fs::path& dir) {
    std::error_code ec;
    struct Item { fs::path p; fs::file_time_type t; std::uintmax_t size; };
    std::vector<Item> items;
    std::uintmax_t total = 0;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (e.path().extension() != L".ucache") continue;
        const auto size = e.file_size(ec);
        if (ec) continue;
        items.push_back({e.path(), e.last_write_time(ec), size});
        total += size;
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.t < b.t; });
    for (const Item& i : items) {
        if (total <= kCapacity) break;
        if (fs::remove(i.p, ec)) total -= i.size;
    }
}

}

void configure(bool enabled) {
    std::lock_guard<std::mutex> g(lock);
    on = enabled;
}

bool enabled() {
    std::lock_guard<std::mutex> g(lock);
    return on;
}

void clear() {
    std::lock_guard<std::mutex> g(lock);
    const fs::path dir = folderPath();
    if (dir.empty()) return;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.path().extension() == L".ucache") fs::remove(e.path(), ec);
}

std::string folder() {
    const fs::path dir = folderPath();
#ifdef _WIN32
    const std::wstring w = dir.wstring();
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
#else
    return dir.string();
#endif
}

void setSources(const std::string& original, const std::string& instrumental) {
    originalPath = original;
    instrumentalPath = instrumental;
}

std::uint64_t hash(const std::vector<float>& v) {
    std::uint64_t h = 0xcbf29ce484222325ull ^ (v.size() * 0x9e3779b97f4a7c15ull);
    std::uint64_t a = 0x9e3779b97f4a7c15ull, b = 0xc2b2ae3d27d4eb4full;
    const std::size_t n = v.size();
    std::size_t i = 0;
    for (; i + 1 < n; i += 2) {
        std::uint32_t x, y;
        std::memcpy(&x, &v[i], 4);
        std::memcpy(&y, &v[i + 1], 4);
        a = (a ^ x) * 0x100000001b3ull;
        b = (b ^ y) * 0x100000001b3ull;
    }
    if (i < n) {
        std::uint32_t x;
        std::memcpy(&x, &v[i], 4);
        a = (a ^ x) * 0x100000001b3ull;
    }
    h ^= a + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    h ^= b + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h;
}

std::string key(const char* step, std::initializer_list<std::uint64_t> parts, const std::string& extra) {
    std::string k = step;
    for (std::uint64_t p : parts) k += "|" + std::to_string(p);
    if (!extra.empty()) k += "|" + extra;
    return k;
}

void Writer::i64(long long v) { raw(&v, sizeof v); }
void Writer::f64(double v) { raw(&v, sizeof v); }
void Writer::str(const std::string& v) { i64(static_cast<long long>(v.size())); raw(v.data(), v.size()); }
void Writer::floats(const std::vector<float>& v) { i64(static_cast<long long>(v.size())); raw(v.data(), v.size() * sizeof(float)); }
void Writer::doubles(const std::vector<double>& v) { i64(static_cast<long long>(v.size())); raw(v.data(), v.size() * sizeof(double)); }
void Writer::chars(const std::vector<char>& v) { i64(static_cast<long long>(v.size())); raw(v.data(), v.size()); }

bool Reader::raw(void* p, std::size_t n) {
    if (!ok_ || pos_ + n > buf_.size()) {
        ok_ = false;
        return false;
    }
    std::memcpy(p, buf_.data() + pos_, n);
    pos_ += n;
    return true;
}

long long Reader::i64() { long long v = 0; raw(&v, sizeof v); return v; }
double Reader::f64() { double v = 0; raw(&v, sizeof v); return v; }

std::string Reader::str() {
    const long long n = i64();
    if (n < 0 || static_cast<std::size_t>(n) > buf_.size() - std::min(pos_, buf_.size())) { ok_ = false; return {}; }
    std::string v(static_cast<std::size_t>(n), '\0');
    raw(v.data(), v.size());
    return v;
}

std::vector<float> Reader::floats() {
    const long long n = i64();
    if (n < 0 || static_cast<std::size_t>(n) > (buf_.size() - std::min(pos_, buf_.size())) / sizeof(float)) { ok_ = false; return {}; }
    std::vector<float> v(static_cast<std::size_t>(n));
    raw(v.data(), v.size() * sizeof(float));
    return v;
}

std::vector<double> Reader::doubles() {
    const long long n = i64();
    if (n < 0 || static_cast<std::size_t>(n) > (buf_.size() - std::min(pos_, buf_.size())) / sizeof(double)) { ok_ = false; return {}; }
    std::vector<double> v(static_cast<std::size_t>(n));
    raw(v.data(), v.size() * sizeof(double));
    return v;
}

std::vector<char> Reader::chars() {
    const long long n = i64();
    if (n < 0 || static_cast<std::size_t>(n) > buf_.size() - std::min(pos_, buf_.size())) { ok_ = false; return {}; }
    std::vector<char> v(static_cast<std::size_t>(n));
    raw(v.data(), v.size());
    return v;
}

bool load(const char* step, Source from, const std::string& key, Reader& r) {
    if (!enabled()) return false;
    const fs::path p = fileFor(step, from);
    if (p.empty()) return false;
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    char magic[8];
    long long n = 0;
    if (!in.read(magic, 8) || std::memcmp(magic, kMagic, 8) != 0 || !in.read(reinterpret_cast<char*>(&n), sizeof n) ||
        n != static_cast<long long>(key.size()))
        return false;
    std::string stored(static_cast<std::size_t>(n), '\0');
    if (!in.read(stored.data(), n) || stored != key) return false;
    std::error_code ec;
    const auto size = fs::file_size(p, ec);
    if (ec) return false;
    const std::uintmax_t header = 8 + sizeof n + static_cast<std::uintmax_t>(n);
    std::string& buf = r.buffer();
    buf.assign(static_cast<std::size_t>(size - header), '\0');
    if (!in.read(buf.data(), static_cast<std::streamsize>(buf.size()))) return false;
    fs::last_write_time(p, fs::file_time_type::clock::now(), ec);
    return true;
}

void save(const char* step, Source from, const std::string& key, const Writer& w) {
    if (!enabled()) return;
    const fs::path p = fileFor(step, from);
    if (p.empty()) return;
    std::lock_guard<std::mutex> g(lock);
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        const long long n = static_cast<long long>(key.size());
        out.write(kMagic, 8);
        out.write(reinterpret_cast<const char*>(&n), sizeof n);
        out.write(key.data(), n);
        out.write(w.data().data(), static_cast<std::streamsize>(w.data().size()));
        if (!out) {
            out.close();
            fs::remove(tmp, ec);
            return;
        }
    }
    fs::rename(tmp, p, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return;
    }
    trim(p.parent_path());
}

}
}
