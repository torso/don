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

extern void ParserAddKeywords(void);
extern nonnull void ParseInit(ParsedProgram *program);
extern void ParseDispose(void);
extern nonnull void ParseFile(ParsedProgram *program, vref filename, namespaceref ns);
