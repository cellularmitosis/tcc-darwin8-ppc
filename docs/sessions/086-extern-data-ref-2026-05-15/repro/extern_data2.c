/* Slightly larger repro: multiple extern data + static init pointers,
 * mixing local + extern data references, plus a runtime access path
 * (which still uses the slot via PIC). Looks for interactions that
 * the minimal repro might have missed. */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int optind;
extern int opterr;
extern char *optarg;

int local_int = 42;
int *p_opt   = &optind;
int *p_err   = &opterr;
int *p_loc   = &local_int;
char **p_arg = &optarg;

int main(void) {
    optind = 11;
    opterr = 22;
    *p_loc = 33;
    optarg = "hello";
    if (p_opt != &optind) { puts("FAIL p_opt"); return 1; }
    if (p_err != &opterr) { puts("FAIL p_err"); return 1; }
    if (p_loc != &local_int) { puts("FAIL p_loc"); return 1; }
    if (p_arg != &optarg) { puts("FAIL p_arg"); return 1; }
    if (*p_opt != 11) { puts("FAIL *p_opt"); return 1; }
    if (*p_err != 22) { puts("FAIL *p_err"); return 1; }
    if (*p_loc != 33) { puts("FAIL *p_loc"); return 1; }
    if (strcmp(*p_arg, "hello") != 0) { puts("FAIL *p_arg"); return 1; }
    puts("PASS");
    return 0;
}
