/*
 *  TCC runtime library for 32-bit PowerPC (Apple Mach-O).
 *
 *  Provides the libgcc helpers that the PPC backend emits calls to
 *  but that aren't supplied by the system.
 *
 *  All helpers use explicit 32-bit arithmetic rather than the DWunion
 *  struct trick from libtcc1.c, because PPC is big-endian: a
 *  "struct { int low, high; }" has `low` at the lower address, which
 *  is the HIGH word of a big-endian long long — the naming in libtcc1.c
 *  is backwards for PPC. Simpler to just use (x >> 32) / (unsigned int)x
 *  throughout.
 *
 *  Copying and distribution of this file, with or without modification,
 *  are permitted in any medium without royalty provided the copyright
 *  notice and this notice are preserved. This file is offered as-is,
 *  without any warranty.
 */

/* ---------------------------------------------------------------------------
 * float helpers
 * ---------------------------------------------------------------------------*/

/* Convert by splitting into two 32-bit unsigned halves so we don't
 * recursively reach for __floatundidf. (uint32 -> double goes through
 * gen_cvt_itof's magic-constant inline expansion, no libcall.) */
double __floatundidf(unsigned long long x)
{
    double hi = (double)(unsigned int)(x >> 32);
    double lo = (double)(unsigned int)x;
    return hi * 4294967296.0 + lo;
}

/* Signed 64-bit -> double. Sign-split, then forward to the unsigned
 * version. tcc's PPC backend emits a call to __floatdidf for any
 * `(double)(long long)x` that doesn't reduce to constants. */
double __floatdidf(long long x)
{
    if (x < 0)
        return -(double)__floatundidf((unsigned long long)-x);
    return (double)__floatundidf((unsigned long long)x);
}

/* Signed 64-bit -> float. Same sign-split + forward pattern. The
 * round-trip through double doesn't lose precision relative to the
 * single-precision result because float has 24 bits of mantissa and
 * we're doing the conversion at full double precision first. */
float __floatdisf(long long x)
{
    return (float)__floatdidf(x);
}

/* Unsigned 64-bit -> float. Same as the double version but truncated. */
float __floatundisf(unsigned long long x)
{
    return (float)__floatundidf(x);
}

/* Forward declarations of helpers defined further down the file
 * but referenced by the LD <-> LL conversion stubs that need to
 * appear close to their float counterparts. */
long long __fixdfdi(double);
unsigned long long __fixunsdfdi(double);

/* (un)signed 64-bit -> long double. Tcc's generic gen_cvt_itof1
 * emits these names for LL/ULL -> LD when LDOUBLE_SIZE != 8.
 * Lossy: produce LD with high = (double)x, low = 0. The tcc-on-PPC
 * conversion (long double)d goes through our gen_cvt_ftof which
 * fills the LD pair (high = d, low = 0). */
long double __floatundixf(unsigned long long x)
{
    return (long double)__floatundidf(x);
}
long double __floatdixf(long long x)
{
    return (long double)__floatdidf(x);
}

/* long double -> (un)signed 64-bit. Tcc's generic gen_cvt_ftoi1
 * emits these for LD -> LL/ULL conversions when LDOUBLE_SIZE != 8.
 * Lossy: discard the low half of the LD double-double, then
 * convert the high double via existing __fixdfdi / __fixunsdfdi. */
long long __fixxfdi(long double x)
{
    return __fixdfdi((double)x);
}
unsigned long long __fixunsxfdi(long double x)
{
    return __fixunsdfdi((double)x);
}

/* double -> unsigned long long.
 *
 * IEEE 754 double layout (big-endian PPC, 8 bytes):
 *   bit 63      : sign
 *   bits 62-52  : biased exponent (bias = 1023)
 *   bits 51-0   : fraction (implicit leading 1)
 *
 * We access the bits via a union of two unsigned ints. PPC is big-endian,
 * so w[0] holds the high 32 bits (sign + exponent + top 20 fraction bits)
 * and w[1] holds the low 32 bits (bottom 32 fraction bits).
 *
 * All bit-assembly is done with 32-bit values to avoid calling __ashldi3 /
 * __lshrdi3 recursively. */
