// x87 の丸め命令を直接使う補助。元実装の ftol_round (0x4b8650) は frndint 相当で、最近接偶数丸め。
// std::nearbyint(long double) は MinGW では浮動小数点環境を退避する重い library 呼び出しになるため、こちらを使う。
// 丸めモードは extract() が処理中に最近接へ固定している。

#ifndef UTAGOE_X87_H
#define UTAGOE_X87_H

#include <cmath>

namespace utagoe {

inline long double rint87(long double x) {
#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
    __asm__("frndint" : "=t"(x) : "0"(x));
    return x;
#else
    return std::nearbyint(x);
#endif
}

}

#endif
