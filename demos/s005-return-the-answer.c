/*
 * Session 005 milestone — first end-to-end execution of tcc-generated
 * PPC code on a real G3 via `tcc -run`.
 *
 * Build & run on imacg3 (Tiger 10.4.11 PowerPC):
 *
 *     ./tcc -B. -run demos/s005-return-the-answer.c
 *     echo $?      # → 42
 *
 * Why this matters: every prior session was infrastructure (configure,
 * Makefile, scaffold). This one closes the loop: tcc compiles a C
 * function to PowerPC instructions, mmaps them executable, calls the
 * function pointer, gets the right answer back.
 *
 * Codegen surface exercised: function prologue + epilogue (Apple PPC
 * ABI, with r31 frame pointer), `li` immediate load, `blr` return.
 * That's the entire feature set of session 005 — and it's enough to
 * prove the JIT pipeline, the icache flush, and the ABI compliance
 * with Tiger's calling convention.
 */

int main(void)
{
    return 42;  /* the answer */
}
