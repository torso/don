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
    targetref target;
    fileref file;
    uint line;
    uint statementLine;
    uint indent;
    ErrorCode error;

    intvector blockStack;
    inthashmap locals;

    bytevector *bytecode;
} ParseState;

extern nonnull void ParseStateCheck(const ParseState *state);
extern void ParseStateInit(ParseState *restrict state,
                           bytevector *restrict bytecode,
                           targetref target,
                           fileref file, uint line, uint offset);
extern nonnull void ParseStateDispose(ParseState *state);
extern nonnull boolean ParseStateFinish(ParseState *restrict state);
extern nonnull boolean ParseStateFinishBlock(ParseState *restrict state,
                                             uint indent, boolean trailingElse);
extern nonnull uint ParseStateGetJumpTarget(ParseState *state);
extern nonnull boolean ParseStateSetError(ParseState *state, ErrorCode error);

extern nonnull void ParseStateSetIndent(ParseState *state, uint indent);
extern nonnull uint ParseStateBlockIndent(ParseState *state);

extern nonnull boolean ParseStateGetVariable(ParseState *state, stringref name);
extern nonnull boolean ParseStateSetVariable(ParseState *state, stringref name);

extern nonnull boolean ParseStateWriteNullLiteral(ParseState *state);
extern nonnull boolean ParseStateWriteTrueLiteral(ParseState *state);
extern nonnull boolean ParseStateWriteFalseLiteral(ParseState *state);
extern nonnull boolean ParseStateWriteIntegerLiteral(ParseState *state,
                                                     int value);
extern nonnull boolean ParseStateWriteStringLiteral(ParseState *state,
                                                    stringref value);
extern nonnull boolean ParseStateWriteBinaryOperation(ParseState *state,
                                                      Instruction operation);
extern nonnull boolean ParseStateWriteBeginCondition(ParseState *state);
extern nonnull boolean ParseStateWriteSecondConsequent(ParseState *state);
extern nonnull boolean ParseStateWriteFinishCondition(ParseState *state);

extern nonnull boolean ParseStateWriteIf(ParseState *state);
extern nonnull boolean ParseStateWriteWhile(ParseState *state, uint loopTarget);
extern nonnull boolean ParseStateWriteReturn(ParseState *state, uint values);
extern nonnull boolean ParseStateWriteReturnVoid(ParseState *state);
extern nonnull boolean ParseStateWriteInvocation(
    ParseState *state, nativefunctionref nativeFunction,
    targetref target, uint argumentCount, uint returnValues);
