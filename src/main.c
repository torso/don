#define _XOPEN_SOURCE 600
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include <memory.h>
#include <unistd.h>
#include "common.h"
#include "vm.h"
#include "bytecode.h"
#include "cache.h"
#include "env.h"
#include "fail.h"
#include "fieldindex.h"
#include "file.h"
#include "functionindex.h"
#include "interpreter.h"
#include "log.h"
#include "namespace.h"
#include "native.h"
#include "parser.h"
#include "stringpool.h"
#include "work.h"


static intvector targets;
static bytevector parsed;


static void cleanup(void)
{
    IVDispose(&targets);
    BVDispose(&parsed);
    WorkDispose();
    HeapDispose();
    NamespaceDispose();
    FieldIndexDispose();
    FunctionIndexDispose();
    CacheDispose();
    FileDisposeAll();
    EnvDispose();
    StringPoolDispose();
    LogDispose();
}

int main(int argc, const char **argv)
{
    int i;
    uint j;
    const char *options;
    const char *inputFilename = null;
    const char *env;
    size_t envLength;
    char *string;
    vref filename;
    File inputFile;
    char *cacheDirectory;
    size_t cacheDirectoryLength;
    File donNamespaceFile;
    namespaceref defaultNamespace;
    vref name;
    fieldref field;
    functionref function;
    boolean parseOptions = true;
    boolean disassemble = false;
    byte *bytecode;
    const byte *bytecodeLimit;
    const byte *p;
    size_t size;
    boolean fail;

    atexit(cleanup);

    IVInit(&targets, 4);
    LogInit();
    HeapInit();
    FileInit();

    for (i = 1; i < argc; i++)
    {
        if (parseOptions && argv[i][0] == '-')
        {
            options = argv[i] + 1;
            if (!*options)
            {
                fprintf(stderr, "Invalid argument: \"-\"\n");
                return 1;
            }
            if (*options == '-')
            {
                if (*++options)
                {
                    fprintf(stderr, "TODO: Long option\n");
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
#ifdef DEBUG
                case 'd':
                    disassemble = true;
                    break;
#endif

                case 'f':
                    if (inputFilename)
                    {
                        fprintf(stderr, "More than one input file specified.\n");
                        return 1;
                    }
                    if (++i >= argc)
                    {
                        fprintf(stderr, "Option \"-f\" requires an argument.\n");
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
            IVAddRef(&targets, name);
        }
    }
    if (!inputFilename)
    {
        inputFilename = "build.don";
    }

    EnvInit(environ);

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
        IVAddRef(&targets, name);
    }

    FunctionIndexInit();
    FunctionIndexAddFunction(0, StringPoolAdd(""), 0, 0, 0);
    FieldIndexInit();
    NamespaceInit();
    NativeInit();

    BVInit(&parsed, 65536);

    string = FileCreatePath(null, 0,
                            DATADIR "don.don", strlen(DATADIR "don.don"),
                            null, 0,
                            &size);
    filename = StringPoolAdd2(string, size);
    free(string);
    FileOpen(&donNamespaceFile, HeapGetString(filename),
             HeapStringLength(filename));
    FileMMap(&donNamespaceFile, &p, &size);
    ParseFile(filename, NamespaceCreate(StringPoolAdd("don")));

    string = FileCreatePath(null, 0,
                            inputFilename, strlen(inputFilename),
                            null, 0,
                            &size);
    filename = StringPoolAdd2(string, size);
    free(string);
    FileOpen(&inputFile, HeapGetString(filename), HeapStringLength(filename));
    FileMMap(&inputFile, &p, &size);
    defaultNamespace = NamespaceCreate(0);
    ParseFile(filename, defaultNamespace);

    for (field = FieldIndexGetFirstField();
         field;
         field = FieldIndexGetNextField(field))
    {
        ParseField(field, &parsed);
    }

    for (function = FunctionIndexGetNextFunction(
             FunctionIndexGetFirstFunction());
         function;
         function = FunctionIndexGetNextFunction(function))
    {
        ParseFunctionDeclaration(function, &parsed);
    }

    bytecode = BVDisposeContainer(&parsed);
    BVInit(&parsed, 65536);
    FieldIndexFinishBytecode(bytecode, &parsed);
    free(bytecode);
    bytecode = null;

    for (function = FunctionIndexGetNextFunction(
             FunctionIndexGetFirstFunction());
         function;
         function = FunctionIndexGetNextFunction(function))
    {
        ParseFunctionBody(function, &parsed);
    }

    FileClose(&donNamespaceFile);
    FileClose(&inputFile);
    size = BVSize(&parsed);
    bytecode = BVDisposeContainer(&parsed);
    bytecodeLimit = bytecode + size;

    if (disassemble)
    {
        for (function = FunctionIndexGetFirstFunction();
             function;
             function = FunctionIndexGetNextFunction(function))
        {
            if (function == FunctionIndexGetFirstFunction())
            {
                printf("Init:\n");
                BytecodeDisassembleFunction(
                    bytecode + FunctionIndexGetBytecodeOffset(function),
                    bytecodeLimit);
            }
            else if (FunctionIndexGetBytecodeOffset(function))
            {
                printf("Function %s:\n",
                       HeapGetString(FunctionIndexGetName(function)));
                BytecodeDisassembleFunction(
                    bytecode + FunctionIndexGetBytecodeOffset(function),
                    bytecodeLimit);
            }
        }
        fflush(stdout);
    }

    if (LogFlushParseErrors())
    {
        return 1;
    }
    fail = false;
    for (j = 0; j < IVSize(&targets); j++)
    {
        name = IVGetRef(&targets, j);
        if (!NamespaceGetTarget(defaultNamespace, name))
        {
            fprintf(stderr, "'%s' is not a target.\n", HeapGetString(name));
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
        InterpreterExecute(bytecode,
                           NamespaceGetTarget(defaultNamespace,
                                              IVGetRef(&targets, j)));
    }

    free(bytecode);
    return 0;
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
}
#endif

#undef calloc
void *mycalloc(size_t count, size_t eltsize)
{
    void *p = calloc(count, eltsize);
    if (!p)
    {
        FailOOM();
    }
    return p;
}

#undef malloc
void *mymalloc(size_t size)
{
    void *p = malloc(size);
    if (!p)
    {
        FailOOM();
    }
    return p;
}

#undef realloc
void *myrealloc(void *ptr, size_t size)
{
    void *p = realloc(ptr, size);
    if (!p)
    {
        FailOOM();
    }
    return p;
}
