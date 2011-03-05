#include <string.h>
#include <stdio.h>
#include "common.h"
#include "bytevector.h"
#include "heap.h"
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
    printf("%s%s(%s)\n", prefix,
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
}

void WorkExecute(void)
{
    const Work *work;

    assert(ByteVectorSize(&queue));
    work = (const Work*)ByteVectorGetPointer(&queue, 0);
    assert(ByteVectorSize(&queue) >= getWorkSize(work));

    if (DEBUG_WORK)
    {
        printWork("executing: ", work);
    }
    NativeWork(work);
    ByteVectorRemoveRange(&queue, 0, getWorkSize(work));
}
