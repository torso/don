typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;

typedef unsigned int uint;
typedef unsigned long ulong;
typedef uint8 boolean;
typedef uint8 byte;

#define null ((void*)0)
#define false 0
#define true 1
#define MAX_UINT ((uint)-1)

#define nonnull __attribute((nonnull))
#define pure __attribute((pure))

extern void* zmalloc(size_t size);

#ifdef DEBUG
extern void _assert(const char* expression, const char* file, int line);
#define assert(e) if (e) {} else { _assert(#e, __FILE__, __LINE__); }
#else
#define assert(e)
#endif
