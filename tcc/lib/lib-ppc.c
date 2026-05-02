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
extern char *strdup(const char *);

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
char *__bound_strdup(const char *s) { return strdup(s); }

/* ---------------------------------------------------------------------------
 * Atomic helper stubs (single-threaded — DO NOT use for real
 * concurrency).
 *
 * tcc's atomic codegen calls __atomic_load_N / __atomic_store_N /
 * __atomic_compare_exchange_N etc. as ordinary functions when the
 * backend lacks intrinsic codegen for them. The full atomic.S
 * helpers (lwarx/stwcx. variants) aren't ported to PPC yet; for
 * single-threaded test programs that just exercise atomic syntax,
 * a plain non-atomic implementation is observationally correct.
 *
 * Multi-threaded tests (124_atomic_counter) will race; that's
 * documented as a "needs real PPC atomics" gap.
 * --------------------------------------------------------------------------*/

unsigned char __atomic_load_1(const unsigned char *p, int o) { (void)o; return *p; }
unsigned short __atomic_load_2(const unsigned short *p, int o) { (void)o; return *p; }
unsigned int __atomic_load_4(const unsigned int *p, int o) { (void)o; return *p; }
unsigned long long __atomic_load_8(const unsigned long long *p, int o) { (void)o; return *p; }

void __atomic_store_1(unsigned char *p, unsigned char v, int o) { (void)o; *p = v; }
void __atomic_store_2(unsigned short *p, unsigned short v, int o) { (void)o; *p = v; }
void __atomic_store_4(unsigned int *p, unsigned int v, int o) { (void)o; *p = v; }
void __atomic_store_8(unsigned long long *p, unsigned long long v, int o) { (void)o; *p = v; }

unsigned char __atomic_exchange_1(unsigned char *p, unsigned char v, int o)
    { unsigned char r = *p; (void)o; *p = v; return r; }
unsigned short __atomic_exchange_2(unsigned short *p, unsigned short v, int o)
    { unsigned short r = *p; (void)o; *p = v; return r; }
unsigned int __atomic_exchange_4(unsigned int *p, unsigned int v, int o)
    { unsigned int r = *p; (void)o; *p = v; return r; }
unsigned long long __atomic_exchange_8(unsigned long long *p,
                                       unsigned long long v, int o)
    { unsigned long long r = *p; (void)o; *p = v; return r; }

#define ATOMIC_CAS(BITS, TYPE) \
int __atomic_compare_exchange_##BITS(TYPE *p, TYPE *exp, TYPE des, \
                                     int weak, int o1, int o2) { \
    (void)weak; (void)o1; (void)o2; \
    if (*p == *exp) { *p = des; return 1; } \
    *exp = *p; return 0; \
}
ATOMIC_CAS(1, unsigned char)
ATOMIC_CAS(2, unsigned short)
ATOMIC_CAS(4, unsigned int)
ATOMIC_CAS(8, unsigned long long)
#undef ATOMIC_CAS

#define ATOMIC_RMW(NAME, BITS, TYPE, OP) \
TYPE __atomic_fetch_##NAME##_##BITS(TYPE *p, TYPE v, int o) { \
    TYPE r = *p; (void)o; *p = OP; return r; \
}
ATOMIC_RMW(add, 1, unsigned char,      r + v)
ATOMIC_RMW(add, 2, unsigned short,     r + v)
ATOMIC_RMW(add, 4, unsigned int,       r + v)
ATOMIC_RMW(add, 8, unsigned long long, r + v)
ATOMIC_RMW(sub, 1, unsigned char,      r - v)
ATOMIC_RMW(sub, 2, unsigned short,     r - v)
ATOMIC_RMW(sub, 4, unsigned int,       r - v)
ATOMIC_RMW(sub, 8, unsigned long long, r - v)
ATOMIC_RMW(and, 1, unsigned char,      r & v)
ATOMIC_RMW(and, 2, unsigned short,     r & v)
ATOMIC_RMW(and, 4, unsigned int,       r & v)
ATOMIC_RMW(and, 8, unsigned long long, r & v)
ATOMIC_RMW(or,  1, unsigned char,      r | v)
ATOMIC_RMW(or,  2, unsigned short,     r | v)
ATOMIC_RMW(or,  4, unsigned int,       r | v)
ATOMIC_RMW(or,  8, unsigned long long, r | v)
ATOMIC_RMW(xor, 1, unsigned char,      r ^ v)
ATOMIC_RMW(xor, 2, unsigned short,     r ^ v)
ATOMIC_RMW(xor, 4, unsigned int,       r ^ v)
ATOMIC_RMW(xor, 8, unsigned long long, r ^ v)
ATOMIC_RMW(nand, 1, unsigned char,     ~(r & v))
ATOMIC_RMW(nand, 2, unsigned short,    ~(r & v))
ATOMIC_RMW(nand, 4, unsigned int,      ~(r & v))
ATOMIC_RMW(nand, 8, unsigned long long,~(r & v))
#undef ATOMIC_RMW

/* C11 atomic_flag_* — single-threaded stubs. */
int atomic_flag_test_and_set(volatile unsigned char *p)
    { unsigned char r = *p; *p = 1; return r != 0; }
int atomic_flag_test_and_set_explicit(volatile unsigned char *p, int o)
    { (void)o; return atomic_flag_test_and_set(p); }
void atomic_flag_clear(volatile unsigned char *p) { *p = 0; }
void atomic_flag_clear_explicit(volatile unsigned char *p, int o)
    { (void)o; *p = 0; }
