#include <stdio.h>
#include "builder.h"
#include "bytevector.h"
#include "stringpool.h"
#include "native.h"
#include "fileindex.h"
#include "targetindex.h"
#include "parser.h"
#include "bytecodegenerator.h"
#include "interpreter.h"

int main(int argc, const char **argv)
{
    int i;
    const char *options;
    const char *inputFilename = null;
    fileref inputFile;
    targetref target;
    boolean parseOptions = true;
    bytevector parsed;
    bytevector bytecode;
    bytevector valueBytecode;

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
    TargetIndexInit();
    inputFile = FileIndexAdd(inputFilename);
    assert(inputFile);
    if (!ParseFile(inputFile))
    {
        assert(false); /* TODO: Error handling */
    }
    TargetIndexFinish();

    ByteVectorInit(&parsed);
    target = TargetIndexGet(StringPoolAdd("default"));
    assert(target >= 0); /* TODO: Error handling for non-existing target */
    if (!ParseTarget(target, &parsed))
    {
        assert(false); /* TODO: Error handling */
    }

    ByteVectorInit(&bytecode);
    ByteVectorInit(&valueBytecode);
    NativeWriteBytecode(&bytecode, &valueBytecode);
    BytecodeGeneratorExecute(&parsed, &bytecode, &valueBytecode);
    TargetIndexDisposeParsed();
    ByteVectorFree(&parsed);

    InterpreterExecute(&bytecode, &valueBytecode, target);

    ByteVectorFree(&bytecode);
    ByteVectorFree(&valueBytecode);
    TargetIndexFree();
    FileIndexFree();
    StringPoolFree();
    return 0;
}