unsigned long long __fixunsdfdi(double a)
{
    union { double d; unsigned int w[2]; } u;
    unsigned int hi, lo, sig_hi, sig_lo;
    unsigned int res_hi, res_lo;
    int exp, sh;

    u.d = a;
    /* PPC big-endian: w[0] = high 32 bits, w[1] = low 32 bits */
    hi = u.w[0];
    lo = u.w[1];

    if (hi & 0x80000000u)   /* negative -> 0 */
        return 0;

    exp = (int)((hi >> 20) & 0x7FF) - 1023;
    if (exp < 0)
        return 0;
    if (exp >= 64)          /* overflow: saturate to ULLONG_MAX */
        return ~(unsigned long long)0;

    /* The 53-bit significand Q = (1 << 52) | fraction.
     * Represent it as two halves:
     *   sig_hi = bits [52..32] = 21 bits (bit 20 is the implicit 1)
     *   sig_lo = bits [31..0]  = 32 bits
     * so Q = (sig_hi << 32) | sig_lo (conceptually a 64-bit value). */
    sig_hi = (hi & 0x000FFFFFu) | 0x00100000u;
    sig_lo = lo;

    /* result = Q * 2^(exp - 52)
     * = Q >> (52 - exp)  if exp <= 52
     * = Q << (exp - 52)  if exp >  52 */

    if (exp <= 52) {
        sh = 52 - exp;
        /* right-shift Q (53-bit) by sh in [0..52] */
        if (sh == 0) {
            /* Q fits exactly: sig_hi is 21 bits (bits 52..32), sig_lo is 31..0 */
            res_hi = sig_hi;
            res_lo = sig_lo;
        } else if (sh >= 32) {
            /* sh in [32..52]: sig_lo is completely shifted out */
            res_hi = 0;
            res_lo = sig_hi >> (sh - 32);
        } else {
            /* sh in [1..31] */
            res_hi = sig_hi >> sh;
            res_lo = (sig_lo >> sh) | (sig_hi << (32 - sh));
        }
    } else {
        sh = exp - 52;
        /* left-shift Q (53-bit) by sh in [1..11] (exp <= 63, so sh <= 11) */
        /* sh < 32 always holds here */
        res_hi = (sig_hi << sh) | (sig_lo >> (32 - sh));
        res_lo = sig_lo << sh;
    }
    return ((unsigned long long)res_hi << 32) | (unsigned long long)res_lo;
}

/* double -> signed long long.
 *
 * We deliberately avoid unary negation of the double argument because
 * tcc-on-PPC has a codegen bug where `fneg` on a parameter double can
 * corrupt the low bits (the sign bit is not always correctly flipped).
 * Instead, we flip the sign bit manually in the raw bit representation. */
long long __fixdfdi(double a)
{
    union { double d; unsigned int w[2]; } u;
    unsigned int hi;
    int negative;

    u.d = a;
    hi = u.w[0];                        /* PPC big-endian: w[0] = high 32 bits */
    negative = (hi & 0x80000000u) != 0;

    if (negative) {
        /* Clear sign bit to get |a|, then convert unsigned, then negate. */
        u.w[0] = hi & ~0x80000000u;
        return -(long long)__fixunsdfdi(u.d);
    }
    return (long long)__fixunsdfdi(a);
}

/* float -> long long / unsigned long long: widen to double then
 * forward. Float fits exactly in double, so no precision loss. */
long long __fixsfdi(float a) { return __fixdfdi((double)a); }
unsigned long long __fixunssfdi(float a) { return __fixunsdfdi((double)a); }

/* gcc-style math builtins. Tiger's <math.h> for PPC declares
 * functions via these (e.g. /usr/include/architecture/ppc/math.h
 * uses __builtin_fabs / __builtin_inf inside macros). gcc treats
 * them as compiler intrinsics and inlines; tcc emits regular
 * function calls. Provide thin C wrappers. */
double __builtin_fabs(double x) { return x < 0 ? -x : x; }
float  __builtin_fabsf(float x) { return x < 0 ? -x : x; }
double __builtin_inf(void) {
    /* IEEE 754 +Inf is exponent=2047, mantissa=0, sign=0 */
    union { double d; unsigned long long u; } u;
    u.u = 0x7FF0000000000000ULL;
    return u.d;
}
float __builtin_inff(void) {
    union { float f; unsigned u; } u;
    u.u = 0x7F800000u;
    return u.f;
}
/* __builtin_isnan / __builtin_isinf / __builtin_isfinite — gcc treats
 * these as type-generic intrinsics that inline to FP register checks.
 * tcc emits a regular call. Provide thin C implementations covering the
 * float / double / long-double overloads gcc inlines. (Long double on
 * Apple PPC is IBM double-double; NaN-ness lives in the high half.)
 *
 * Tiger's <math.h> uses __builtin_isnan inside the isnan() macro:
 *   #define isnan(x) (__builtin_isnan(x))
 * — so any program that #includes <math.h> and calls isnan() will
 * unresolved-link without these stubs. Same for isinf / isfinite. */
