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
    fileref file;
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
                           functionref function,
                           fileref file, uint line, uint offset);
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

extern nonnull boolean ParseStateGetVariable(ParseState *state, stringref name);
extern nonnull boolean ParseStateSetVariable(ParseState *state, stringref name);
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
extern nonnull void ParseStateWriteNullLiteral(ParseState *state);
extern nonnull void ParseStateWriteTrueLiteral(ParseState *state);
extern nonnull void ParseStateWriteFalseLiteral(ParseState *state);
extern nonnull void ParseStateWriteIntegerLiteral(ParseState *state,
                                                  int value);
extern nonnull void ParseStateWriteStringLiteral(ParseState *state,
                                                 stringref value);
extern nonnull void ParseStateWriteList(ParseState *state, uint size);
extern nonnull void ParseStateWriteFile(ParseState *state,
                                        stringref filename);
extern nonnull void ParseStateWriteFileset(ParseState *state,
                                           stringref pattern);
extern nonnull void ParseStateWriteBeginCondition(ParseState *state);
extern nonnull boolean ParseStateWriteSecondConsequent(ParseState *state);
extern nonnull boolean ParseStateWriteFinishCondition(ParseState *state);

extern nonnull void ParseStateWriteIf(ParseState *state);
extern nonnull void ParseStateWriteWhile(ParseState *state,
                                         size_t loopTarget);
extern nonnull void ParseStateWriteReturn(ParseState *state, uint values);
extern nonnull void ParseStateWriteReturnVoid(ParseState *state);
extern nonnull void ParseStateWriteInvocation(
    ParseState *state, nativefunctionref nativeFunction,
    functionref function, uint argumentCount, uint returnValues);
extern nonnull void ParseStateReorderStack(
    ParseState *state, intvector *order, uint offset, uint count);
