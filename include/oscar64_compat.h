/*
 * oscar64_compat.h -- GCC/llvm-mos attribute stubs for oscar64 compilation.
 *
 * oscar64 does not recognise GCC __attribute__((...)) syntax.  When building
 * with __OSCAR64__ defined, all __attribute__ annotations are silently
 * removed so that headers and source files shared with the llvm-mos build
 * continue to parse cleanly.
 *
 * Include this file before any f256lib or project header in oscar64 builds,
 * or add it as the first -i path so the compiler can find it automatically.
 */

#ifndef OSCAR64_COMPAT_H
#define OSCAR64_COMPAT_H

#ifdef __OSCAR64__

/* Suppress all GCC __attribute__((...)) annotations. */
#define __attribute__(x)  /* oscar64: GCC attribute suppressed */

/* oscar64 has its own __asm {} syntax; map the GCC __asm__ keyword so that
 * any remaining guard-wrapped GCC asm statements at least tokenise. */
#ifndef __asm__
#define __asm__  __asm
#endif

/* oscar64 f256lib names the payload unions inside event_t and call_args as
 * 'u'.  The llvm-mos f256lib uses anonymous unions, so the '.u.' path does
 * not exist there.  Use these macros everywhere you access those fields. */
#define KEVENT(field)  kernelEventData.u.field
#define KEVENT_FILE_DATA(field) kernelEventData.u.file.u.field
#define KARGS(field)   kernelArgs->u.field

#else /* llvm-mos: anonymous unions, no '.u.' indirection */

#define KEVENT(field)  kernelEventData.field
#define KEVENT_FILE_DATA(field) kernelEventData.file.field
#define KARGS(field)   kernelArgs->field

#endif /* __OSCAR64__ */

#endif /* OSCAR64_COMPAT_H */