int __builtin_isnan(double x) {
    union { double d; unsigned long long u; } u;
    u.d = x;
    return ((u.u >> 52) & 0x7ff) == 0x7ff && (u.u & 0xfffffffffffffull) != 0;
}
int __builtin_isnanf(float x) {
    union { float f; unsigned u; } u;
    u.f = x;
    return ((u.u >> 23) & 0xff) == 0xff && (u.u & 0x7fffffu) != 0;
}
int __builtin_isnanl(long double x) { return __builtin_isnan((double)x); }
int __builtin_isinf(double x) {
    union { double d; unsigned long long u; } u;
    u.d = x;
    return ((u.u >> 52) & 0x7ff) == 0x7ff && (u.u & 0xfffffffffffffull) == 0;
}
int __builtin_isinff(float x) {
    union { float f; unsigned u; } u;
    u.f = x;
    return ((u.u >> 23) & 0xff) == 0xff && (u.u & 0x7fffffu) == 0;
}
int __builtin_isinfl(long double x) { return __builtin_isinf((double)x); }
int __builtin_isfinite(double x) { return !(__builtin_isnan(x) || __builtin_isinf(x)); }
int __builtin_isfinitef(float x)  { return !(__builtin_isnanf(x) || __builtin_isinff(x)); }
int __builtin_isfinitel(long double x) { return __builtin_isfinite((double)x); }
/* IBM double-double helpers (__gcc_qadd / qsub / qmul / qdiv).
 *
 * Apple PPC's `long double` is 128-bit IBM double-double (a pair of
 * doubles). gcc-4.0 on Tiger emits calls to these helpers for any
 * `long double` arithmetic; tcc emits the same calls (see
 * `gen_opf` in ppc-gen.c).
 *
 * Apple's libgcc-style ABI for these helpers: input is two `long
 * double` values (each = pair of doubles in FPR pair), output is
 * `long double` (returned in f1, f2). gcc-4.0's source declaration
 * uses 4 doubles in / struct{double,double} out, but the actual
 * register-level ABI matches the long-double convention because
 * Apple PPC32 returns LD in (f1, f2).
 *
 * Implementation: precision-correct (not lossy) IBM double-double
 * arithmetic via Knuth Two-Sum + Veltkamp split + Dekker product.
 * No fma dependency (PPC has fmadd as an instruction but tcc-on-PPC
 * doesn't emit it; calling libm's fma() would create a libm
 * dependency we'd rather avoid). The Veltkamp split costs a few
 * extra multiplies per two_prod but is otherwise comparable.
 *
 * References: Hida/Li/Bailey "Library for Double-Double and
 * Quad-Double Arithmetic" (2007); Dekker "A Floating-Point
 * Technique for Extending the Available Precision" (1971);
 * Knuth TAOCP vol 2 §4.2.2 (TwoSum). */

/* Memory layout of an Apple PPC `long double` is (high double at
 * offset 0, low double at offset 8). Cast through a union to
 * extract / pack the pair. We avoid type-punning via raw pointer
 * cast because tcc's optimizer is a non-issue but a union read is
 * still cleaner. */
typedef union {
    struct { double hi, lo; } p;
    long double ld;
} dd_union;

/* TwoSum(a, b): exact representation of a+b as (s, e) where
 * s = round(a+b) and s+e = a+b exactly (in infinite precision).
 * Knuth's algorithm — works for any a, b. */
static void dd_two_sum(double a, double b, double *s, double *e)
{
    double bb;
    *s = a + b;
    bb = *s - a;
    *e = (a - (*s - bb)) + (b - bb);
}

/* QuickTwoSum(a, b): like TwoSum but requires |a| >= |b|. Saves
 * three flops; used to renormalize after we've already computed
 * the high-magnitude sum. */
static void dd_quick_two_sum(double a, double b, double *s, double *e)
{
    *s = a + b;
    *e = b - (*s - a);
}

/* Veltkamp split: divides a 53-bit double into two halves
 * (hi: top ~27 bits, lo: bottom ~26 bits) such that hi + lo == a
 * exactly. The split constant 2^27 + 1 is exact. */
static void dd_split(double a, double *hi, double *lo)
{
    /* 2^27 + 1 = 134217729 */
    double t = a * 134217729.0;
    double t1 = t - a;
    *hi = t - t1;
    *lo = a - *hi;
}

/* TwoProd(a, b): exact representation of a*b as (p, e) where
 * p = round(a*b) and p+e = a*b exactly. With PPC's hardware fmadd
 * (exposed via tcc's __builtin_fma since v0.2.36), the algorithm
 * collapses to two flops:
 *   p = a*b           (rounded)
 *   e = fma(a, b, -p) (= a*b - p, exact thanks to single rounding step)
 * vs. the ~10-flop Veltkamp+Dekker fallback for arches without fma. */
static void dd_two_prod(double a, double b, double *p, double *e)
{
    *p = a * b;
    *e = __builtin_fma(a, b, -*p);
}

/* DD addition. Sloppy variant from Hida/Li/Bailey — the IEEE-754
 * faithfully-rounded result for ordinary inputs; can deviate by
 * <= 2 ulp in the final low half for nasty cases (acceptable for
 * abitest and matches gcc-4.0's libgcc for the cases we tested). */
long double __gcc_qadd(long double x, long double y)
{
    dd_union ux, uy, ur;
    double s, e;
    ux.ld = x;
    uy.ld = y;
    dd_two_sum(ux.p.hi, uy.p.hi, &s, &e);
    e += (ux.p.lo + uy.p.lo);
    dd_quick_two_sum(s, e, &ur.p.hi, &ur.p.lo);
    return ur.ld;
}

long double __gcc_qsub(long double x, long double y)
{
    dd_union ux, uy, ur;
    double s, e;
    ux.ld = x;
    uy.ld = y;
    dd_two_sum(ux.p.hi, -uy.p.hi, &s, &e);
    e += (ux.p.lo - uy.p.lo);
    dd_quick_two_sum(s, e, &ur.p.hi, &ur.p.lo);
    return ur.ld;
}

long double __gcc_qmul(long double x, long double y)
{
    dd_union ux, uy, ur;
    double p, e;
    ux.ld = x;
    uy.ld = y;
    dd_two_prod(ux.p.hi, uy.p.hi, &p, &e);
    /* Cross terms: hi*lo + lo*hi. Lo*lo is below ~2^-104, dropped. */
    e += ux.p.hi * uy.p.lo + ux.p.lo * uy.p.hi;
    dd_quick_two_sum(p, e, &ur.p.hi, &ur.p.lo);
    return ur.ld;
}

