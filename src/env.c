#include "config.h"
#include <stdarg.h>
#include <string.h>
#include "common.h"
#include "env.h"
#include "value.h"

static const char **env;
static size_t envCount;

void EnvInit(char **environ)
{
    char **src;
    const char **dst;

    for (src = environ; *src; src++);
    env = (const char**)malloc(((size_t)(src - environ) + 2) * sizeof(char*));

    dst = env;
    *dst++ = "TERM=dumb";
    for (src = environ; *src; src++)
    {
        if (strncmp(*src, "TERM=", 5) && strncmp(*src, "COLORTERM=", 10))
        {
            *dst++ = *src;
        }
    }
    envCount = (size_t)(dst - env);
    *dst = null;
}

void EnvDispose(void)
{
    free(env);
}

static const char **getEnvEntry(const char **envp,
                                const char *name, size_t length)
{
    for (; *envp; envp++)
    {
        if (!strncmp(*envp, name, length) && (*envp)[length] == '=')
        {
            break;
        }
    }
    return envp;
}

void EnvGet(const char *name, size_t length,
            const char **value, size_t *valueLength)
{
    const char **p = getEnvEntry(env, name, length);
    if (*p)
    {
        *value = &(*p)[length + 1];
        *valueLength = *value ? strlen(*value) : 0;
    }
    else
    {
        *value = null;
        *valueLength = 0;
    }
}

const char *const*EnvGetEnv(void)
{
    return env;
}

const char *const*EnvCreateCopy(vref overrides)
{
    vref name;
    vref value;
    size_t nameLength;
    const char **result;
    const char **p;
    char *data;
    char *pname;
    size_t count = envCount + (VCollectionSize(overrides) / 2);
    size_t indexSize;
    size_t dataSize = 0;
    size_t tempSize = 0;
    size_t index;

    for (index = 0; VCollectionGet(overrides, VBoxSize(index++), &name);)
    {
        VCollectionGet(overrides, VBoxSize(index++), &value);
        nameLength = VStringLength(name);
        if (value != VNull)
        {
            dataSize += nameLength + VStringLength(value) + 2;
        }
        else
        {
            tempSize = max(tempSize, nameLength);
        }
    }

    indexSize = (count + 1) * sizeof(*env);
    data = (char*)malloc(indexSize + dataSize + tempSize);
    result = (const char**)data;
    data += indexSize;
    memcpy(result, env, (envCount + 1) * sizeof(*env));
    count = envCount;

    for (index = 0; VCollectionGet(overrides, VBoxSize(index++), &name);)
    {
        VCollectionGet(overrides, VBoxSize(index++), &value);
        nameLength = VStringLength(name);
        VWriteString(name, data);
        p = getEnvEntry(result, data, nameLength);
        if (value != VNull)
        {
            pname = data;
            data += nameLength;
            *data++ = '=';
            data = VWriteString(value, data);
            *data++ = 0;
            if (!*p)
            {
                p[1] = null;
                count++;
            }
            *p = pname;
        }
        else
        {
            *p = result[--count];
            result[count] = null;
        }
    }
    return result;
}
