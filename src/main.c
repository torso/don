#include <stdio.h>
#include "builder.h"
#include "bytecode.h"
#include "bytevector.h"
#include "fieldindex.h"
#include "fileindex.h"
#include "functionindex.h"
#include "heap.h"
#include "interpreter.h"
#include "namespace.h"
#include "native.h"
#include "parser.h"
#include "stringpool.h"

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

    case BUILD_ERROR:
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

static functionref getTarget(const char *name)
{
    stringref string = StringPoolAdd(name);
    functionref function;
    if (!string)
    {
        handleError(OUT_OF_MEMORY);
        return 0;
    }
    function = NamespaceGetTarget(string);
    if (!function)
    {
        printf("'%s' is not a target.\n", name);
        return 0;
    }
    return function;
}

int main(int argc, const char **argv)
{
    int i;
    const char *options;
    const char *inputFilename = null;
    fileref inputFile;
    stringref initFunctionName;
    fieldref field;
    functionref function;
    boolean parseOptions = true;
    boolean disassemble = false;
    ErrorCode error;
    boolean parseFailed = false;
    bytevector parsed;
    byte *bytecode;

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
            printf("TODO: target=%s\n", argv[i]);
            return 1;
        }
    }
    if (!inputFilename)
    {
        inputFilename = "build.don";
    }

    if (handleError(StringPoolInit()) ||
        handleError(ParserAddKeywords()) ||
        handleError(FunctionIndexInit()))
    {
        cleanup();
        return 1;
    }
    initFunctionName = StringPoolAdd("");
    if (handleError(initFunctionName ? NO_ERROR : OUT_OF_MEMORY) ||
        handleError(FunctionIndexBeginFunction(initFunctionName)) ||
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
            if (error == BUILD_ERROR)
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
            if (error == BUILD_ERROR)
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
    bytecode = ByteVectorDisposeContainer(&parsed);

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
                bytecode + FunctionIndexGetBytecodeOffset(function));
        }
    }

    function = getTarget("default");
    if (!function || parseFailed)
    {
        free(bytecode);
        cleanup();
        return 1;
    }

    if (handleError(InterpreterExecute(bytecode, function)))
    {
        cleanup();
        return 1;
    }

    free(bytecode);
    cleanup();
    return 0;
}
