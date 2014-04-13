#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "vm.h"
#include "native.h"
#include "stringpool.h"
#include "work.h"

static const boolean DEBUG_WORK = false;

static bytevector queue;

static void printWork(const char *prefix, const Work *work)
{
    char *buffer;
    char *b;
    size_t length;
    vref *p;
    uint i;

    assert(NativeGetParameterCount(work->function));
    length = 0;
    for (i = NativeGetParameterCount(work->function), p = (vref*)(work+1);
         i--;
         p++)
    {
        if (HeapIsFutureValue(*p))
        {
            length += 8;
        }
        else
        {
            length += VStringLength(*p) + 2;
        }
    }
    buffer = (char*)malloc(length);
    b = buffer;
    for (i = NativeGetParameterCount(work->function), p = (vref*)(work+1);
         i--;
         p++)
    {
        if (HeapIsFutureValue(*p))
        {
            memcpy(b, "future", 6);
            b += 6;
        }
        else
        {
            b = VWriteString(*p, b);
        }
        *b++ = ',';
        *b++ = ' ';
    }
    *(b-2) = 0;
    printf("%s[%p] %s(%s)\n", prefix, (void*)work->vm,
           HeapGetString(NativeGetName(work->function)), buffer);
    free(buffer);
}

static size_t getWorkSize(const Work *work)
{
    return sizeof(*work) +
        (NativeGetParameterCount(work->function) +
         NativeGetReturnValueCount(work->function)) * sizeof(vref);
}

void WorkInit(void)
{
    BVInit(&queue, 1024);
}

void WorkDispose(void)
{
    BVDispose(&queue);
}

void WorkAdd(const Work *work)
{
    if (DEBUG_WORK)
    {
        printWork("added: ", work);
    }
    BVAddData(&queue, (const byte*)work, getWorkSize(work));
    WorkExecute();
}

void WorkDiscard(const VM *vm)
{
    Work *work;
    size_t i = 0;
    size_t size;

    if (DEBUG_WORK)
    {
        printf("remove work for: %p\n", (const void*)vm);
    }
    while (i < BVSize(&queue))
    {
        work = (Work*)BVGetPointer(&queue, i);
        size = getWorkSize(work);
        if (work->vm == vm)
        {
            BVRemoveRange(&queue, i, size);
        }
        else
        {
            i += size;
        }
    }
}

boolean WorkQueueEmpty(void)
{
    return BVSize(&queue) == 0;
}

void WorkExecute(void)
{
    Work *work;
    vref *p1;
    vref *p2;
    uint parameterCount;
    uint i;
    struct
    {
        Work work;
        vref values[NATIVE_MAX_VALUES];
    } env;

    assert(BVSize(&queue));
    work = (Work*)BVGetPointer(&queue, 0);
    assert(BVSize(&queue) >= getWorkSize(work));
    parameterCount = NativeGetParameterCount(work->function);

    work->condition = HeapTryWait(work->condition);
    assert(work->condition == HeapTrue);
    for (i = parameterCount, p1 = (vref*)(work+1); i--; p1++)
    {
        *p1 = HeapTryWait(*p1);
        assert(!HeapIsFutureValue(*p1));
    }
    memcpy(&env, work, getWorkSize(work));

    if (DEBUG_WORK)
    {
        printWork("executing: ", work);
    }
    NativeWork(&env.work);
    for (i = NativeGetReturnValueCount(work->function),
             p1 = (vref*)(work+1) + parameterCount,
             p2 = env.values + parameterCount;
         i--;
         p1++, p2++)
    {
        if (*p1 != *p2)
        {
            HeapSetFutureValue(*p1, *p2);
        }
    }
    BVRemoveRange(&queue, 0, getWorkSize(work));
}
