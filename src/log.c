#include <stdio.h>
#include "builder.h"
#include "stringpool.h"
#include "fileindex.h"
#include "log.h"

void LogParseError(fileref file, uint line, const char *message)
{
    printf("%s:%d: %s\n",
           StringPoolGetString(FileIndexGetName(file)), line, message);
}
