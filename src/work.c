#include "common.h"
#include <stdio.h>
#include <string.h>
#include "bytevector.h"
#include "debug.h"
#include "heap.h"
#include "native.h"
#include "work.h"
#include "vm.h"

static bytevector queue;

static void printWork(const char *prefix, const Work *work)
{
    bytevector buffer;
    char *condition = HeapDebug(work->condition);
    vref *p;
    uint i;

    assert(work->argumentCount);
    BVInit(&buffer, work->argumentCount * 16);
    for (i = work->argumentCount, p = (vref*)(work+1); i--; p++)
    {
        char *value = HeapDebug(*p);
        BVAddData(&buffer, (const byte*)value, strlen(value));
        BVAddData(&buffer, (const byte*)", ", 2);
        free(value);
    }
    BVPop(&buffer);
    BVPop(&buffer);
    BVAdd(&buffer, 0);
    printf("%s[%p] (%s) condition:%s\n", prefix, (void*)work->vm,
           BVGetPointer(&buffer, 0), condition);
    BVDispose(&buffer);
    free(condition);
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
    assert(vm->condition);
    work->function = function;
    work->vm = vm;
    work->ip = vm->ip;
    work->condition = vm->condition;
    work->accessedFiles = VEmptyList;
    work->modifiedFiles = VEmptyList;
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

bool WorkExecute(void)
{
    size_t offset = 0;
    assert(BVSize(&queue));
    do
    {
        Work *work = (Work*)BVGetPointer(&queue, offset);
        size_t size = getWorkSize(work);
        VBool b;
        assert(BVSize(&queue) - offset >= size);

        b = VGetBool(work->condition);
        if (b == FALSY)
        {
            if (DEBUG_WORK)
            {
                printWork("never executing: ", work);
            }
            BVRemoveRange(&queue, offset, size);
            continue;
        }

        if (b == TRUTHY)
        {
            if (DEBUG_WORK)
            {
                printWork("executing: ", work);
            }

            if (work->function(work, (vref*)(work + 1)))
            {
                BVRemoveRange(&queue, offset, size);
                return true;
            }
            else
            {
                /* TODO: This will not happen until work is done in parallel. */
                unreachable;
            }
        }

        if (DEBUG_WORK)
        {
            printWork("not executing: ", work);
        }
        offset += size;
    }
    while (offset < BVSize(&queue));
    return false;
}
