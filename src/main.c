#include <stdio.h>
#include "builder.h"
#include "bytecode.h"
#include "bytevector.h"
#include "fileindex.h"
#include "interpreter.h"
#include "native.h"
#include "parser.h"
#include "stringpool.h"
#include "functionindex.h"

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
    function = FunctionIndexGet(string);
    if (!function || !FunctionIndexIsTarget(function))
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
        handleError(FunctionIndexInit()) ||
        handleError(NativeInit()) ||
        handleError(ByteVectorInit(&parsed)))
    {
        cleanup();
        return 1;
    }

    inputFile = FileIndexAdd(inputFilename);
    assert(inputFile);
    if (handleError(ParseFile(inputFile)) || !FunctionIndexBuildIndex())
    {
        ByteVectorDispose(&parsed);
        cleanup();
        return 1;
    }

    for (function = FunctionIndexGetFirstFunction();
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

    bytecode = ByteVectorDisposeContainer(&parsed);
    function = getTarget("default");
    if (!function || parseFailed)
    {
        free(bytecode);
        cleanup();
        return 1;
    }

    if (disassemble)
    {
        BytecodeDisassembleFunction(bytecode);
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
