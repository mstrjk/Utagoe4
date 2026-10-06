#ifndef UTAGOE_CANCEL_H
#define UTAGOE_CANCEL_H

#include <atomic>

namespace utagoe {
namespace cancel {

inline std::atomic<bool>& flag() {
    static std::atomic<bool> f{false};
    return f;
}
inline void request() { flag().store(true, std::memory_order_relaxed); }
inline void reset() { flag().store(false, std::memory_order_relaxed); }
inline bool requested() { return flag().load(std::memory_order_relaxed); }

}
}

#endif
