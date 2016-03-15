typedef struct _ParsedProgram
{
    intvector bytecode;
    intvector functions;
    intvector constants;
    intvector fields;
    uint invocationCount;
    uint maxJumpCount;
    uint maxJumpTargetCount;
} ParsedProgram;

void ParserAddKeywords(void);
nonnull void ParseInit(ParsedProgram *program);
void ParseDispose(void);
nonnull void ParseFile(ParsedProgram *program, const char *filename,
                       size_t filenameLength, namespaceref ns);
