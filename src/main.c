#include <stdio.h>
#include "builder.h"
#include "bytevector.h"
#include "stringpool.h"
#include "native.h"
#include "fileindex.h"
#include "targetindex.h"
#include "parser.h"

#include "instruction.h"
static void dump(const bytevector *bytecode)
{
    uint ip;
    uint readIndex;
    uint dataSize;
    uint controlSize;
    uint stop;
    uint i;
    uint function;
    uint argumentCount;
    uint condition;
    uint value;
    uint target;
    uint stackframe;
    stringref name;

    printf("Dump pass 1\n");
    for (readIndex = 0; readIndex < ByteVectorSize(bytecode);)
    {
        dataSize = ByteVectorReadPackUint(bytecode, &readIndex);
        printf("data, size=%d\n", dataSize);
        for (stop = readIndex + dataSize; readIndex < stop;)
        {
            ip = readIndex;
            switch (ByteVectorRead(bytecode, &readIndex))
            {
            case DATAOP_NULL:
                printf("%d: null\n", ip);
                break;
            case DATAOP_STRING:
                value = ByteVectorReadPackUint(bytecode, &readIndex);
                printf("%d: string %d:\"%s\"\n", ip, value,
                       StringPoolGetString((stringref)value));
                break;
            case DATAOP_PHI_VARIABLE:
                condition = ByteVectorReadUint(bytecode, &readIndex);
                value = ByteVectorReadUint(bytecode, &readIndex);
                printf("%d: phi variable condition=%d %d %d\n", ip, condition,
                       value, ByteVectorReadInt(bytecode, &readIndex));
                break;
            case DATAOP_PARAMETER:
                name = (stringref)ByteVectorReadPackUint(bytecode, &readIndex);
                printf("%d: parameter name=%s\n", ip, StringPoolGetString(name));
                break;
            case DATAOP_RETURN:
                stackframe = ByteVectorReadPackUint(bytecode, &readIndex);
                value = ByteVectorReadPackUint(bytecode, &readIndex);
                printf("%d: return %d from %d\n", ip, value, stackframe);
                break;
            case DATAOP_STACKFRAME:
                printf("%d: stackframe\n", ip);
                break;
            default:
                assert(false);
                break;
            }
        }
        controlSize = ByteVectorReadPackUint(bytecode, &readIndex);
        printf("control, size=%d\n", controlSize);
        for (stop = readIndex + controlSize; readIndex < stop;)
        {
            ip = readIndex;
            switch (ByteVectorRead(bytecode, &readIndex))
            {
            case OP_NOOP:
                printf("%d: noop\n", ip);
                break;
            case OP_RETURN:
                printf("%d: return\n", ip);
                break;
            case OP_BRANCH:
                target = ByteVectorReadUint(bytecode, &readIndex);
                condition = ByteVectorReadPackUint(bytecode, &readIndex);
                printf("%d: branch condition=%d target=%d\n", ip, condition,
                       target);
                break;
            case OP_LOOP:
                target = ByteVectorReadPackUint(bytecode, &readIndex);
                printf("%d: loop %d\n", ip, target);
                break;
            case OP_JUMP:
                target = ByteVectorReadUint(bytecode, &readIndex);
                printf("%d: jump %d\n", ip, target);
                break;
            case OP_INVOKE_NATIVE:
                function = ByteVectorRead(bytecode, &readIndex);
                value = ByteVectorReadPackUint(bytecode, &readIndex);
                argumentCount = ByteVectorReadPackUint(bytecode, &readIndex);
                printf("%d: invoke native function=%d, arguments=%d, stackframe=%d\n",
                       ip, function, argumentCount, value);
                for (i = 0; i < argumentCount; i++)
                {
                    printf("  %d: argument %d\n", i,
                           ByteVectorReadUint(bytecode, &readIndex));
                }
                break;
            case OP_COND_INVOKE:
                condition = ByteVectorReadPackUint(bytecode, &readIndex);
                value = ByteVectorReadPackUint(bytecode, &readIndex);
                function = ByteVectorReadPackUint(bytecode, &readIndex);
                argumentCount = ByteVectorReadPackUint(bytecode, &readIndex);
                printf("%d: cond_invoke function=%d, condition=%d, arguments=%d, stackframe=%d\n",
                       ip, function, condition, argumentCount, value);
                for (i = 0; i < argumentCount; i++)
                {
                    printf("  %d: argument %d\n", i,
                           ByteVectorReadPackUint(bytecode, &readIndex));
                }
                break;
            default:
                assert(false);
                break;
            }
        }
    }
}

int main(int argc, const char **argv)
{
    int i;
    const char *options;
    const char *inputFilename = null;
    fileref inputFile;
    targetref target;
    boolean parseOptions = true;
    bytevector bytecode;

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
    printf("input=%s\n", inputFilename);

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

    ByteVectorInit(&bytecode);
    target = TargetIndexGet(StringPoolAdd("default"));
    assert(target >= 0); /* TODO: Error handling for non-existing target */
    if (!ParseTarget(target, &bytecode))
    {
        assert(false); /* TODO: Error handling */
    }
    dump(&bytecode);
    printf("Bytecode offset=%d\n", TargetIndexGetBytecodeOffset(target));

    ByteVectorFree(&bytecode);
    TargetIndexFree();
    FileIndexFree();
    StringPoolFree();
    return 0;
}
