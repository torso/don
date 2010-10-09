#define _POSIX_SOURCE
#include <memory.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "common.h"
#include "vm.h"
#include "fileindex.h"
#include "interpreter.h"
#include "native.h"
#include "stringpool.h"

#define TOTAL_PARAMETER_COUNT 5

typedef enum
{
    NATIVE_NULL,
    NATIVE_ECHO,
    NATIVE_EXEC,
    NATIVE_FAIL,
    NATIVE_FILENAME,
    NATIVE_SIZE,

    NATIVE_FUNCTION_COUNT
} NativeFunction;

typedef struct
{
    stringref name;
    uint parameterCount;
    uint minimumArgumentCount;
    stringref parameterNames[1];
} FunctionInfo;

static byte functionInfo[
    (sizeof(FunctionInfo) -
     sizeof(stringref)) * NATIVE_FUNCTION_COUNT +
    sizeof(stringref) * TOTAL_PARAMETER_COUNT];
static uint functionIndex[NATIVE_FUNCTION_COUNT];

static byte *initFunctionInfo = functionInfo;
static uint initFunctionIndex = 1;

static void addFunctionInfo(const char *name,
                            uint parameterCount, uint minimumArgumentCount,
                            const char **parameterNames)
{
    FunctionInfo *info = (FunctionInfo*)initFunctionInfo;
    stringref *pnames;
    if (!info)
    {
        return;
    }
    functionIndex[initFunctionIndex++] = (uint)(initFunctionInfo - functionInfo);
    pnames = info->parameterNames;
    initFunctionInfo += sizeof(FunctionInfo) - sizeof(stringref) +
        sizeof(stringref) * parameterCount;
    assert(initFunctionInfo <= &functionInfo[sizeof(functionInfo)]);

    info->name = StringPoolAdd(name);
    info->parameterCount = parameterCount;
    info->minimumArgumentCount = minimumArgumentCount;
    if (!info->name)
    {
        initFunctionInfo = null;
        return;
    }
    while (parameterCount--)
    {
        *pnames = StringPoolAdd(*parameterNames);
        if (!*pnames)
        {
            initFunctionInfo = null;
            return;
        }
        pnames++;
        parameterNames++;
    }
}

static pure const FunctionInfo *getFunctionInfo(nativefunctionref function)
{
    assert(function);
    assert(uintFromRef(function) < NATIVE_FUNCTION_COUNT);
    return (FunctionInfo*)&functionInfo[functionIndex[sizeFromRef(function)]];
}

static char **createStringArray(VM *vm, objectref collection)
{
    Iterator iter;
    objectref value;
    size_t size = sizeof(char*);
    uint count = 1;
    char **strings;
    char **table;
    byte *stringData;

    assert(HeapIsCollection(vm, collection));
    assert(HeapCollectionSize(vm, collection));

    HeapCollectionIteratorInit(vm, &iter, collection, true);
    while (HeapIteratorNext(&iter, &value))
    {
        size += InterpreterGetStringSize(vm, value) + 1 +
            sizeof(char*);
        count++;
    }

    strings = (char**)malloc(size);
    if (!strings)
    {
        return null;
    }

    table = strings;
    stringData = (byte*)&strings[count];
    HeapCollectionIteratorInit(vm, &iter, collection, true);
    while (HeapIteratorNext(&iter, &value))
    {
        *table++ = (char*)stringData;
        stringData = InterpreterCopyString(vm, value, stringData);
        *stringData++ = 0;
    }
    *table = null;
    return strings;
}

ErrorCode NativeInit(void)
{
    static const char *echoParameters[] = {"message"};
    static const char *execParameters[] = {"command"};
    static const char *failParameters[] = {"message"};
    static const char *filenameParameters[] = {"path"};
    static const char *sizeParameters[] = {"collection"};
    addFunctionInfo("echo", 1, 1, echoParameters);
    addFunctionInfo("exec", 1, 1, execParameters);
    addFunctionInfo("fail", 1, 1, failParameters);
    addFunctionInfo("filename", 1, 1, filenameParameters);
    addFunctionInfo("size", 1, 1, sizeParameters);
    return initFunctionInfo ? NO_ERROR : OUT_OF_MEMORY;
}

