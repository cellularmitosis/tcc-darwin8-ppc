/*
 * bt-log stub for 32-bit PowerPC (Apple Mach-O), installed as bt-log.o.
 *
 * Same situation as bcheck-ppc.c: tcc -bt -run loads bt-log.o by
 * name from the support directory. We don't yet implement runtime
 * backtrace on PPC, but the file must exist or `tcc -bt -run`
 * errors out before doing anything.
 *
 * Provide a near-trivial tcc_backtrace() that just forwards the
 * format to stderr. Good enough for `assert()` and similar
 * no-frame-walking uses; full backtrace support waits on a
 * native PPC __builtin_frame_address.
 */

#include <stdarg.h>
#include <stdio.h>

int tcc_backtrace(const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
    return ret;
}
