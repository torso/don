#include "common.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "vm.h"
#include "native.h"
#include "stringpool.h"
#include "work.h"

static const bool DEBUG_WORK = false;

static bytevector queue;

static void printWork(const char *prefix, const Work *work)
{
    char *buffer;
    char *b;
    size_t length;
    vref *p;
    uint i;

    assert(work->argumentCount);
    length = 0;
    for (i = work->argumentCount, p = (vref*)(work+1);
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
    for (i = work->argumentCount, p = (vref*)(work+1);
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
    printf("%s[%p] (%s)\n", prefix, (void*)work->vm, buffer);
    free(buffer);
}

static size_t getWorkSize(const Work *work)
{
    return sizeof(*work) + work->argumentCount * sizeof(vref);
}

void WorkInit(void)
{
    BVInit(&queue, 1024);
}

void WorkDispose(void)
{
    BVDispose(&queue);
}

Work *WorkAdd(WorkFunction function, VM *vm, uint argumentCount, vref **arguments)
{
    Work *work = (Work*)BVGetAppendPointer(
        &queue, sizeof(Work) + argumentCount * sizeof(**arguments));
    work->function = function;
    work->vm = vm;
    work->ip = vm->ip;
    work->condition = vm->condition;
    work->accessedFiles = HeapEmptyList;
    work->modifiedFiles = HeapEmptyList;
    work->argumentCount = argumentCount;
    *arguments = (vref*)(work + 1);
    return work;
}

void WorkCommit(Work *work)
{
    if (DEBUG_WORK)
    {
        printWork("added: ", work);
    }
}

void WorkAbort(Work *work)
{
    assert(BVGetPointer(&queue, BVSize(&queue)) == (byte*)work + getWorkSize(work));
    BVSetSize(&queue, (size_t)((const byte*)work - BVGetPointer(&queue, 0)));
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

bool WorkQueueEmpty(void)
{
    return BVSize(&queue) == 0;
}

void WorkExecute(void)
{
    Work *work;
    VBool b;

    assert(BVSize(&queue));
    work = (Work*)BVGetPointer(&queue, 0);
    assert(BVSize(&queue) >= getWorkSize(work));

    b = VGetBool(work->condition);
    if (b == FALSY)
    {
        if (DEBUG_WORK)
        {
            printWork("not executing: ", work);
        }
        BVRemoveRange(&queue, 0, getWorkSize(work));
        return;
    }
    assert(b == TRUTHY);

    if (DEBUG_WORK)
    {
        printWork("executing: ", work);
    }
    if (work->function(work, (vref*)(work + 1)))
    {
        BVRemoveRange(&queue, 0, getWorkSize(work));
    }
    else
    {
        /* TODO: This will not happen until work is done in parallel. */
        unreachable;
    }
}
