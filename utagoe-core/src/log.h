// 処理の記録と stack trace。UI の terminal に流す行、段階 (stage) の開始と終了、上書き表示する進捗行を出す。
// callback が無ければ何もしない。どの thread から呼んでもよい。

#ifndef UTAGOE_LOG_H
#define UTAGOE_LOG_H

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

namespace utagoe {
namespace log {

// 種類は C API (utagoe_c.h の UTAGOE_LOG_*) と同じ値。
enum Kind { Line = 0, Status = 1, StageBegin = 2, StageEnd = 3, Warning = 4, Error = 5, Detail = 6 };

using Callback = void (*)(int32_t kind, int32_t id, const char* text, void* user);
void setCallback(Callback fn, void* user);
bool enabled();

void line(const char* fmt, ...) __attribute__((format(gnu_printf, 1, 2)));
void detail(const char* fmt, ...) __attribute__((format(gnu_printf, 1, 2)));
void warn(const char* fmt, ...) __attribute__((format(gnu_printf, 1, 2)));
void error(const std::string& message, const std::string& stack);

// 同じ id の行を上書きする進捗表示。force でなければ 100 ms に 1 回まで。
void status(int id, bool force, const char* fmt, ...) __attribute__((format(gnu_printf, 3, 4)));
void clearStatus(int id);

// 進捗行の文字列。"[####......]  42.0% | 1200/2400 blocks | 00:04 elapsed | ~00:05 left"
std::string progressBar(double fraction, int width = 24);
std::string clock(double seconds);

// 段階の開始と終了 (所要時間つき)。
class Stage {
public:
    explicit Stage(const std::string& name);
    ~Stage();
    double seconds() const;
    void fail() { failed_ = true; }
private:
    std::string name_;
    std::chrono::steady_clock::time_point start_;
    bool failed_ = false;
};

// 進捗の推定。経過時間と割合から残り時間を出す。
class Eta {
public:
    Eta() : start_(std::chrono::steady_clock::now()) {}
    double elapsed() const;
    // 割合が小さいうちは推定しない (戻り値 < 0)。
    double remaining(double fraction) const;
private:
    std::chrono::steady_clock::time_point start_;
};

class Bar {
public:
    explicit Bar(int id = 1);
    ~Bar();
    Bar(const Bar&) = delete;
    Bar& operator=(const Bar&) = delete;
    void update(double fraction, const std::string& what = "");
    void range(double from, double to);
    static void report(double fraction, const std::string& what = "");
    static void report(double done, double total, const std::string& what = "");
    static Bar* current();
private:
    int id_;
    Bar* previous_;
    Eta eta_;
    double from_ = 0.0, to_ = 1.0, shown_ = 0.0;
    std::string what_;
    std::mutex lock_;
};

// 今の位置の stack trace (skip 個の frame を飛ばす)。関数名は各 module の COFF symbol から引く。
std::string stackTrace(int skip = 0);
// 最後に投げられた C++ 例外の、投げた位置での stack trace (この thread)。
std::string lastThrowTrace();

// crash 記録の置き場所。設定すると、この DLL 群の中で起きたアクセス違反などを記録する。
void setCrashDirectory(const std::string& utf8Dir);

}
}

#endif
