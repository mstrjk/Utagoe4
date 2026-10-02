#ifndef UTAGOE_RC_INTERNAL_H
#define UTAGOE_RC_INTERNAL_H

#include "rc.h"

namespace utagoe {
namespace rc {

inline double absSq(const cd& z) { return z.real() * z.real() + z.imag() * z.imag(); }
inline float absSq(const cf& z) { return z.real() * z.real() + z.imag() * z.imag(); }

}
}

#endif
