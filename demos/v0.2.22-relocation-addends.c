/*
 * v0.2.22-relocation-addends.c — `int *p = &arr[N]` static
 * initializers actually point at &arr[N] now (not &arr[0]).
 *
 * Build + run:
 *     ./tcc -B. -I. -I./include -o /tmp/relo \
 *         demos/v0.2.22-relocation-addends.c
 *     /tmp/relo
 *
 * Pre-fix output:    *rel1 = 100, *rel2 = 100
 * Post-fix output:   *rel1 = 200, *rel2 = 300
 */

#include <stdio.h>

int reltab[3] = { 100, 200, 300 };
int *rel1 = &reltab[1];
int *rel2 = &reltab[2];

/* Function-pointer-with-addend isn't a thing, but a struct-with-
 * pointers static init exercises the same code path. */
struct two_ptrs { int *a; int *b; } tp = { &reltab[1], &reltab[2] };

int main(void)
{
    printf("*rel1   = %d (want 200)\n", *rel1);
    printf("*rel2   = %d (want 300)\n", *rel2);
    printf("*tp.a   = %d (want 200)\n", *tp.a);
    printf("*tp.b   = %d (want 300)\n", *tp.b);
    return (*rel1 == 200 && *rel2 == 300
            && *tp.a == 200 && *tp.b == 300) ? 0 : 1;
}
