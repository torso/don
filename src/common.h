#define _BSD_SOURCE 1
#define _POSIX_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#define _GNU_SOURCE 1

#include <limits.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef VALGRIND
#undef NVALGRIND
#include <valgrind/valgrind.h>
#endif

#ifndef DATADIR
#define DATADIR "data/"
#endif

#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN
#endif
#define HAVE_POSIX_SPAWN 0
#define HAVE_OPENAT 1
#define HAVE_PIPE2 1
#define VFORK vfork

typedef int8_t int8;
typedef int16_t int16;
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

typedef unsigned int uint;
typedef unsigned long int ulong;
typedef uint8 byte;

#define null (0)

#define attrprintf(formatarg, args) __attribute((format(printf, formatarg, args)))
#define nonnull __attribute((nonnull))
#define noreturn __attribute((noreturn))
#define pure __attribute((pure))
#define pureconst __attribute((const))
#define restrict __restrict
#define unused __attribute((unused))
#ifdef DEBUG
#define unreachable _assert("unreachable", __FILE__, __LINE__)
#else
#define unreachable __builtin_unreachable()
#endif
#define expect(a, b) __builtin_expect((a), (b))
#define likely(a) __builtin_expect(!!(a), 1)
#define unlikely(a) __builtin_expect(!!(a), 0)

#define min(a, b) ((a) > (b) ? (b) : (a))
#define max(a, b) ((a) < (b) ? (b) : (a))

#define calloc mycalloc
#define malloc mymalloc
#define realloc myrealloc
extern void *nonnull mycalloc(size_t count, size_t eltsize);
extern void *nonnull mymalloc(size_t size);
extern void *nonnull myrealloc(void *ptr, size_t size);

#ifdef DEBUG
extern noreturn void _assert(const char *expression, const char *file, int line);
#define assert(e) do { if (!(e)) { _assert(#e, __FILE__, __LINE__); } } while (false)
#else
#define assert(e) do { (void)sizeof(e); } while (false)
#endif

extern noreturn void cleanShutdown(int exitcode);

typedef struct bytevector bytevector;
typedef struct inthashmap inthashmap;
typedef struct intvector intvector;
typedef struct HashState HashState;
typedef struct VM VM;

typedef uint ref_t;
typedef ref_t namespaceref;
typedef ref_t nativefunctionref;

/*
  A reference to a value. Generally manipulated by the functions in value.h, but
  is actually an index in the heap.
*/
typedef ref_t vref;

typedef struct
{
    vref name;
    vref value;
} ParameterInfo;

unused static ref_t refFromInt(int i)
{
    return (ref_t)i;
}

unused static ref_t refFromUint(uint i)
{
    return (ref_t)i;
}

unused static ref_t refFromSize(size_t i)
{
    assert(i <= UINT_MAX - 1);
    return (ref_t)i;
}

unused static size_t sizeFromRef(ref_t r)
{
    return (size_t)r;
}

unused static int intFromRef(ref_t r)
{
    return (int)r;
}

unused static uint uintFromRef(ref_t r)
{
    return (uint)r;
}
