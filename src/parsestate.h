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
    functionref function;
    fileref file;
    uint line;
    uint statementLine;
    uint indent;
    ErrorCode error;

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
extern nonnull boolean ParseStateBeginForwardJump(ParseState *state,
                                                  Instruction instruction,
                                                  size_t *branch);
extern nonnull boolean ParseStateFinishJump(ParseState *state, size_t branch);
extern nonnull boolean ParseStateSetError(ParseState *state, ErrorCode error);

extern nonnull void ParseStateSetIndent(ParseState *state, uint indent);
extern nonnull uint ParseStateBlockIndent(ParseState *state);

extern nonnull boolean ParseStateGetVariable(ParseState *state, stringref name);
extern nonnull boolean ParseStateSetVariable(ParseState *state, stringref name);
extern nonnull uint16 ParseStateCreateUnnamedVariable(ParseState *state);
extern nonnull boolean ParseStateGetUnnamedVariable(ParseState *state,
                                                    uint16 variable);
extern nonnull boolean ParseStateSetUnnamedVariable(ParseState *state,
                                                    uint16 variable);

extern nonnull boolean ParseStateGetField(ParseState *state, fieldref field);
extern nonnull boolean ParseStateSetField(ParseState *state, fieldref field);

extern nonnull boolean ParseStateWriteInstruction(ParseState *state,
                                                  Instruction instruction);
extern nonnull boolean ParseStateWriteNullLiteral(ParseState *state);
extern nonnull boolean ParseStateWriteTrueLiteral(ParseState *state);
extern nonnull boolean ParseStateWriteFalseLiteral(ParseState *state);
extern nonnull boolean ParseStateWriteIntegerLiteral(ParseState *state,
                                                     int value);
extern nonnull boolean ParseStateWriteStringLiteral(ParseState *state,
                                                    stringref value);
extern nonnull boolean ParseStateWriteList(ParseState *state, uint size);
extern nonnull boolean ParseStateWriteFile(ParseState *state,
                                           stringref filename);
extern nonnull boolean ParseStateWriteFileset(ParseState *state,
                                              stringref pattern);
extern nonnull boolean ParseStateWriteBeginCondition(ParseState *state);
extern nonnull boolean ParseStateWriteSecondConsequent(ParseState *state);
extern nonnull boolean ParseStateWriteFinishCondition(ParseState *state);

extern nonnull boolean ParseStateWriteIf(ParseState *state);
extern nonnull boolean ParseStateWriteWhile(ParseState *state,
                                            size_t loopTarget);
extern nonnull boolean ParseStateWritePipe(ParseState *state,
                                           stringref out, stringref err);
extern nonnull boolean ParseStateWriteReturn(ParseState *state, uint values);
extern nonnull boolean ParseStateWriteReturnVoid(ParseState *state);
extern nonnull boolean ParseStateWriteInvocation(
    ParseState *state, nativefunctionref nativeFunction,
    functionref function, uint argumentCount, uint returnValues);