ErrorCode NativeInvoke(VM *vm, nativefunctionref function, uint returnValues)
{
    objectref value;
    size_t size;
    const char *buffer;
    pid_t pid;
    int status;
    char **argv;
    bytevector *out;
    bytevector *err;
    int pipeOut[2];
    int pipeErr[2];
    ssize_t ssize;
    fileref file;
    const char *filename;
    ErrorCode error;

    switch ((NativeFunction)function)
    {
    case NATIVE_ECHO:
        assert(!returnValues);
        value = InterpreterPop(vm);
        size = InterpreterGetStringSize(vm, value);
        buffer = InterpreterGetString(vm, value);
        out = InterpreterGetPipeOut(vm);
        error = NO_ERROR;
        if (out)
        {
            error = ByteVectorAddData(out, (byte*)buffer, size);
            if (!error)
            {
                error = ByteVectorAdd(out, '\n');
            }
        }
        else if (size)
        {
            if (buffer[size - 1] == '\n')
            {
                printf("%s", buffer);
            }
            else
            {
                printf("%s\n", buffer);
            }
        }
        else
        {
            printf("\n");
        }
        InterpreterFreeStringBuffer(vm, buffer);
        return error;

    case NATIVE_EXEC:
        assert(returnValues <= 1);
        value = InterpreterPop(vm);
        argv = createStringArray(vm, value);
        if (!argv)
        {
            return OUT_OF_MEMORY;
        }

        out = InterpreterGetPipeOut(vm);
        if (out)
        {
            status = pipe(pipeOut);
            if (status < 0)
            {
                /* TODO: Error handling. */
                return OUT_OF_MEMORY;
            }
        }
        err = InterpreterGetPipeErr(vm);
        if (err)
        {
            status = pipe(pipeErr);
            if (status < 0)
            {
                /* TODO: Error handling. */
                return OUT_OF_MEMORY;
            }
        }

        pid = fork();
        if (!pid)
        {
            if (out)
            {
                status = dup2(pipeOut[1], STDOUT_FILENO);
                if (status < 0)
                {
                    /* TODO: Error handling. */
                    return false;
                }
                close(pipeOut[0]);
                close(pipeOut[1]);
            }
            if (err)
            {
                status = dup2(pipeErr[1], STDERR_FILENO);
                if (status < 0)
                {
                    /* TODO: Error handling. */
                    return false;
                }
                close(pipeErr[0]);
                close(pipeErr[1]);
            }

            execvp(argv[0], argv);
            _exit(EXIT_FAILURE);
        }
        free(argv);
        if (pid < 0)
        {
            /* TODO: Error handling. */
            return OUT_OF_MEMORY;
        }
        if (out)
        {
            status = fcntl(pipeOut[0], F_GETFL, 0);
            fcntl(pipeOut[0], F_SETFL, status | O_NONBLOCK);
            close(pipeOut[1]);
        }
        if (err)
        {
            status = fcntl(pipeErr[0], F_GETFL, 0);
            fcntl(pipeErr[0], F_SETFL, status | O_NONBLOCK);
            close(pipeErr[1]);
        }

        /* TODO: Sleep when no data is available. */
        while (out || err)
        {
            if (out)
            {
                ssize = read(pipeOut[0], ByteVectorGetAppendPointer(out),
                             50);
                if (ssize)
                {
                    if (ssize > 0)
                    {
                        ByteVectorGrow(out, (size_t)ssize);
                    }
                    else if (errno != EWOULDBLOCK)
                    {
                        /* TODO: Error handling */
                        return OUT_OF_MEMORY;
                    }
                }
                else
                {
                    close(pipeOut[0]);
                    out = null;
                }
            }
            if (err)
            {
                ssize = read(pipeErr[0], ByteVectorGetAppendPointer(err),
                             ByteVectorGetAvailableSize(err));
                if (ssize)
                {
                    if (ssize > 0)
                    {
                        ByteVectorGrow(err, (size_t)ssize);
                    }
                    else if (errno != EWOULDBLOCK)
                    {
                        /* TODO: Error handling */
                        return OUT_OF_MEMORY;
                    }
                }
                else
                {
                    close(pipeErr[0]);
                    err = null;
                }
            }
        }

        pid = waitpid(pid, &status, 0);
        if (pid < 0)
        {
            /* TODO: Error handling. */
            return OUT_OF_MEMORY;
        }
        if (returnValues)
        {
            InterpreterPush(vm, HeapBoxInteger(vm, status));
        }
        return NO_ERROR;

    case NATIVE_FAIL:
        assert(!returnValues);
        value = InterpreterPop(vm);
        if (!value)
        {
            size = 0;
        }
        else
        {
            size = InterpreterGetStringSize(vm, value);
            if (size)
            {
                buffer = InterpreterGetString(vm, value);
                if (buffer[size - 1] == '\n')
                {
                    printf("BUILD FAILED: %s", buffer);
                }
                else
                {
                    printf("BUILD FAILED: %s\n", buffer);
                }
                InterpreterFreeStringBuffer(vm, buffer);
            }
        }
        if (!size)
        {
            printf("BUILD FAILED\n");
        }
        return ERROR_FAIL;

    case NATIVE_FILENAME:
        assert(returnValues <= 1);
        value = InterpreterPop(vm);
        assert(HeapGetObjectType(vm, value) == TYPE_FILE);
        if (returnValues)
        {
            file = HeapGetFile(vm, value);
            size = strlen(FileIndexGetName(file));
            filename = FileIndexFilename(FileIndexGetName(file), &size);
            if (!filename)
            {
                return OUT_OF_MEMORY;
            }
            value = HeapCreateString(vm, filename, size);
            if (!value)
            {
                return OUT_OF_MEMORY;
            }
            InterpreterPush(vm, value);
        }
        return NO_ERROR;

    case NATIVE_SIZE:
        value = InterpreterPop(vm);
        if (returnValues)
        {
            assert(returnValues == 1);
            assert(HeapCollectionSize(vm, value) <= INT_MAX);
            InterpreterPush(vm, HeapBoxSize(vm, HeapCollectionSize(vm, value)));
        }
        return NO_ERROR;

    case NATIVE_NULL:
    case NATIVE_FUNCTION_COUNT:
        break;
    }
    assert(false);
    return NO_ERROR;
}

nativefunctionref NativeFindFunction(stringref name)
{
    uint i;
    for (i = 1; i < NATIVE_FUNCTION_COUNT; i++)
    {
        if (NativeGetName(refFromUint(i)) == name)
        {
            return refFromUint(i);
        }
    }
    return 0;
}

stringref NativeGetName(nativefunctionref function)
{
    return getFunctionInfo(function)->name;
}

uint NativeGetParameterCount(nativefunctionref function)
{
    return getFunctionInfo(function)->parameterCount;
}

uint NativeGetMinimumArgumentCount(nativefunctionref function)
{
    return getFunctionInfo(function)->minimumArgumentCount;
}

const stringref *NativeGetParameterNames(nativefunctionref function)
{
    return getFunctionInfo(function)->parameterNames;
}
