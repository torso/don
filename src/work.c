#include "config.h"
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "bytevector.h"
#include "debug.h"
#include "heap.h"
#include "native.h"
#include "work.h"
#include "vm.h"

static void printWork(const char *prefix, const Work *work)
{
    bytevector buffer;
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
    printf("%s%p[vm:%p] (%s)\n", prefix, (void*)work, (void*)work->vm,
           BVGetPointer(&buffer, 0));
    BVDispose(&buffer);
}

Work *WorkAdd(WorkFunction function, VM *vm, const vref *arguments, uint argumentCount,
              vref accessedFiles, vref modifiedFiles)
{
    Work *work = vm->work ? vm->work : (Work*)malloc(sizeof(Work) + argumentCount * sizeof(vref));
    assert(!vm->work || work->argumentCount == argumentCount);
    work->function = function;
    work->vm = vm;
    work->accessedFiles = accessedFiles;
    work->modifiedFiles = modifiedFiles;
    work->argumentCount = argumentCount;
    memcpy(work + 1, arguments, argumentCount * sizeof(vref));
    if (DEBUG_WORK)
    {
        if (vm->work)
        {
            printWork("update work: ", work);
        }
        else
        {
            printWork("add work: ", work);
        }
    }
    return work;
}

void WorkDiscard(Work *work)
{
    if (DEBUG_WORK)
    {
        printWork("remove work: ", work);
    }
    free(work);
}

void WorkExecute(Work *work)
{
    vref value;

    if (DEBUG_WORK)
    {
        printWork("execute work: ", work);
    }

    assert(work->vm->work == work);
    value = work->function(work, (vref*)(work + 1));
    if (value)
    {
        VMStoreValue(work->vm, work->storeAt, value);
        work->vm->idle = false;
        work->vm->work = null;
        free(work);
    }
    else if (work->vm->failMessage)
    {
        work->vm->work = null;
        free(work);
    }
    else
    {
        /* TODO: This will not happen until work is done in parallel. */
        unreachable;
    }
}
