#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

typedef int8_t int8;
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

typedef unsigned int uint;
typedef unsigned long int ulong;
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

#define attrprintf(formatarg, args) __attribute((format(printf, formatarg, args)))
#define nonnull __attribute((nonnull))
#define noreturn __attribute((noreturn))
#define pure __attribute((pure))
#define restrict __restrict
#define unused __attribute((unused))

#define min(a, b) (a > b ? b : a)
#define max(a, b) (a < b ? b : a)

#define calloc mycalloc
#define malloc mymalloc
extern void *nonnull mycalloc(size_t count, size_t eltsize);
extern void *nonnull mymalloc(size_t size);

#ifdef DEBUG
extern void _assert(const char *expression, const char *file, int line);
#define assert(e) do { if (!(e)) { _assert(#e, __FILE__, __LINE__); } } while (false)
#else
#define assert(e) do { (void)sizeof(e); } while(0)
#endif

typedef struct bytevector bytevector;
typedef struct inthashmap inthashmap;
typedef struct intvector intvector;
typedef struct HashState HashState;
typedef struct VM VM;

typedef uint ref_t;
typedef ref_t cacheref;
typedef ref_t fieldref;
typedef ref_t fileref;
typedef ref_t functionref;
typedef ref_t namespaceref;
typedef ref_t nativefunctionref;
typedef ref_t objectref;
typedef ref_t stringref;

typedef struct
{
    stringref name;
    fieldref value;
} ParameterInfo;

extern pure ref_t refFromSize(size_t i);
extern pure ref_t refFromUint(uint i);
extern pure size_t sizeFromRef(ref_t r);
extern pure uint uintFromRef(ref_t r);
