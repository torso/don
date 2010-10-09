#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

typedef int8_t int8;
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;

typedef unsigned int uint;
typedef _Bool boolean;
typedef uint8 byte;

#define null (0)
#define false 0
#define true 1
#define UINT_MAX ((uint)-1)
#define INT_MIN (1 << (sizeof(int) * 8 - 1))
#define INT_MAX (-1 - INT_MIN)
#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

#define nonnull __attribute((nonnull))
#define pure __attribute((pure))
#define restrict __restrict
#define unused __attribute((unused))

#define min(a, b) (a > b ? b : a)
#define max(a, b) (a < b ? b : a)

#ifdef DEBUG
extern void _assert(const char *expression, const char *file, int line);
#define assert(e) do { if (!(e)) { _assert(#e, __FILE__, __LINE__); } } while (false)

#ifndef _STDIO_H
extern int printf(__const char *__restrict __format, ...);
#endif

#define log(v) printf(#v"=%d\n", v);
#define logs(s) printf("%s\n", s);
#define logp(p) printf(#p"=%p\n", (void*)p);
#else
#define assert(e) do { (void)sizeof(e); } while(0)
#endif

typedef enum
{
    NO_ERROR = 0,
    OUT_OF_MEMORY,
    ERROR_FAIL
} ErrorCode;

typedef struct bytevector bytevector;
typedef struct inthashmap inthashmap;
typedef struct intvector intvector;
typedef struct VM VM;

typedef uint ref_t;
typedef ref_t nativefunctionref;
typedef ref_t stringref;
typedef ref_t fieldref;
typedef ref_t fileref;
typedef ref_t functionref;

extern pure ref_t refFromSize(size_t i);
extern pure ref_t refFromUint(uint i);
extern pure size_t sizeFromRef(ref_t r);
extern pure uint uintFromRef(ref_t r);