/* DD division via long division: q1 = xhi/yhi (rounded), then
 * compute the residual (x - q1*y) and refine with q2 = (residual
 * hi)/yhi. Two iterations are enough for ~104 bits of precision. */
long double __gcc_qdiv(long double x, long double y)
{
    dd_union ux, uy, ur, urem;
    double q1, q2, p, e, rhi, rlo;
    long double q1_dd, q2b;

    ux.ld = x;
    uy.ld = y;

    q1 = ux.p.hi / uy.p.hi;

    /* Compute q1 * y as a dd value. q1 is one double, y is dd. */
    dd_two_prod(q1, uy.p.hi, &p, &e);
    e += q1 * uy.p.lo;
    dd_quick_two_sum(p, e, &rhi, &rlo);
    /* Hand-pack into a dd_union for re-use as a long double. */
    {
        dd_union tmp;
        tmp.p.hi = rhi;
        tmp.p.lo = rlo;
        q1_dd = tmp.ld;
    }

    /* x - q1*y, again as dd. */
    q2b = __gcc_qsub(x, q1_dd);
    urem.ld = q2b;
    q2 = urem.p.hi / uy.p.hi;

    /* Combine: result = q1 + q2 (precise sum). */
    dd_quick_two_sum(q1, q2, &ur.p.hi, &ur.p.lo);
    return ur.ld;
}

/* Long-double comparison helpers (libgcc tf2 family). For canonical
 * dd values (hi is the rounded-double of the true value, lo is the
 * remainder satisfying |lo| <= ulp(hi)/2), comparison is "compare
 * highs; on tie, compare lows." Our arithmetic functions all
 * produce canonical dd outputs via dd_quick_two_sum, so this
 * comparison is exact for any value that flowed through them. */
int __eqtf2(long double x, long double y)
{
    dd_union ux, uy;
    ux.ld = x; uy.ld = y;
    return (ux.p.hi == uy.p.hi && ux.p.lo == uy.p.lo) ? 0 : 1;
}
int __netf2(long double x, long double y)
{
    dd_union ux, uy;
    ux.ld = x; uy.ld = y;
    return (ux.p.hi == uy.p.hi && ux.p.lo == uy.p.lo) ? 0 : 1;
}
int __lttf2(long double x, long double y)
{
    dd_union ux, uy;
    ux.ld = x; uy.ld = y;
    if (ux.p.hi != uy.p.hi)
        return ux.p.hi < uy.p.hi ? -1 : 1;
    if (ux.p.lo != uy.p.lo)
        return ux.p.lo < uy.p.lo ? -1 : 1;
    return 0;
}
int __letf2(long double x, long double y) { return __lttf2(x, y); }
int __gttf2(long double x, long double y) { return __lttf2(x, y); }
int __getf2(long double x, long double y) { return __lttf2(x, y); }

/* 64-bit comparison helpers also from libgcc. Same situation as
 * __gcc_q*: gcc-4.0-built objects in libtcc.a reference these and
 * Tiger libc doesn't ship them. Standard libgcc semantics: returns
 * 0/1/2 = a<b / a==b / a>b. */
int __cmpdi2(long long a, long long b) {
    return (a < b) ? 0 : (a == b) ? 1 : 2;
}
int __ucmpdi2(unsigned long long a, unsigned long long b) {
    return (a < b) ? 0 : (a == b) ? 1 : 2;
}

/* ---------------------------------------------------------------------------
 * 64-bit shift helpers
 *
 * All use explicit high/low splitting to avoid the DWunion endianness trap.
 * "high" means the most-significant 32 bits (bits 63-32).
 * "low"  means the least-significant 32 bits (bits 31-0).
 * ---------------------------------------------------------------------------*/

long long __ashldi3(long long a, int b)
{
    unsigned int hi = (unsigned int)((unsigned long long)a >> 32);
    unsigned int lo = (unsigned int)a;
    unsigned int rhi, rlo;

    if (b == 0) {
        rhi = hi; rlo = lo;
    } else if (b < 32) {
        rhi = (hi << b) | (lo >> (32 - b));
        rlo = lo << b;
    } else {
        rhi = lo << (b - 32);
        rlo = 0;
    }
    return ((unsigned long long)rhi << 32) | (unsigned long long)rlo;
}

unsigned long long __lshrdi3(unsigned long long a, int b)
{
    unsigned int hi = (unsigned int)(a >> 32);
    unsigned int lo = (unsigned int)a;
    unsigned int rhi, rlo;

    if (b == 0) {
        rhi = hi; rlo = lo;
    } else if (b < 32) {
        rlo = (lo >> b) | (hi << (32 - b));
        rhi = hi >> b;
    } else {
        rlo = hi >> (b - 32);
        rhi = 0;
    }
    return ((unsigned long long)rhi << 32) | (unsigned long long)rlo;
}

/* ---------------------------------------------------------------------------
 * 64-bit unsigned divide/modulo
 *
 * Simple 64-step shift-and-subtract long division.  Not fast, but
 * correct and dependency-free (no 64-bit multiply needed).
 * ---------------------------------------------------------------------------*/

