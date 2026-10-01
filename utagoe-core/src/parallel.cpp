// 常駐 thread pool。block ごとの探索のように細かい仕事が何千回も来るので、毎回 thread を作らずに使い回す。
// worker thread は x87 の精度設定を呼び出し側から引き継ぐ (拡張精度の計算結果を thread で変えないため)。

#include "parallel.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace utagoe {
namespace {

unsigned short readFpuControl() {
#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
    unsigned short cw;
    __asm__ volatile("fnstcw %0" : "=m"(cw));
    return cw;
#else
    return 0;
#endif
}

void writeFpuControl([[maybe_unused]] unsigned short cw) {
#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
    __asm__ volatile("fldcw %0" : : "m"(cw));
#endif
}

class Pool {
public:
    Pool() {
        const unsigned hw = std::thread::hardware_concurrency();
        const int n = static_cast<int>(std::clamp(hw == 0 ? 1u : hw, 1u, 64u)) - 1;
        for (int i = 0; i < n; ++i) threads_.emplace_back([this] { loop(); });
    }

    ~Pool() {
        {
            std::lock_guard<std::mutex> lk(m_);
            quit_ = true;
        }
        cv_.notify_all();
        for (auto& t : threads_) t.join();
    }

    int size() const { return static_cast<int>(threads_.size()) + 1; }

    void run(long long n, long long chunk, const std::function<void(long long, long long)>& fn) {
        // 入れ子の呼び出しや同時呼び出しは、その場で直列に処理する。
        std::unique_lock<std::mutex> busy(runMutex_, std::try_to_lock);
        if (!busy.owns_lock() || threads_.empty()) {
            fn(0, n);
            return;
        }
        {
            std::lock_guard<std::mutex> lk(m_);
            fn_ = &fn;
            n_ = n;
            chunk_ = chunk;
            next_.store(0);
            active_ = static_cast<int>(threads_.size());
            fpu_ = readFpuControl();
            ++generation_;
        }
        cv_.notify_all();
        work();
        std::unique_lock<std::mutex> lk(m_);
        done_.wait(lk, [this] { return active_ == 0; });
        fn_ = nullptr;
    }

private:
    void work() {
        for (;;) {
            const long long b = next_.fetch_add(chunk_);
            if (b >= n_) break;
            (*fn_)(b, std::min(n_, b + chunk_));
        }
    }

    void loop() {
        unsigned long long seen = 0;
        for (;;) {
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait(lk, [&] { return quit_ || generation_ != seen; });
                if (quit_) return;
                seen = generation_;
            }
            writeFpuControl(fpu_);
            work();
            {
                std::lock_guard<std::mutex> lk(m_);
                if (--active_ == 0) done_.notify_one();
            }
        }
    }

    std::vector<std::thread> threads_;
    std::mutex m_, runMutex_;
    std::condition_variable cv_, done_;
    const std::function<void(long long, long long)>* fn_ = nullptr;
    long long n_ = 0, chunk_ = 1;
    std::atomic<long long> next_{0};
    int active_ = 0;
    unsigned short fpu_ = 0;
    unsigned long long generation_ = 0;
    bool quit_ = false;
};

// DLL の unload 時に static destructor から thread を join すると loader lock で止まることがあるため、pool は破棄しない。
Pool& pool() {
    static Pool* p = new Pool;
    return *p;
}

}

int workerCount() { return pool().size(); }

void parallelFor(long long n, long long minChunk, const std::function<void(long long, long long)>& fn) {
    if (n <= 0) return;
    if (minChunk < 1) minChunk = 1;
    Pool& p = pool();
    // 各 thread に数回ずつ回るくらいに分ける。小さすぎる仕事は分けない。
    const long long target = std::max(minChunk, (n + p.size() * 4 - 1) / (p.size() * 4));
    if (n <= minChunk || p.size() == 1) {
        fn(0, n);
        return;
    }
    p.run(n, target, fn);
}

}
