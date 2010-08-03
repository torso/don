#include <stdio.h>
#include "builder.h"
#include "bytevector.h"
#include "intvector.h"
#include "stringpool.h"
#include "fileindex.h"
#include "targetindex.h"
#include "interpreter.h"
#include "interpreterstate.h"
#include "value.h"
#include "instruction.h"
#include "native.h"

static const boolean DUMP_STATE = false;
static const boolean TRACE = false;


static void dumpState(const RunState *state)
{
    ValueDump(state);
}

static void execute(RunState *state)
{
    uint condition;
    uint offset;
    nativefunctionref nativeFunction;
    targetref target;
    uint stackframeValueOffset;
    uint stackframe;
    uint function;
    uint argumentCount;

    ValueCreateStackframe(state, state->ip, 0);
    if (state->error)
    {
        return;
    }

    /* Remove old (non-existing) stackframe pushed onto the stack by
     * ValueCreateStackframe. */
    IntVectorSetSize(&state->stack, 0);

    for (;;)
    {
        if (TRACE)
        {
            printf("execute ip=%d op=%d bp=%d stacksize=%d\n", state->ip,
                   ByteVectorGet(state->bytecode, state->ip), state->bp,
                   IntVectorSize(&state->stack));
        }
        switch (ByteVectorRead(state->bytecode, &state->ip))
        {
        case OP_RETURN:
            if (!IntVectorSize(&state->stack))
            {
                return;
            }
            ValueDestroyStackframe(state);
            break;

        case OP_BRANCH:
            condition = ByteVectorReadPackUint(state->bytecode, &state->ip);
            offset = ByteVectorReadPackUint(state->bytecode, &state->ip);
            if (!ValueGetBoolean(state, state->bp, condition))
            {
                state->ip += offset;
            }
            break;

        case OP_JUMP:
            offset = ByteVectorReadPackUint(state->bytecode, &state->ip);
            state->ip += offset;
            break;

        case OP_INVOKE_NATIVE:
            nativeFunction = (nativefunctionref)ByteVectorRead(state->bytecode,
                                                               &state->ip);
            stackframeValueOffset = ValueGetOffset(
                state->bp, ByteVectorReadPackUint(state->bytecode, &state->ip));
            stackframe = ValueCreateStackframe(
                state, NativeGetBytecodeOffset(nativeFunction),
                ByteVectorReadPackUint(state->bytecode, &state->ip));
            if (state->error)
            {
                return;
            }
            ValueSetStackframeValue(state, stackframeValueOffset, stackframe);
            NativeInvoke(state, nativeFunction);
            ValueDestroyStackframe(state);
            break;

        case OP_INVOKE_TARGET:
            target = (targetref)ByteVectorReadPackUint(state->bytecode,
                                                       &state->ip);
            stackframeValueOffset = ValueGetOffset(
                state->bp, ByteVectorReadPackUint(state->bytecode, &state->ip));
            stackframe = ValueCreateStackframe(
                state, TargetIndexGetBytecodeOffset(target),
                ByteVectorReadPackUint(state->bytecode, &state->ip));
            if (state->error)
            {
                return;
            }
            ValueSetStackframeValue(state, stackframeValueOffset, stackframe);
            break;

        case OP_COND_INVOKE:
            condition = ByteVectorReadPackUint(state->bytecode, &state->ip);
            function = ByteVectorReadPackUint(state->bytecode, &state->ip);
            if (ValueGetBoolean(state, state->bp, condition))
            {
                stackframeValueOffset = ValueGetOffset(
                    state->bp,
                    ByteVectorReadPackUint(state->bytecode, &state->ip));
                stackframe = ValueCreateStackframe(
                    state, function,
                    ByteVectorReadPackUint(state->bytecode, &state->ip));
                if (state->error)
                {
                    return;
                }
                ValueSetStackframeValue(state, stackframeValueOffset,
                                        stackframe);
            }
            else
            {
                ByteVectorSkipPackUint(state->bytecode, &state->ip);
                argumentCount = ByteVectorReadPackUint(state->bytecode,
                                                       &state->ip);
                while (argumentCount--)
                {
                    ByteVectorSkipPackUint(state->bytecode, &state->ip);
                }
            }
            break;

        default:
            assert(false);
            break;
        }
    }
}

ErrorCode InterpreterExecute(const bytevector *restrict bytecode,
                             const bytevector *restrict valueBytecode,
                             targetref target)
{
    RunState state;

    state.ip = TargetIndexGetBytecodeOffset(target);
    state.bp = 1;
    state.error = NO_ERROR;
    state.bytecode = bytecode;
    state.valueBytecode = valueBytecode;
    IntVectorInit(&state.values);
    IntVectorInit(&state.stack);
    ByteVectorInit(&state.heap);

    execute(&state);
    if (DUMP_STATE)
    {
        dumpState(&state);
    }

    IntVectorDispose(&state.values);
    IntVectorDispose(&state.stack);
    ByteVectorDispose(&state.heap);
    return state.error;
}
