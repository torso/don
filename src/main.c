#include <stdio.h>
#include "common.h"
#include "vm.h"
#include "bytecode.h"
#include "fieldindex.h"
#include "fileindex.h"
#include "functionindex.h"
#include "interpreter.h"
#include "namespace.h"
#include "native.h"
#include "parser.h"
#include "stringpool.h"


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
    frames = (uint)backtrace(backtraceData, sizeof(backtraceData) / sizeof(void*));
    backtrace_symbols_fd(&backtraceData[1], (int)frames - 1, 1);
    raise(SIGABRT);
}
#endif

static boolean handleError(ErrorCode error)
{
    switch (error)
    {
    case NO_ERROR:
        return false;

    case OUT_OF_MEMORY:
        printf("Out of memory\n");
        break;

    case ERROR_FAIL:
        break;
    }
    return true;
}

static void cleanup(void)
{
    NamespaceDispose();
    FieldIndexDispose();
    FunctionIndexDispose();
    FileIndexDispose();
    StringPoolDispose();
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
    ErrorCode error;
    boolean parseFailed = false;
    bytevector parsed;
    byte *bytecode;
    const byte *bytecodeLimit;
    size_t bytecodeSize;

    if (handleError(IntVectorInit(&targets)) ||
        handleError(StringPoolInit()) ||
        handleError(ParserAddKeywords()) ||
        handleError(StringPoolAdd("") ? NO_ERROR : OUT_OF_MEMORY))
    {
        return 1;
    }

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
            if (handleError(name ? NO_ERROR : OUT_OF_MEMORY) ||
                handleError(IntVectorAddRef(&targets, name)))
            {
                return 1;
            }
        }
    }
    if (!inputFilename)
    {
        inputFilename = "build.don";
    }
    if (!IntVectorSize(&targets))
    {
        name = StringPoolAdd("default");
        if (handleError(name ? NO_ERROR : OUT_OF_MEMORY) ||
            handleError(IntVectorAddRef(&targets, name)))
        {
            return 1;
        }
    }

    if (handleError(FileIndexInit()) ||
        handleError(FunctionIndexInit()) ||
        handleError(FunctionIndexBeginFunction(StringPoolAdd(""))) ||
        handleError(FieldIndexInit()) ||
        handleError(NamespaceInit()) ||
        handleError(NativeInit()) ||
        handleError(ByteVectorInit(&parsed)))
    {
        cleanup();
        return 1;
    }
    FunctionIndexFinishFunction(0, 0, 0);

    inputFile = FileIndexOpen(inputFilename);
    assert(inputFile);
    if (handleError(ParseFile(inputFile)))
    {
        ByteVectorDispose(&parsed);
        cleanup();
        return 1;
    }

    for (field = FieldIndexGetFirstField();
         field;
         field = FieldIndexGetNextField(field))
    {
        error = ParseField(field, &parsed);
        if (error)
        {
            if (error == ERROR_FAIL)
            {
                parseFailed = true;
            }
            else
            {
                handleError(error);
                ByteVectorDispose(&parsed);
                cleanup();
                return 1;
            }
        }
    }

    bytecode = ByteVectorDisposeContainer(&parsed);
    if (handleError(ByteVectorInit(&parsed)))
    {
        free(bytecode);
        cleanup();
        return 1;
    }
    if (!parseFailed)
    {
        if (handleError(FieldIndexFinishBytecode(bytecode, &parsed)))
        {
            free(bytecode);
            ByteVectorDispose(&parsed);
            cleanup();
            return 1;
        }
    }
    free(bytecode);

    for (function = FunctionIndexGetNextFunction(
             FunctionIndexGetFirstFunction());
         function;
         function = FunctionIndexGetNextFunction(function))
    {
        error = ParseFunction(function, &parsed);
        if (error)
        {
            if (error == ERROR_FAIL)
            {
                parseFailed = true;
            }
            else
            {
                handleError(error);
                ByteVectorDispose(&parsed);
                cleanup();
                return 1;
            }
        }
    }

    FileIndexClose(inputFile);
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
            }
            else
            {
                printf("Function %s:\n",
                       StringPoolGetString(FunctionIndexGetName(function)));
            }
            BytecodeDisassembleFunction(
                bytecode + FunctionIndexGetBytecodeOffset(function),
                bytecodeLimit);
        }
    }

    for (j = 0; j < IntVectorSize(&targets); j++)
    {
        name = IntVectorGetRef(&targets, j);
        if (!NamespaceGetTarget(name))
        {
            printf("'%s' is not a target.\n", StringPoolGetString(name));
            parseFailed = true;
        }
    }
    if (parseFailed)
    {
        IntVectorDispose(&targets);
        free(bytecode);
        cleanup();
        return 1;
    }

    for (j = 0; j < IntVectorSize(&targets); j++)
    {
        function = NamespaceGetTarget(IntVectorGetRef(&targets, j));
        assert(function);
        if (handleError(InterpreterExecute(bytecode, function)))
        {
            IntVectorDispose(&targets);
            free(bytecode);
            cleanup();
            return 1;
        }
    }

    IntVectorDispose(&targets);
    free(bytecode);
    cleanup();
    return 0;
}