static unsigned long long __udivmoddi4_ppc(unsigned long long n,
                                            unsigned long long d,
                                            unsigned long long *rem)
{
    unsigned long long q = 0;
    unsigned long long r = 0;
    int i;

    if (d == 0) {
        /* Division by zero: match libgcc behaviour (return 0 / set rem=n). */
        if (rem) *rem = n;
        return 0;
    }

    for (i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) {
            r -= d;
            q |= (unsigned long long)1 << i;
        }
    }
    if (rem) *rem = r;
    return q;
}

unsigned long long __udivdi3(unsigned long long u, unsigned long long v)
{
    return __udivmoddi4_ppc(u, v, (unsigned long long *)0);
}

unsigned long long __umoddi3(unsigned long long u, unsigned long long v)
{
    unsigned long long r;
    __udivmoddi4_ppc(u, v, &r);
    return r;
}

/* ---------------------------------------------------------------------------
 * 64-bit signed divide/modulo
 *
 * Reduce to the unsigned helpers by splitting off the sign. The C99 rule
 * for signed integer division is "truncation toward zero", which means:
 *   sign(quot) = sign(num) ^ sign(den)
 *   sign(rem)  = sign(num)
 * (i.e., the remainder takes the sign of the dividend).
 * ---------------------------------------------------------------------------*/

long long __divdi3(long long u, long long v)
{
    int neg = 0;
    unsigned long long uu, vv, q;
    if (u < 0) { uu = (unsigned long long)-u; neg ^= 1; } else { uu = (unsigned long long)u; }
    if (v < 0) { vv = (unsigned long long)-v; neg ^= 1; } else { vv = (unsigned long long)v; }
    q = __udivmoddi4_ppc(uu, vv, (unsigned long long *)0);
    return neg ? -(long long)q : (long long)q;
}

long long __moddi3(long long u, long long v)
{
    int neg = 0;
    unsigned long long uu, vv, r;
    if (u < 0) { uu = (unsigned long long)-u; neg = 1; } else { uu = (unsigned long long)u; }
    if (v < 0) { vv = (unsigned long long)-v; }            else { vv = (unsigned long long)v; }
    __udivmoddi4_ppc(uu, vv, &r);
    return neg ? -(long long)r : (long long)r;
}

/* ---------------------------------------------------------------------------
 * 64-bit arithmetic right shift
 *
 * tcc's PPC backend can emit __ashrdi3 calls for `(signed long long) >> n`.
 * Sign-extend through the high word.
 * ---------------------------------------------------------------------------*/

long long __ashrdi3(long long a, int b)
{
    int hi = (int)((unsigned long long)a >> 32);    /* signed high word */
    unsigned int lo = (unsigned int)a;
    int rhi;
    unsigned int rlo;

    if (b == 0) {
        rhi = hi; rlo = lo;
    } else if (b < 32) {
        rlo = (lo >> b) | ((unsigned int)hi << (32 - b));
        rhi = hi >> b;          /* arithmetic shift on signed int */
    } else {
        rlo = (unsigned int)(hi >> (b - 32));
        rhi = hi >> 31;         /* sign-extend */
    }
    return ((unsigned long long)(unsigned int)rhi << 32) | (unsigned long long)rlo;
}

/* (Earlier sessions defined a __tcc_start_main shim here. It's now
 * obsolete: the tcc -o exe path auto-injects an equivalent helper in
 * tccelf.c::tcc_output_file when crt1.o isn't available, and the
 * gcc-link bootstrap path pulls in the real /usr/lib/crt1.o which
 * does the job properly.) */

/* ---------------------------------------------------------------------------
 * libgcc/libsystem-ish helpers expected by Tiger headers
 * ---------------------------------------------------------------------------*/

/* Tiger's <assert.h> expands assert(e) to call __eprintf when the
 * expression is false. gcc's libgcc provides this; libSystem on Tiger
 * does not. Provide a minimal implementation that matches the libgcc
 * behavior (print to stderr, then abort). */
extern int fprintf(void *stream, const char *fmt, ...);
extern void *__stderrp;     /* libSystem's stderr backing FILE * */
extern int fflush(void *);
extern void abort(void);

void __eprintf(const char *fmt, const char *file, unsigned line,
               const char *expr)
{
    fprintf(__stderrp, fmt, file, line, expr);
    fflush(__stderrp);
    abort();
}

/* ---------------------------------------------------------------------------
 * Bound-check helpers — no-op stubs.
 *
 * tcc's codegen with `-b` inserts calls to these on every pointer
 * arithmetic, function entry/exit, etc. The full bcheck.c
 * implementation in libtcc1 isn't ported to PPC yet (would need
 * hash tables, region tracking, signal hooks). For programs that
 * don't actually want runtime bounds checking but were built with
 * `-b` because the test harness sets it: provide no-op stubs so
 * the binary at least links and runs.
 *
 * Callers that EXPECT bounds checking (tests 112_backtrace,
 * 114_bound_signal, 126_bound_global, 132_bound_test) still won't
 * pass — the stubs intentionally do nothing.
 * --------------------------------------------------------------------------*/

void *__bound_ptr_add(void *p, unsigned long offset)
{
    return (char *)p + offset;
}

