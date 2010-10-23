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


ref_t refFromUint(uint i)
{
    return (ref_t)i;
}

ref_t refFromSize(size_t i)
{
    assert(i <= UINT_MAX);
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


#ifdef DEBUG
#include <execinfo.h>
#include <signal.h>

void _assert(const char *expression, const char *file, int line)
{
    void *backtraceData[128];
    uint frames;

    printf("Assertion failed: %s:%d: %s\n", file, line, expression);
    fflush(stdout);
    frames = (uint)backtrace(backtraceData, sizeof(backtraceData) / sizeof(void*));
    backtrace_symbols_fd(&backtraceData[1], (int)frames - 1, 1);
    raise(SIGABRT);
}
#endif

static void cleanup(void)
{
    NamespaceDispose();
    FieldIndexDispose();
    FunctionIndexDispose();
    FileDisposeAll();
    StringPoolDispose();
    LogDispose();
}

int main(int argc, const char **argv)
{
    int i;
    uint j;
    const char *options;
    const char *inputFilename = null;
    fileref inputFile;
    intvector targets;
    stringref name;
    fieldref field;
    functionref function;
    boolean parseOptions = true;
    boolean disassemble = false;
    bytevector parsed;
    byte *bytecode;
    const byte *bytecodeLimit;
    size_t bytecodeSize;
    boolean fail;

    IntVectorInit(&targets);
    LogInit();
    StringPoolInit();
    ParserAddKeywords();

    for (i = 1; i < argc; i++)
    {
        if (parseOptions && argv[i][0] == '-')
        {
            options = argv[i] + 1;
            if (!*options)
            {
                printf("Invalid argument: \"-\"\n");
                return 1;
            }
            if (*options == '-')
            {
                if (*++options)
                {
                    printf("TODO: Long option\n");
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
                        printf("Input file already specified\n");
                        return 1;
                    }
                    if (++i >= argc)
                    {
                        printf("Option \"-i\" requires an argument\n");
                        return 1;
                    }
                    inputFilename = argv[i];
                    break;

                default:
                    printf("Unknown option: %c\n", argv[i][1]);
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
    if (!IntVectorSize(&targets))
    {
        name = StringPoolAdd("default");
        IntVectorAddRef(&targets, name);
    }

    FileInit();
    FunctionIndexInit();
    FunctionIndexAddFunction(StringPoolAdd(""), 0, 0, 0);
    FieldIndexInit();
    NamespaceInit();
    ByteVectorInit(&parsed, 65536);

    inputFile = FileAdd(inputFilename, strlen(inputFilename));
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

    NativeInit(&parsed);

    bytecode = ByteVectorDisposeContainer(&parsed);
    ByteVectorInit(&parsed, 65536);
    FieldIndexFinishBytecode(bytecode, &parsed);
    free(bytecode);

    for (function = FunctionIndexGetNextFunction(
             FunctionIndexGetFirstFunction());
         function;
         function = FunctionIndexGetNextFunction(function))
    {
        ParseFunctionBody(function, &parsed);
    }

    FileDispose(inputFile);
    bytecodeSize = ByteVectorSize(&parsed);
    bytecode = ByteVectorDisposeContainer(&parsed);
    bytecodeLimit = bytecode + bytecodeSize;

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
        IntVectorDispose(&targets);
        free(bytecode);
        cleanup();
        return 1;
    }
    fail = false;
    for (j = 0; j < IntVectorSize(&targets); j++)
    {
        name = IntVectorGetRef(&targets, j);
        if (!NamespaceGetTarget(name))
        {
            printf("'%s' is not a target.\n", StringPoolGetString(name));
            fail = true;
        }
    }
    if (fail)
    {
        IntVectorDispose(&targets);
        free(bytecode);
        cleanup();
        return 1;
    }
    FileMkdir(FileAdd(".don", 4));
    CacheInit();

    for (j = 0; j < IntVectorSize(&targets); j++)
    {
        function = NamespaceGetTarget(IntVectorGetRef(&targets, j));
        assert(function);
        InterpreterExecute(bytecode, function);
    }

    IntVectorDispose(&targets);
    free(bytecode);
    CacheDispose();
    cleanup();
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
