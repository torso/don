#include "config.h"
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "bytevector.h"
#include "debug.h"
#include "heap.h"
#include "native.h"
#include "job.h"
#include "vm.h"

static void printJob(const char *prefix, const Job *job)
{
    bytevector buffer;
    vref *p;
    uint i;

    assert(job->argumentCount);
    BVInit(&buffer, job->argumentCount * 16);
    for (i = job->argumentCount, p = (vref*)(job+1); i--; p++)
    {
        char *value = HeapDebug(*p);
        BVAddData(&buffer, (const byte*)value, strlen(value));
        BVAddData(&buffer, (const byte*)", ", 2);
        free(value);
    }
    BVPop(&buffer);
    BVPop(&buffer);
    BVAdd(&buffer, 0);
    printf("%s%p[vm:%p] (%s)\n", prefix, (void*)job, (void*)job->vm,
           BVGetPointer(&buffer, 0));
    BVDispose(&buffer);
}

Job *JobAdd(JobFunction function, VM *vm, const vref *arguments, uint argumentCount,
              vref accessedFiles, vref modifiedFiles)
{
    Job *job = vm->job ? vm->job : (Job*)malloc(sizeof(Job) + argumentCount * sizeof(vref));
    assert(!vm->job || job->argumentCount == argumentCount);
    job->function = function;
    job->vm = vm;
    job->accessedFiles = accessedFiles;
    job->modifiedFiles = modifiedFiles;
    job->argumentCount = argumentCount;
    memcpy(job + 1, arguments, argumentCount * sizeof(vref));
    if (DEBUG_JOB)
    {
        if (vm->job)
        {
            printJob("update job: ", job);
        }
        else
        {
            printJob("add job: ", job);
        }
    }
    return job;
}

void JobDiscard(Job *job)
{
    if (DEBUG_JOB)
    {
        printJob("remove job: ", job);
    }
    free(job);
}

void JobExecute(Job *job)
{
    vref value;

    if (DEBUG_JOB)
    {
        printJob("execute job: ", job);
    }

    assert(job->vm->job == job);
    value = job->function(job, (vref*)(job + 1));
    if (value)
    {
        VMStoreValue(job->vm, job->storeAt, value);
        job->vm->idle = false;
        job->vm->job = null;
        free(job);
    }
    else if (job->vm->failMessage)
    {
        job->vm->job = null;
        free(job);
    }
    else
    {
        /* TODO: This will not happen until job is done in parallel. */
        unreachable;
    }
}
