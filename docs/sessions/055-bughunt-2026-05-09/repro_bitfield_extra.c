/* Additional BE bitfield tests: compound assign, array, function arg. */
#include <stdio.h>

static int fail = 0;

#define EXPECT(name, got, want) do {                          \
    if ((got) != (want)) {                                    \
        printf("FAIL %s: got 0x%lx expect 0x%lx\n",           \
               name, (long)(got), (long)(want));              \
        fail = 1;                                             \
    } else {                                                  \
        printf("PASS %s: 0x%lx\n", name, (long)(got));        \
    }                                                         \
} while (0)

struct BF {
    unsigned a : 14;
    unsigned b : 14;
};

static struct BF arr[3] = { {1, 2}, {3, 4}, {5, 6} };

static int sum_a(struct BF *p, int n)
{
    int s = 0, i;
    for (i = 0; i < n; i++)
        s += p[i].a;
    return s;
}

static struct BF make_bf(unsigned a, unsigned b)
{
    struct BF r;
    r.a = a;
    r.b = b;
    return r;
}

int main(void)
{
    /* Compound assignment. */
    struct BF s;
    s.a = 100;
    s.b = 200;
    s.a += 50;
    s.b *= 2;
    EXPECT("compound a", s.a, 150u);
    EXPECT("compound b", s.b, 400u);

    /* Array of struct. */
    EXPECT("arr[0].a", arr[0].a, 1u);
    EXPECT("arr[1].b", arr[1].b, 4u);
    EXPECT("arr[2].a", arr[2].a, 5u);
    EXPECT("sum_a(arr,3)", sum_a(arr, 3), 9);

    /* Function returns struct with bitfield. */
    struct BF r = make_bf(0x123, 0x234);
    EXPECT("returned a", r.a, 0x123u);
    EXPECT("returned b", r.b, 0x234u);

    /* Bitfield as boolean. */
    s.a = 1;
    EXPECT("bool a=1",   s.a ? 1 : 0, 1);
    s.a = 0;
    EXPECT("bool a=0",   s.a ? 1 : 0, 0);

    /* Pre/post-increment. */
    s.a = 10;
    s.a++;
    EXPECT("post-inc",   s.a, 11u);
    ++s.a;
    EXPECT("pre-inc",    s.a, 12u);

    /* Static initializer with multiple fields in one container. */
    static struct BF init1 = { 0x1234, 0x2345 };
    EXPECT("init1 a",    init1.a, 0x1234u);
    EXPECT("init1 b",    init1.b, 0x2345u);

    if (fail) {
        printf("\n*** FAIL ***\n");
        return 1;
    }
    printf("\nAll PASS\n");
    return 0;
}
