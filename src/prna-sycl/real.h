#ifndef REAL_H
#define REAL_H

#include <math.h>
#if defined FLOAT

typedef float real_t;
#define RF "%.6g"
#define RCONST(x) x##f
#define ALMOST_ZERO RCONST(1e-7)
#define STR_TO_REAL(x,y) strtof(x,y)
#define MATHFN(x) x##f

#elif defined DOUBLE

typedef double real_t;
#define RF "%.14lg"
#define RCONST(x) x
#define ALMOST_ZERO RCONST(1e-15)
#define STR_TO_REAL(x,y) strtod(x,y)
#define MATHFN(x) x

#elif defined LONG_DOUBLE

typedef long double real_t;
#define RF "%.18Lg"
#define RCONST(x) x##L
#define ALMOST_ZERO RCONST(1e-19)
#define STR_TO_REAL(x,y) strtold(x,y)
#define MATHFN(x) x##l

#else

#error Must define one of FLOAT, DOUBLE, or LONG_DOUBLE

#endif

#ifdef __CUDACC__
#include <sycl/sycl.hpp>
#define LOG   sycl::log
#define EXP   sycl::exp
#define LOG1P sycl::log1p
#define COS   sycl::cos
#define SIN   sycl::sin
#define SQRT  sycl::sqrt
#define RINT  sycl::rint
#define FABS  sycl::fabs
#define ABS   sycl::abs

#else

#define LOG MATHFN(log)
#define EXP MATHFN(exp)
#define LOG1P MATHFN(log1p)
#define COS MATHFN(cos)
#define SIN MATHFN(sin)
#define SQRT MATHFN(sqrt)
#define RINT MATHFN(rint)
#define FABS MATHFN(fabs)
// integer math function
#define ABS  abs

#endif

#define HALF RCONST(0.5)
#define PI RCONST(3.1415926535897932384626433832795029)
#define INF (-LOG(RCONST(0.)))
#define NOT_A_NUMBER (LOG(RCONST(-1.)))

static inline real_t sq(real_t x) { return x*x; }

#endif /* REAL_H */