void *__bound_ptr_indir1(void *p, unsigned long offset)  { return (char *)p + offset; }
void *__bound_ptr_indir2(void *p, unsigned long offset)  { return (char *)p + offset; }
void *__bound_ptr_indir4(void *p, unsigned long offset)  { return (char *)p + offset; }
void *__bound_ptr_indir8(void *p, unsigned long offset)  { return (char *)p + offset; }
void *__bound_ptr_indir12(void *p, unsigned long offset) { return (char *)p + offset; }
void *__bound_ptr_indir16(void *p, unsigned long offset) { return (char *)p + offset; }

void __bound_local_new(void *p) { (void)p; }
void __bound_local_delete(void *p) { (void)p; }
void __bound_new_region(void *p, unsigned long size) { (void)p; (void)size; }
int  __bound_delete_region(void *p) { (void)p; return 0; }

void __bounds_checking(int no_check) { (void)no_check; }
void __bound_never_fatal(int n) { (void)n; }
void __bound_main_arg(int argc, char **argv, char **envp)
    { (void)argc; (void)argv; (void)envp; }
void __bound_exit(void) {}
void __bound_init(unsigned long *p, int mode) { (void)p; (void)mode; }
void __bound_setjmp(void *env) { (void)env; }
void __bound_longjmp(void *env, int val) { (void)env; (void)val; }
void __bound_siglongjmp(void *env, int val) { (void)env; (void)val; }

/* Bound-check string/mem helper stubs — forward to libc with no
 * actual checking. Same rationale: link-and-run rather than
 * actually validate. */
extern void *memcpy(void *, const void *, unsigned long);
extern int   memcmp(const void *, const void *, unsigned long);
extern void *memmove(void *, const void *, unsigned long);
extern void *memset(void *, int, unsigned long);
extern unsigned long strlen(const char *);
extern char *strcpy(char *, const char *);
extern char *strncpy(char *, const char *, unsigned long);
extern int   strcmp(const char *, const char *);
extern int   strncmp(const char *, const char *, unsigned long);
extern char *strcat(char *, const char *);
extern char *strncat(char *, const char *, unsigned long);
extern char *strchr(const char *, int);
extern char *strrchr(const char *, int);
extern char *strstr(const char *, const char *);
extern char *strdup(const char *);
extern void *malloc(unsigned long);
extern void *calloc(unsigned long, unsigned long);
extern void *realloc(void *, unsigned long);
extern void  free(void *);

void *__bound_memcpy(void *d, const void *s, unsigned long n) { return memcpy(d,s,n); }
int   __bound_memcmp(const void *a, const void *b, unsigned long n) { return memcmp(a,b,n); }
void *__bound_memmove(void *d, const void *s, unsigned long n) { return memmove(d,s,n); }
void *__bound_memset(void *d, int c, unsigned long n) { return memset(d,c,n); }
int   __bound_strlen(const char *s) { return (int)strlen(s); }
char *__bound_strcpy(char *d, const char *s) { return strcpy(d,s); }
char *__bound_strncpy(char *d, const char *s, unsigned long n) { return strncpy(d,s,n); }
int   __bound_strcmp(const char *a, const char *b) { return strcmp(a,b); }
int   __bound_strncmp(const char *a, const char *b, unsigned long n) { return strncmp(a,b,n); }
char *__bound_strcat(char *d, const char *s) { return strcat(d,s); }
char *__bound_strncat(char *d, const char *s, unsigned long n) { return strncat(d,s,n); }
char *__bound_strchr(const char *s, int c) { return strchr(s,c); }
char *__bound_strrchr(const char *s, int c) { return strrchr(s,c); }
char *__bound_strstr(const char *h, const char *n) { return strstr(h,n); }
char *__bound_strdup(const char *s) { return strdup(s); }

/* Heap helpers: just pass through. With a real bcheck.c port these
 * would track regions; for now make sure tcc-built programs that
 * link with -b can still call malloc/free/realloc/etc. */
void *__bound_malloc(unsigned long n)              { return malloc(n); }
void *__bound_calloc(unsigned long n, unsigned long s) { return calloc(n,s); }
void *__bound_realloc(void *p, unsigned long n)    { return realloc(p,n); }
void  __bound_free(void *p)                        { free(p); }
void *__bound_memalign(unsigned long a, unsigned long n) {
    /* Tiger libc has no memalign(3); valloc(n) returns page-aligned
     * memory which dominates any normal alignment request. Bounds
     * checking is no-op anyway, so the over-aligned return is fine. */
    extern void *valloc(unsigned long);
    (void)a;
    return valloc(n);
}

/* Bounds-internal hooks. With a real bcheck.c port these would
 * acquire a global lock and walk allocations; here they're no-ops
 * so any code linked with -b doesn't fail to resolve them. */
void __bound_check(const void *p, unsigned long n) { (void)p; (void)n; }
void __bound_checking_lock(void)                    {}
void __bound_checking_unlock(void)                  {}
void __bound_exit_dll(void)                         {}
void __bound_long_jump(void *env, int val)          { (void)env; (void)val; }

