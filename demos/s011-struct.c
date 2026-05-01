/*
 * Session 011 milestone — struct member access + pass-by-pointer.
 *
 * Build & run on imacg3 (Tiger 10.4.11 PowerPC):
 *
 *     ./tcc -B. -run demos/s011-struct.c
 *     echo $?      # → 42
 *
 * Why this matters: tcc's own source uses structs *everywhere*
 * (Sym, CType, SValue, Section, ...), but almost always via pointer.
 * This demo proves both:
 *   1) Local struct allocation + member access works (`p.field`).
 *   2) Struct passed via pointer to a helper works (`p->field`).
 *   3) Structs containing pointers work (helper walks an array
 *      pointed at by a struct member).
 *
 * What's NOT yet implemented (deferred):
 *   - Struct return by value (Apple PPC ABI: caller allocates
 *     space, passes pointer in r3 as hidden first arg).
 *   - Struct passed by value (Apple-specific in-register passing
 *     rules for small composites).
 *   - Anything that triggers tcc's memcpy intrinsic for whole-
 *     struct copies (needs a working PLT or builtin).
 *
 * tcc itself almost never returns structs by value, so this is
 * acceptable for the self-host target.
 */

struct point { int x; int y; };

struct dataset {
    int *values;
    int n;
};

int distance_squared(struct point *p, struct point *q)
{
    int dx = p->x - q->x;
    int dy = p->y - q->y;
    return dx*dx + dy*dy;
}

int sum_dataset(struct dataset *ds)
{
    int s = 0;
    int i = 0;
    while (i < ds->n) { s = s + ds->values[i]; i = i + 1; }
    return s;
}

int main(void)
{
    struct point a, b;
    a.x = 0;  a.y = 0;
    b.x = 5;  b.y = 5;
    int d2 = distance_squared(&a, &b);   /* = 50 */

    int values[3];
    values[0] = -10;
    values[1] = 20;
    values[2] = -18;
    struct dataset ds;
    ds.values = values;
    ds.n = 3;
    int s = sum_dataset(&ds);            /* = -8 */

    return d2 + s;                       /* 50 + -8 = 42 */
}
