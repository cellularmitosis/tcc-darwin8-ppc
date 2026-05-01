/*
 * Session 010 milestone — long long (64-bit int) codegen.
 *
 * Build & run on imacg3 (Tiger 10.4.11 PowerPC):
 *
 *     ./tcc -B. -run demos/s010-long-long.c
 *     echo $?      # → 120  (= 0x78, low byte of 0x22345678)
 *
 * Why this matters: tcc uses long long heavily in its own source
 * (file offsets, compile-time integer arithmetic, etc.). Working
 * 64-bit integers are a precondition for self-hosting.
 *
 * What's exercised here:
 *   - long long literals (LL suffix).
 *   - long long parameter passing (each long long takes 2 GPR slots:
 *     r3:r4 for the first arg, r5:r6 for the second, etc.).
 *   - long long return (low in r3, high in r4).
 *   - long long arithmetic — `+` decomposes into addc/adde, `-`
 *     into subfc/subfe, `*` (LL*LL) into 4 mullw + 1 mulhwu + 3 add
 *     sequence orchestrated by tcc's gen_opl.
 *   - long long stack storage (8-byte spill slots in the frame).
 *
 * What's still TODO:
 *   - long long divide / modulo on G3: tcc emits calls to libgcc
 *     helpers (__divdi3, __moddi3) which currently require a working
 *     PLT (next session block).
 *   - long long stored from a value that doesn't set r2 — tcc has
 *     a path that hits this; investigation deferred.
 */

long long add64(long long a, long long b) { return a + b; }

int main(void)
{
    /* 0x12345678 + 0x10000000 = 0x22345678; low byte = 0x78 = 120. */
    long long r = add64(0x12345678LL, 0x10000000LL);
    return (int)r;
}
