#include <stdio.h>
#include "builder.h"
#include "bytevector.h"
#include "intvector.h"
#include "stringpool.h"
#include "fileindex.h"
#include "targetindex.h"
#include "parser.h"
#include "bytecodegenerator.h"
#include "interpreterstate.h"
#include "interpreter.h"
#include "native.h"

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

static bytevector parsed;
static bytevector bytecode;
static bytevector valueBytecode;

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
    ByteVectorDispose(&parsed);
    ByteVectorDispose(&bytecode);
    ByteVectorDispose(&valueBytecode);

    TargetIndexDispose();
    FileIndexDispose();
    StringPoolDispose();
}

static targetref getTarget(const char *name)
{
    targetref target = TargetIndexGet(StringPoolAdd(name));
    if (!target || !TargetIndexIsTarget(target))
    {
        printf("'%s' is not a target.\n", name);
        return 0;
    }
    return target;
}

int main(int argc, const char **argv)
{
    int i;
    const char *options;
    const char *inputFilename = null;
    fileref inputFile;
    targetref target;
    targetref unparsedTarget;
    boolean parseOptions = true;

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

    StringPoolInit();
    ParserAddKeywords();
    if (handleError(TargetIndexInit()))
    {
        cleanup();
        return 1;
    }
    ByteVectorInit(&parsed);
    ByteVectorInit(&bytecode);
    ByteVectorInit(&valueBytecode);

    inputFile = FileIndexAdd(inputFilename);
    assert(inputFile);
    if (handleError(ParseFile(inputFile)) || !TargetIndexBuildIndex())
    {
        cleanup();
        return 1;
    }

    target = getTarget("default");
    if (!target)
    {
        cleanup();
        return 1;
    }
    TargetIndexMarkForParsing(target);
    for (;;)
    {
        unparsedTarget = TargetIndexPopUnparsedTarget();
        if (!unparsedTarget)
        {
            break;
        }
        if (handleError(ParseFunction(unparsedTarget, &parsed)))
        {
            cleanup();
            return 1;
        }
    }

    NativeWriteBytecode(&bytecode, &valueBytecode);
    if (handleError(
            BytecodeGeneratorExecute(&parsed, &bytecode, &valueBytecode)))
    {
        cleanup();
        return 1;
    }
    ByteVectorDispose(&parsed);

    if (handleError(InterpreterExecute(&bytecode, &valueBytecode, target)))
    {
        cleanup();
        return 1;
    }

    cleanup();
    return 0;
}
