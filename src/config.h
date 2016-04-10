#define _BSD_SOURCE 1
#define _POSIX_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#define _GNU_SOURCE 1

#ifndef DATADIR
#define DATADIR "data/"
#endif

#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN
#endif
#define HAVE_MEMRCHR 1
#define HAVE_OPENAT 1
#define HAVE_PIPE2 1
#define HAVE_POSIX_SPAWN 1
#define HAVE_VFORK 1

#if HAVE_VFORK
#define VFORK vfork
#define USE_POSIX_SPAWN 0
#else
#define VFORK fork
#define USE_POSIX_SPAWN HAVE_POSIX_SPAWN
#endif
