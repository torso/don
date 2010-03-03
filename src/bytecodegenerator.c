#include <stdlib.h>
#include "builder.h"
#include "bytevector.h"
#include "stringpool.h"
#include "instruction.h"
#include "bytecodegenerator.h"

static void dump(const bytevector *parsed)
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
    for (readIndex = 0; readIndex < ByteVectorSize(parsed);)
    {
        dataSize = ByteVectorReadPackUint(parsed, &readIndex);
        printf("data, size=%d\n", dataSize);
        for (stop = readIndex + dataSize, ip = 0; readIndex < stop; ip++)
        {
            switch (ByteVectorRead(parsed, &readIndex))
            {
            case DATAOP_NULL:
                printf("%d: null\n", ip);
                break;
            case DATAOP_STRING:
                value = ByteVectorReadPackUint(parsed, &readIndex);
                printf("%d: string %d:\"%s\"\n", ip, value,
                       StringPoolGetString((stringref)value));
                break;
            case DATAOP_PHI_VARIABLE:
                condition = ByteVectorReadUint(parsed, &readIndex);
                value = ByteVectorReadUint(parsed, &readIndex);
                printf("%d: phi variable condition=%d %d %d\n", ip, condition,
                       value, ByteVectorReadInt(parsed, &readIndex));
                break;
            case DATAOP_PARAMETER:
                name = (stringref)ByteVectorReadPackUint(parsed, &readIndex);
                printf("%d: parameter name=%s\n", ip, StringPoolGetString(name));
                break;
            case DATAOP_RETURN:
                stackframe = ByteVectorReadPackUint(parsed, &readIndex);
                value = ByteVectorReadPackUint(parsed, &readIndex);
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
        controlSize = ByteVectorReadPackUint(parsed, &readIndex);
        printf("control, size=%d\n", controlSize);
        for (stop = readIndex + controlSize; readIndex < stop;)
        {
            ip = readIndex;
            switch (ByteVectorRead(parsed, &readIndex))
            {
            case OP_NOOP:
                printf("%d: noop\n", ip);
                break;
            case OP_RETURN:
                printf("%d: return\n", ip);
                break;
            case OP_BRANCH:
                target = ByteVectorReadUint(parsed, &readIndex);
                condition = ByteVectorReadPackUint(parsed, &readIndex);
                printf("%d: branch condition=%d target=%d\n", ip, condition,
                       target);
                break;
            case OP_JUMP:
                target = ByteVectorReadUint(parsed, &readIndex);
                printf("%d: jump %d\n", ip, target);
                break;
            case OP_INVOKE_NATIVE:
                function = ByteVectorRead(parsed, &readIndex);
                value = ByteVectorReadPackUint(parsed, &readIndex);
                argumentCount = ByteVectorReadPackUint(parsed, &readIndex);
                printf("%d: invoke native function=%d, arguments=%d, stackframe=%d\n",
                       ip, function, argumentCount, value);
                for (i = 0; i < argumentCount; i++)
                {
                    printf("  %d: argument %d\n", i,
                           ByteVectorReadUint(parsed, &readIndex));
                }
                break;
            case OP_COND_INVOKE:
                condition = ByteVectorReadPackUint(parsed, &readIndex);
                value = ByteVectorReadPackUint(parsed, &readIndex);
                function = ByteVectorReadPackUint(parsed, &readIndex);
                argumentCount = ByteVectorReadPackUint(parsed, &readIndex);
                printf("%d: cond_invoke function=%d, condition=%d, arguments=%d, stackframe=%d\n",
                       ip, function, condition, argumentCount, value);
                for (i = 0; i < argumentCount; i++)
                {
                    printf("  %d: argument %d\n", i,
                           ByteVectorReadPackUint(parsed, &readIndex));
                }
                break;
            default:
                assert(false);
                break;
            }
        }
    }
}

void BytecodeGeneratorExecute(bytevector *parsed)
{
    dump(parsed);
}
