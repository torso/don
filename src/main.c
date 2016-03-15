#include "config.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "common.h"
#include "bytecode.h"
#include "cache.h"
#include "debug.h"
#include "env.h"
#include "fail.h"
#include "file.h"
#include "heap.h"
#include "interpreter.h"
#include "intvector.h"
#include "linker.h"
#include "log.h"
#include "namespace.h"
#include "native.h"
#include "parser.h"
#include "stringpool.h"
#include "work.h"


static intvector targets;


int main(int argc, const char **argv)
{
    int i;
    uint j;
    const char *options;
    const char *inputFilename = null;
    const char *env;
    size_t envLength;
    char *string;
    char *cacheDirectory;
    size_t cacheDirectoryLength;
    namespaceref defaultNamespace;
    vref name;
    bool parseOptions = true;
    size_t size;
    bool fail;
    ParsedProgram parsed;
    LinkedProgram linked;

    IVInit(&targets, 4);
    LogInit();
    HeapInit();

    for (i = 1; i < argc; i++)
    {
        if (parseOptions && argv[i][0] == '-')
        {
            options = argv[i] + 1;
            if (!*options)
            {
                fputs("Invalid argument: \"-\"\n", stderr);
                return 1;
            }
            if (*options == '-')
            {
                if (*++options)
                {
                    fputs("TODO: Long option\n", stderr);
                    return 1;
                }
                else
                {
                    parseOptions = false;
                }
            }
            for (; *options; options++)
            {
                switch (*options)
                {
                case 'f':
                    if (inputFilename)
                    {
                        fputs("More than one input file specified.\n", stderr);
                        return 1;
                    }
                    if (++i >= argc)
                    {
                        fputs("Option \"-f\" requires an argument.\n", stderr);
                        return 1;
                    }
                    inputFilename = argv[i];
                    break;

                default:
                    fprintf(stderr, "Unknown option: %c\n", argv[i][1]);
                    return 1;
                }
            }
        }
        else
        {
            name = StringPoolAdd(argv[i]);
            IVAdd(&targets, intFromRef(name));
        }
    }
    if (inputFilename)
    {
        char *slash = strrchr(inputFilename, '/');
        if (slash)
        {
            *slash = 0;
            if (chdir(inputFilename))
            {
                FailIO("Error changing directory", inputFilename);
            }
            inputFilename = slash + 1;
        }
    }
    else
    {
        inputFilename = "build.don";
    }

    EnvInit(environ);
    FileInit();

    EnvGet("XDG_CACHE_HOME", 14, &env, &envLength);
    if (envLength)
    {
        string = FileCreatePath(null, 0, env, envLength, null, 0, &envLength);
        cacheDirectory = FileCreatePath(string, envLength, "don/", 4,
                                        null, 0, &cacheDirectoryLength);
        free(string);
    }
    else
    {
        EnvGet("HOME", 4, &env, &envLength);
        if (!envLength)
        {
            fprintf(stderr,
                    "No suitable location for cache directory found.\n");
            return 1;
        }
        string = FileCreatePath(null, 0, env, envLength, null, 0, &envLength);
        cacheDirectory = FileCreatePath(string, envLength, ".cache/don/", 11,
                                        null, 0, &cacheDirectoryLength);
        free(string);
    }

    if (!IVSize(&targets))
    {
        name = StringPoolAdd("default");
        IVAdd(&targets, intFromRef(name));
    }

    NamespaceInit();
    NativeInit();

    string = FileCreatePath(null, 0,
                            DATADIR "don.don", strlen(DATADIR "don.don"),
                            null, 0,
                            &size);
    ParseInit(&parsed);
    ParseFile(&parsed, string, size, NamespaceCreate(StringPoolAdd("don")));
    free(string);

    string = FileCreatePath(null, 0,
                            inputFilename, strlen(inputFilename),
                            null, 0,
                            &size);
    defaultNamespace = NamespaceCreate(0);
    ParseFile(&parsed, string, size, defaultNamespace);
    free(string);

    ParseDispose();


    if (DEBUG_DISASSEMBLE)
    {
        BytecodeDisassemble(IVGetPointer(&parsed.bytecode, 0),
                            IVGetPointer(&parsed.bytecode, 0) + IVSize(&parsed.bytecode));
        fflush(stdout);
    }
    if (!Link(&parsed, &linked))
    {
        return 1;
    }

    if (DEBUG_DISASSEMBLE)
    {
        BytecodeDisassemble(linked.bytecode, linked.bytecode + linked.size);
        fflush(stdout);
    }

    fail = false;
    for (j = 0; j < IVSize(&targets); j++)
    {
        name = refFromInt(IVGet(&targets, j));
        if (NamespaceGetTarget(defaultNamespace, name) < 0)
        {
            fprintf(stderr, "'%s' is not a target.\n", VGetString(name));
            fail = true;
        }
    }
    if (fail)
    {
        return 1;
    }
    StringPoolDispose();

    CacheInit(cacheDirectory, cacheDirectoryLength);
    WorkInit();
    for (j = 0; j < IVSize(&targets); j++)
    {
        InterpreterExecute(
            &linked, linked.functions[NamespaceGetTarget(defaultNamespace,
                                                         refFromInt(IVGet(&targets, j)))]);
    }

#ifdef VALGRIND
    free(linked.bytecode);
    free(linked.lineNumbers);
    free(linked.functions);
    free(linked.constants);
    free(linked.fields);
#endif
    cleanShutdown(EXIT_SUCCESS);
}

void cleanShutdown(int exitcode)
{
    static bool shuttingDown;
    if (shuttingDown)
    {
        exit(EXIT_FAILURE);
    }
    shuttingDown = true;
    CacheDispose();
#ifdef VALGRIND
    IVDispose(&targets);
    WorkDispose();
    HeapDispose();
    NamespaceDispose();
    FileDisposeAll();
    EnvDispose();
    StringPoolDispose();
    LogDispose();
#endif
    exit(exitcode);
}


#ifdef DEBUG
#include <execinfo.h>
#include <signal.h>

#pragma GCC diagnostic ignored "-Wconversion"
void _assert(const char *expression, const char *file, int line)
{
    void *backtraceData[128];
    uint frames;

    fflush(stdout);
#ifdef VALGRIND
    if (RUNNING_ON_VALGRIND)
    {
        VALGRIND_PRINTF_BACKTRACE("Assertion failed: %s:%d: %s\n",
                                  file, line, expression);
    }
    else
#endif
    {
        fprintf(stderr, "Assertion failed: %s:%d: %s\n", file, line, expression);
        frames = (uint)backtrace(backtraceData, sizeof(backtraceData) / sizeof(void*));
        backtrace_symbols_fd(&backtraceData[1], (int)frames - 1, 1);
    }
    raise(SIGABRT);
    unreachable;
}
#endif

#undef calloc
void *mycalloc(size_t count, size_t eltsize)
{
    void *p = calloc(count, eltsize);
    if (unlikely(!p))
    {
        FailOOM();
    }
    return p;
}

#undef malloc
void *mymalloc(size_t size)
{
    void *p = malloc(size);
    if (unlikely(!p))
    {
        FailOOM();
    }
    return p;
}

#undef realloc
void *myrealloc(void *ptr, size_t size)
{
    void *p = realloc(ptr, size);
    if (unlikely(!p))
    {
        FailOOM();
    }
    return p;
}
