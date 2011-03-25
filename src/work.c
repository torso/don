#include <string.h>
#include <stdio.h>
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
    objectref *p;
    uint i;

    assert(NativeGetParameterCount(work->function));
    length = 0;
    for (i = NativeGetParameterCount(work->function), p = (objectref*)(work+1);
         i--;
         p++)
    {
        length += HeapStringLength(*p) + 2;
    }
    buffer = (char*)malloc(length);
    b = buffer;
    for (i = NativeGetParameterCount(work->function), p = (objectref*)(work+1);
         i--;
         p++)
    {
        b = HeapWriteString(*p, b);
        *b++ = ',';
        *b++ = ' ';
    }
    *(b-2) = 0;
    printf("%s[%p] %s(%s)\n", prefix, (void*)work->vm,
           StringPoolGetString(NativeGetName(work->function)), buffer);
    free(buffer);
}

static size_t getWorkSize(const Work *work)
{
    return sizeof(*work) +
        (NativeGetParameterCount(work->function) +
         NativeGetReturnValueCount(work->function)) * sizeof(objectref);
}

void WorkInit(void)
{
    ByteVectorInit(&queue, 1024);
}

void WorkDispose(void)
{
    ByteVectorDispose(&queue);
}

void WorkAdd(const Work *work)
{
    if (DEBUG_WORK)
    {
        printWork("added: ", work);
    }
    ByteVectorAddData(&queue, (const byte*)work, getWorkSize(work));
    WorkExecute();
}

void WorkDiscard(const VM *vm)
{
    Work *work;
    size_t i = 0;
    size_t size;

    while (i < ByteVectorSize(&queue))
    {
        work = (Work*)ByteVectorGetPointer(&queue, i);
        size = getWorkSize(work);
        if (work->vm == vm)
        {
            ByteVectorRemoveRange(&queue, i, size);
        }
        else
        {
            i += size;
        }
    }
}

boolean WorkQueueEmpty(void)
{
    return ByteVectorSize(&queue) == 0;
}

void WorkExecute(void)
{
    Work *work;
    objectref *p1;
    objectref *p2;
    uint parameterCount;
    uint i;
    struct
    {
        Work work;
        objectref values[NATIVE_MAX_VALUES];
    } env;

    assert(ByteVectorSize(&queue));
    work = (Work*)ByteVectorGetPointer(&queue, 0);
    assert(ByteVectorSize(&queue) >= getWorkSize(work));
    parameterCount = NativeGetParameterCount(work->function);

    work->condition = HeapTryWait(work->vm, work->condition);
    assert(work->condition == HeapTrue);
    for (i = parameterCount, p1 = (objectref*)(work+1);
         i--;
         p1++)
    {
        *p1 = HeapTryWait(work->vm, *p1);
        assert(!HeapIsFutureValue(*p1));
    }
    memcpy(&env, work, getWorkSize(work));

    if (DEBUG_WORK)
    {
        printWork("executing: ", work);
    }
    NativeWork(&env.work);
    for (i = NativeGetReturnValueCount(work->function),
             p1 = (objectref*)(work+1) + parameterCount,
             p2 = env.values + parameterCount;
         i--;
         p1++, p2++)
    {
        if (*p1 != *p2)
        {
            HeapSetFutureValue(*p1, *p2);
        }
    }
    ByteVectorRemoveRange(&queue, 0, getWorkSize(work));
}
