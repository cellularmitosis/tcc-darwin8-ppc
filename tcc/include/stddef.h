#ifndef _STDDEF_H
#define _STDDEF_H

/* Tiger's sys/types.h gates ssize_t with #ifndef _SSIZE_T; mirror that
 * here so including system headers after stddef.h doesn't conflict.
 * Same idea for ptrdiff_t / intptr_t / uintptr_t — these are all
 * conventionally guarded in BSD/Darwin headers. */
#ifndef _SIZE_T
#define _SIZE_T
typedef __SIZE_TYPE__ size_t;
#endif
#ifndef _SSIZE_T
#define _SSIZE_T
typedef __PTRDIFF_TYPE__ ssize_t;
#endif
typedef __WCHAR_TYPE__ wchar_t;
#ifndef _PTRDIFF_T
#define _PTRDIFF_T
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#endif
#ifndef _INTPTR_T
#define _INTPTR_T
typedef __PTRDIFF_TYPE__ intptr_t;
#endif
#ifndef _UINTPTR_T
#define _UINTPTR_T
typedef __SIZE_TYPE__ uintptr_t;
#endif

#if __STDC_VERSION__ >= 201112L
typedef union { long long __ll; long double __ld; } max_align_t;
#endif

#ifndef NULL
#define NULL ((void*)0)
#endif

#undef offsetof
#define offsetof(type, field) __builtin_offsetof(type, field)

void *alloca(size_t size);

#endif

/* Older glibc require a wint_t from <stddef.h> (when requested
   by __need_wint_t, as otherwise stddef.h isn't allowed to
   define this type).   Note that this must be outside the normal
   _STDDEF_H guard, so that it works even when we've included the file
   already (without requiring wint_t).  Some other libs define _WINT_T
   if they've already provided that type, so we can use that as guard.
   TCC defines __WINT_TYPE__ for us.  */
#if defined (__need_wint_t)
#ifndef _WINT_T
#define _WINT_T
typedef __WINT_TYPE__ wint_t;
#endif
#undef __need_wint_t
#endif
