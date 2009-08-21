#include <stdio.h>
#include "builder.h"
#include "stringpool.h"
#include "fileindex.h"

int main(int argc, const char** argv)
{
    int i;
    const char* options;
    const char* inputFile = null;
    boolean parseOptions = true;

    for (i = 1; i < argc; i++)
    {
        if (parseOptions && argv[i][0] == '-')
        {
            options = argv[i] + 1;
            if (!*options)
            {
                printf("Invalid argument: \"-\"\n");
                return 1;
            }
            if (*options == '-')
            {
                if (*++options)
                {
                    printf("TODO: Long option\n");
                    return 1;
                }
                else
                {
                    parseOptions = false;
                }
            }
            for (; *options; options++)
            {
                switch (*options)
                {
                case 'i':
                    if (inputFile)
                    {
                        printf("Input file already specified\n");
                        return 1;
                    }
                    if (++i >= argc)
                    {
                        printf("Option \"-i\" requires an argument\n");
                        return 1;
                    }
                    inputFile = argv[i];
                    break;

                default:
                    printf("Unknown option: %c\n", argv[i][1]);
                    return 1;
                }
            }
        }
        else
        {
            printf("TODO: target=%s\n", argv[i]);
            return 1;
        }
    }
    if (!inputFile)
    {
        inputFile = "build.don";
    }
    printf("input=%s\n", inputFile);

    StringPoolInit();
    FileIndexInit();
    FileIndexAdd(inputFile);
    return 0;
}
