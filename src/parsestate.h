#ifndef INTHASHMAP_H
#error inthashmap.h not included
#endif
#ifndef INTVECTOR_H
#error intvector.h not included
#endif

typedef struct
{
    const byte *start;
    const byte *current;
    const byte *limit;
    namespaceref ns;
    functionref function;
    File fh;
    vref filename;
    uint line;
    uint statementLine;
    uint indent;

    intvector blockStack;
    inthashmap locals;
    uint unnamedVariables;

    bytevector *bytecode;
} ParseState;

extern nonnull void ParseStateCheck(const ParseState *state);
extern void ParseStateInit(ParseState *restrict state,
                           bytevector *restrict bytecode,
                           namespaceref ns, functionref function,
                           vref filename, uint line, uint offset);
extern nonnull void ParseStateDispose(ParseState *state);
extern nonnull boolean ParseStateFinish(ParseState *restrict state);
extern nonnull boolean ParseStateFinishBlock(ParseState *restrict state,
                                             uint indent, boolean trailingElse);
extern nonnull size_t ParseStateGetJumpTarget(ParseState *state);
extern nonnull void ParseStateBeginForwardJump(ParseState *state,
                                               Instruction instruction,
                                               size_t *branch);
extern nonnull void ParseStateFinishJump(ParseState *state, size_t branch);

extern nonnull void ParseStateSetIndent(ParseState *state, uint indent);
extern nonnull uint ParseStateBlockIndent(ParseState *state);

extern nonnull boolean ParseStateIsParameter(ParseState *state, vref name);
extern nonnull int ParseStateGetVariableIndex(ParseState *state, vref name);
extern nonnull boolean ParseStateGetVariable(ParseState *state, vref name);
extern nonnull boolean ParseStateSetVariable(ParseState *state, vref name);
extern nonnull boolean ParseStateCreateUnnamedVariable(ParseState *state,
                                                       uint16 *result);
extern nonnull void ParseStateGetUnnamedVariable(ParseState *state,
                                                 uint16 variable);
extern nonnull void ParseStateSetUnnamedVariable(ParseState *state,
                                                 uint16 variable);

extern nonnull void ParseStateGetField(ParseState *state, fieldref field);
extern nonnull void ParseStateSetField(ParseState *state, fieldref field);

extern nonnull void ParseStateWriteInstruction(ParseState *state,
                                               Instruction instruction);
extern nonnull void ParseStateWritePush(ParseState *state, vref value);
extern nonnull void ParseStateReorderStack(ParseState *state,
                                           const uint16 *reorder, uint16 count);
extern nonnull void ParseStateWriteList(ParseState *state, uint size);
extern nonnull void ParseStateWriteFilelist(ParseState *state, vref pattern);
extern nonnull void ParseStateWriteBeginCondition(ParseState *state);
extern nonnull boolean ParseStateWriteSecondConsequent(ParseState *state);
extern nonnull boolean ParseStateWriteFinishCondition(ParseState *state);

extern nonnull void ParseStateWriteIf(ParseState *state);
extern nonnull void ParseStateWriteWhile(ParseState *state,
                                         size_t loopTarget);
extern nonnull void ParseStateWriteReturn(ParseState *state, uint values);
extern nonnull void ParseStateWriteInvocation(ParseState *state,
                                              functionref function,
                                              uint returnValues);
extern nonnull void ParseStateWriteNativeInvocation(ParseState *state,
                                                    nativefunctionref function);
