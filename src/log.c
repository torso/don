#include <stdio.h>
#include "builder.h"
#include "fileindex.h"
#include "log.h"
#include "stringpool.h"

void LogParseError(fileref file, uint line, const char *message)
{
    printf("%s:%d: %s\n", FileIndexGetName(file), line, message);
}
