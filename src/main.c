#define _XOPEN_SOURCE 600
#include <stdarg.h>
#include <stdio.h>
#include <memory.h>
#include "common.h"
#include "vm.h"
#include "bytecode.h"
#include "cache.h"
#include "fieldindex.h"
#include "file.h"
#include "functionindex.h"
#include "interpreter.h"
#include "log.h"
#include "namespace.h"
#include "native.h"
#include "parser.h"
#include "stringpool.h"
#include "task.h"


static intvector targets;
static bytevector parsed;
static byte *bytecode;
static VM vm;


ref_t refFromUint(uint i)
{
    return (ref_t)i;
}

ref_t refFromSize(size_t i)
{
    assert(i <= UINT_MAX - 1);
    return refFromUint((uint)i);
}

size_t sizeFromRef(ref_t r)
{
    return uintFromRef(r);
}

uint uintFromRef(ref_t r)
{
    return (uint)r;
}


static void cleanup(void)
{
    IntVectorDispose(&targets);
    ByteVectorDispose(&parsed);
    free(bytecode);
    InterpreterDispose(&vm);
    NamespaceDispose();
    FieldIndexDispose();
    FunctionIndexDispose();
    CacheDispose();
    FileDisposeAll();
    StringPoolDispose();
    LogDispose();
}

#ifdef DEBUG
#include <execinfo.h>
#include <signal.h>

void _assert(const char *expression, const char *file, int line)
{
    void *backtraceData[128];
    uint frames;

    fflush(stdout);
    fprintf(stderr, "Assertion failed: %s:%d: %s\n", file, line, expression);
    frames = (uint)backtrace(backtraceData, sizeof(backtraceData) / sizeof(void*));
    backtrace_symbols_fd(&backtraceData[1], (int)frames - 1, 1);
    cleanup();
    raise(SIGABRT);
}
#endif

int main(int argc, const char **argv)
{
    int i;
    uint j;
    const char *options;
    const char *inputFilename = null;
    const char *env;
    const char *filename;
    fileref inputFile;
    fileref cacheDirectory = 0;
    fileref donNamespaceFile;
    namespaceref defaultNamespace;
    stringref name;
    fieldref field;
    functionref function;
    boolean parseOptions = true;
    boolean disassemble = false;
    const byte *bytecodeLimit;
    const byte *p;
    size_t size;
    boolean fail;

    atexit(cleanup);

    IntVectorInit(&targets);
    LogInit();
    StringPoolInit();
    ParserAddKeywords();
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

                case 'i':
                    if (inputFilename)
                    {
                        fprintf(stderr, "More than one input file specified.\n");
                        return 1;
                    }
                    if (++i >= argc)
                    {
                        fprintf(stderr, "Option \"-i\" requires an argument.\n");
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
            IntVectorAddRef(&targets, name);
        }
    }
    if (!inputFilename)
    {
        inputFilename = "build.don";
    }

    unsetenv("COLORTERM");
    if (setenv("TERM", "dumb", 1))
    {
        TaskFailErrno(false);
    }

    env = getenv("XDG_CACHE_HOME");
    if (env && env[0])
    {
        cacheDirectory = FileAddRelative(env, strlen(env), "don", 3);
    }
    else
    {
        env = getenv("HOME");
        if (!env || !env[0])
        {
            fprintf(stderr,
                    "No suitable location for cache directory found.\n");
            return 1;
        }
        cacheDirectory = FileAddRelative(env, strlen(env), ".cache/don", 10);
    }

    if (!IntVectorSize(&targets))
    {
        name = StringPoolAdd("default");
        IntVectorAddRef(&targets, name);
    }

    FunctionIndexInit();
    FunctionIndexAddFunction(StringPoolAdd(""), 0, 0, 0);
    FieldIndexInit();
    NamespaceInit();
    NativeInit();

    ByteVectorInit(&parsed, 65536);
    filename = DATADIR "don.don";
    donNamespaceFile = FileAdd(filename, strlen(filename));
    FileMMap(donNamespaceFile, &p, &size, true);
    NamespaceCreate(donNamespaceFile, StringPoolAdd("don"));
    ParseFile(donNamespaceFile);
    inputFile = FileAdd(inputFilename, strlen(inputFilename));
    FileMMap(inputFile, &p, &size, true);
    defaultNamespace = NamespaceCreate(inputFile, 0);
    ParseFile(inputFile);

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

    bytecode = ByteVectorDisposeContainer(&parsed);
    ByteVectorInit(&parsed, 65536);
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

    FileMUnmap(donNamespaceFile);
    FileMUnmap(inputFile);
    FileDispose(donNamespaceFile);
    FileDispose(inputFile);
    size = ByteVectorSize(&parsed);
    bytecode = ByteVectorDisposeContainer(&parsed);
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
                       StringPoolGetString(FunctionIndexGetName(function)));
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
    for (j = 0; j < IntVectorSize(&targets); j++)
    {
        name = IntVectorGetRef(&targets, j);
        if (!NamespaceGetTarget(defaultNamespace, name))
        {
            fprintf(stderr, "'%s' is not a target.\n",
                    StringPoolGetString(name));
            fail = true;
        }
    }
    if (fail)
    {
        return 1;
    }
    CacheInit(cacheDirectory);

    for (j = 0; j < IntVectorSize(&targets); j++)
    {
        function = NamespaceGetTarget(defaultNamespace,
                                      IntVectorGetRef(&targets, j));
        assert(function);
        InterpreterInit(&vm, bytecode);
        InterpreterExecute(&vm, function);
        InterpreterDispose(&vm);
    }

    return 0;
}

#undef calloc
void *mycalloc(size_t count, size_t eltsize)
{
    void *p = calloc(count, eltsize);
    if (!p)
    {
        TaskFailOOM();
    }
    return p;
}

#undef malloc
void *mymalloc(size_t size)
{
    void *p = malloc(size);
    if (!p)
    {
        TaskFailOOM();
    }
    return p;
}

#undef realloc
void *myrealloc(void *ptr, size_t size)
{
    void *p = realloc(ptr, size);
    if (!p)
    {
        TaskFailOOM();
    }
    return p;
}
