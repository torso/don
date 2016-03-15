struct _ParsedProgram;

typedef struct _LinkedProgram
{
    int *bytecode;
    int *lineNumbers;
    int *functions;
    uint size;
    vref *constants;
    int constantCount;
    vref *fields;
    int fieldCount;
} LinkedProgram;

nonnull bool Link(struct _ParsedProgram *parsed, LinkedProgram *linked);