/* ---------------------------------------------------------------------------
 * Atomic helper functions — split between lock-free and mutex paths.
 *
 * tcc's atomic codegen calls __atomic_load_N / __atomic_store_N /
 * __atomic_compare_exchange_N etc. as ordinary functions when the
 * backend lacks intrinsic codegen for them. The 4-byte variants
 * live in atomic-ppc.S as lock-free lwarx/stwcx implementations
 * compiled by gcc-4.0 (tcc PPC's built-in assembler doesn't cover
 * those instructions; gcc fills the gap via a per-file Makefile
 * rule). This file provides the 1, 2, and 8 byte variants plus
 * atomic_flag_*, all serialized through a single pthread_mutex --
 * PPC32 has no lbarx/sbarx for 1/2-byte atomics (would need
 * word-RMW with masking) and no ldarx/stdcx for 8-byte (PPC64
 * only), so locking is the simplest correct option.
 *
 * libpthread is part of libSystem on Tiger, so no extra link
 * dependency beyond what every program already pulls in.
 * --------------------------------------------------------------------------*/

#include <pthread.h>

static pthread_mutex_t __ppc_atomic_lock = PTHREAD_MUTEX_INITIALIZER;
#define ATOMIC_LOCK()   pthread_mutex_lock(&__ppc_atomic_lock)
#define ATOMIC_UNLOCK() pthread_mutex_unlock(&__ppc_atomic_lock)

#define ATOMIC_LOAD(BITS, TYPE) \
TYPE __atomic_load_##BITS(const TYPE *p, int o) { \
    TYPE r; (void)o; \
    ATOMIC_LOCK(); r = *p; ATOMIC_UNLOCK(); \
    return r; \
}
/* 1, 2, 4-byte ops live in atomic-ppc.S (lock-free lwarx/stwcx). */
ATOMIC_LOAD(8, unsigned long long)
#undef ATOMIC_LOAD

#define ATOMIC_STORE(BITS, TYPE) \
void __atomic_store_##BITS(TYPE *p, TYPE v, int o) { \
    (void)o; \
    ATOMIC_LOCK(); *p = v; ATOMIC_UNLOCK(); \
}
/* 1, 2, 4-byte: atomic-ppc.S */
ATOMIC_STORE(8, unsigned long long)
#undef ATOMIC_STORE

#define ATOMIC_EXCHANGE(BITS, TYPE) \
TYPE __atomic_exchange_##BITS(TYPE *p, TYPE v, int o) { \
    TYPE r; (void)o; \
    ATOMIC_LOCK(); r = *p; *p = v; ATOMIC_UNLOCK(); \
    return r; \
}
/* 1, 2, 4-byte: atomic-ppc.S */
ATOMIC_EXCHANGE(8, unsigned long long)
#undef ATOMIC_EXCHANGE

#define ATOMIC_CAS(BITS, TYPE) \
int __atomic_compare_exchange_##BITS(TYPE *p, TYPE *exp, TYPE des, \
                                     int weak, int o1, int o2) { \
    int ok; \
    (void)weak; (void)o1; (void)o2; \
    ATOMIC_LOCK(); \
    if (*p == *exp) { *p = des; ok = 1; } \
    else            { *exp = *p; ok = 0; } \
    ATOMIC_UNLOCK(); \
    return ok; \
}
/* 1, 2, 4-byte: atomic-ppc.S */
ATOMIC_CAS(8, unsigned long long)
#undef ATOMIC_CAS

#define ATOMIC_RMW(NAME, BITS, TYPE, OP) \
TYPE __atomic_fetch_##NAME##_##BITS(TYPE *p, TYPE v, int o) { \
    TYPE r; (void)o; \
    ATOMIC_LOCK(); r = *p; *p = OP; ATOMIC_UNLOCK(); \
    return r; \
}
/* 1, 2, 4-byte: atomic-ppc.S. 8-byte stays here -- PPC32 has no
 * ldarx/stdcx (those are PPC64), so locking is the only option. */
ATOMIC_RMW(add,  8, unsigned long long, r + v)
ATOMIC_RMW(sub,  8, unsigned long long, r - v)
ATOMIC_RMW(and,  8, unsigned long long, r & v)
ATOMIC_RMW(or,   8, unsigned long long, r | v)
ATOMIC_RMW(xor,  8, unsigned long long, r ^ v)
ATOMIC_RMW(nand, 8, unsigned long long, ~(r & v))
#undef ATOMIC_RMW

/* C11 atomic_flag_test_and_set / clear -- wrapped over the byte
 * atomics in atomic-ppc.S so they're lock-free too. */
extern unsigned char __atomic_exchange_1(unsigned char *p, unsigned char v, int o);
extern void __atomic_store_1(unsigned char *p, unsigned char v, int o);

int atomic_flag_test_and_set(volatile unsigned char *p) {
    return __atomic_exchange_1((unsigned char *)p, 1, 0) != 0;
}
int atomic_flag_test_and_set_explicit(volatile unsigned char *p, int o) {
    return __atomic_exchange_1((unsigned char *)p, 1, o) != 0;
}
void atomic_flag_clear(volatile unsigned char *p) {
    __atomic_store_1((unsigned char *)p, 0, 0);
}
void atomic_flag_clear_explicit(volatile unsigned char *p, int o) {
    __atomic_store_1((unsigned char *)p, 0, o);
}

