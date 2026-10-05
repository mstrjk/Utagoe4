#ifndef UTAGOE_STEPCACHE_H
#define UTAGOE_STEPCACHE_H

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace utagoe {
namespace stepcache {

void configure(bool enabled);
bool enabled();
void clear();
std::string folder();

void setSources(const std::string& original, const std::string& instrumental);

std::uint64_t hash(const std::vector<float>& v);
std::string key(const char* step, std::initializer_list<std::uint64_t> parts, const std::string& extra = "");

class Writer {
public:
    void i64(long long v);
    void f64(double v);
    void str(const std::string& v);
    void floats(const std::vector<float>& v);
    void doubles(const std::vector<double>& v);
    void chars(const std::vector<char>& v);
    const std::string& data() const { return buf_; }
private:
    void raw(const void* p, std::size_t n) { buf_.append(static_cast<const char*>(p), n); }
    std::string buf_;
};

class Reader {
public:
    long long i64();
    double f64();
    std::string str();
    std::vector<float> floats();
    std::vector<double> doubles();
    std::vector<char> chars();
    bool ok() const { return ok_ && pos_ <= buf_.size(); }
    std::string& buffer() { return buf_; }
private:
    bool raw(void* p, std::size_t n);
    std::string buf_;
    std::size_t pos_ = 0;
    bool ok_ = true;
};

enum class Source { Original, Instrumental };

bool load(const char* step, Source from, const std::string& key, Reader& r);
void save(const char* step, Source from, const std::string& key, const Writer& w);

}
}

#endif