/* __atomic_OP_fetch family: returns the NEW value (post-op) instead
 * of the old. Implemented as wrappers over the existing fetch_OP
 * helpers from atomic-ppc.S / lib-ppc.c. The compiler folds these
 * into single calls when LTO is on; without LTO they're a small
 * extra `add r3, r3, r4`-style epilogue. */
extern unsigned char  __atomic_fetch_add_1(unsigned char *p,  unsigned char v,  int o);
extern unsigned short __atomic_fetch_add_2(unsigned short *p, unsigned short v, int o);
extern unsigned int   __atomic_fetch_add_4(unsigned int *p,   unsigned int v,   int o);
extern unsigned char  __atomic_fetch_sub_1(unsigned char *p,  unsigned char v,  int o);
extern unsigned short __atomic_fetch_sub_2(unsigned short *p, unsigned short v, int o);
extern unsigned int   __atomic_fetch_sub_4(unsigned int *p,   unsigned int v,   int o);
extern unsigned char  __atomic_fetch_and_1(unsigned char *p,  unsigned char v,  int o);
extern unsigned short __atomic_fetch_and_2(unsigned short *p, unsigned short v, int o);
extern unsigned int   __atomic_fetch_and_4(unsigned int *p,   unsigned int v,   int o);
extern unsigned char  __atomic_fetch_or_1 (unsigned char *p,  unsigned char v,  int o);
extern unsigned short __atomic_fetch_or_2 (unsigned short *p, unsigned short v, int o);
extern unsigned int   __atomic_fetch_or_4 (unsigned int *p,   unsigned int v,   int o);
extern unsigned char  __atomic_fetch_xor_1(unsigned char *p,  unsigned char v,  int o);
extern unsigned short __atomic_fetch_xor_2(unsigned short *p, unsigned short v, int o);
extern unsigned int   __atomic_fetch_xor_4(unsigned int *p,   unsigned int v,   int o);
extern unsigned char  __atomic_fetch_nand_1(unsigned char *p,  unsigned char v,  int o);
extern unsigned short __atomic_fetch_nand_2(unsigned short *p, unsigned short v, int o);
extern unsigned int   __atomic_fetch_nand_4(unsigned int *p,   unsigned int v,   int o);

#define ADD_FETCH(BITS, TYPE) \
TYPE __atomic_add_fetch_##BITS(TYPE *p, TYPE v, int o) \
    { return __atomic_fetch_add_##BITS(p, v, o) + v; }
ADD_FETCH(1, unsigned char)
ADD_FETCH(2, unsigned short)
ADD_FETCH(4, unsigned int)
ADD_FETCH(8, unsigned long long)
#undef ADD_FETCH

#define SUB_FETCH(BITS, TYPE) \
TYPE __atomic_sub_fetch_##BITS(TYPE *p, TYPE v, int o) \
    { return __atomic_fetch_sub_##BITS(p, v, o) - v; }
SUB_FETCH(1, unsigned char)
SUB_FETCH(2, unsigned short)
SUB_FETCH(4, unsigned int)
SUB_FETCH(8, unsigned long long)
#undef SUB_FETCH

#define AND_FETCH(BITS, TYPE) \
TYPE __atomic_and_fetch_##BITS(TYPE *p, TYPE v, int o) \
    { return __atomic_fetch_and_##BITS(p, v, o) & v; }
AND_FETCH(1, unsigned char)
AND_FETCH(2, unsigned short)
AND_FETCH(4, unsigned int)
AND_FETCH(8, unsigned long long)
#undef AND_FETCH

#define OR_FETCH(BITS, TYPE) \
TYPE __atomic_or_fetch_##BITS(TYPE *p, TYPE v, int o) \
    { return __atomic_fetch_or_##BITS(p, v, o) | v; }
OR_FETCH(1, unsigned char)
OR_FETCH(2, unsigned short)
OR_FETCH(4, unsigned int)
OR_FETCH(8, unsigned long long)
#undef OR_FETCH

#define XOR_FETCH(BITS, TYPE) \
TYPE __atomic_xor_fetch_##BITS(TYPE *p, TYPE v, int o) \
    { return __atomic_fetch_xor_##BITS(p, v, o) ^ v; }
XOR_FETCH(1, unsigned char)
XOR_FETCH(2, unsigned short)
XOR_FETCH(4, unsigned int)
XOR_FETCH(8, unsigned long long)
#undef XOR_FETCH

#define NAND_FETCH(BITS, TYPE) \
TYPE __atomic_nand_fetch_##BITS(TYPE *p, TYPE v, int o) \
    { return ~(__atomic_fetch_nand_##BITS(p, v, o) & v); }
NAND_FETCH(1, unsigned char)
NAND_FETCH(2, unsigned short)
NAND_FETCH(4, unsigned int)
NAND_FETCH(8, unsigned long long)
#undef NAND_FETCH

/* __atomic_is_lock_free: 1, 2, 4-byte ops are lock-free on PPC32
 * (lwarx/stwcx + word-RMW for byte/short). 8-byte still goes
 * through pthread_mutex (no ldarx/stdcx on PPC32). */
int __atomic_is_lock_free(unsigned long size, const volatile void *ptr) {
    (void)ptr;
    return size <= 4;
}
