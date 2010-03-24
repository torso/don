#ifndef BYTEVECTOR_H
#error bytevector.h not included
#endif
#ifndef INTVECTOR_H
#error intvector.h not included
#endif
#ifndef FILEINDEX_H
#error fileindex.h not included
#endif
#ifndef NATIVE_H
#error native.h not included
#endif
#ifndef INSTRUCTION_H
#error instruction.h not included
#endif

#define PARSESTATE_H

struct _Block;
typedef struct _Block Block;
struct _Block
{
    Block *parent;

    /* Holds the then-block of an if statement while parsing the else-block. */
    Block *unfinished;

    uint branchOffset;
    uint condition;
    intvector locals;
    uint indent;
};

struct _Function;
typedef struct _Function Function;
struct _Function
{
    Function *parent;

    Block *currentBlock;
    Block firstBlock;

    bytevector data;
    bytevector control;
    uint valueCount;
    uint parameterCount;

    /* Index of the stackframe value in the caller function.
       Only used for loops. */
    uint stackframe;
};

typedef struct
{
    const byte *start;
    const byte *current;
    fileref file;
    uint line;
    uint statementLine;
    boolean failed;

    Function *currentFunction;
    Function firstFunction;

    uint parsedOffset;
} ParseState;

extern nonnull void ParseStateCheck(const ParseState *state);
extern nonnull void ParseStateInit(ParseState *state, fileref file, uint line,
                                   uint offset);
extern nonnull void ParseStateDispose(ParseState *state);
extern nonnull boolean ParseStateFinishBlock(ParseState *restrict state,
                                             bytevector *restrict parsed,
                                             uint indent, boolean trailingElse);
extern nonnull void ParseStateSetFailed(ParseState *state);

extern nonnull void ParseStateSetIndent(ParseState *state, uint indent);
extern nonnull uint ParseStateBlockIndent(ParseState *state);

extern nonnull uint ParseStateGetVariable(ParseState *state, stringref name);
extern nonnull boolean ParseStateSetVariable(ParseState *state,
                                             stringref name, uint value);

extern nonnull void ParseStateSetArgument(
    ParseState *state, uint argumentOffset, uint parameterIndex, uint value);

extern nonnull uint ParseStateWriteNullLiteral(ParseState *state);
extern nonnull uint ParseStateWriteTrueLiteral(ParseState *state);
extern nonnull uint ParseStateWriteFalseLiteral(ParseState *state);
extern nonnull uint ParseStateWriteIntegerLiteral(ParseState *state, int value);
extern nonnull uint ParseStateWriteStringLiteral(ParseState *state,
                                                 stringref value);
extern nonnull uint ParseStateWriteBinaryOperation(ParseState *state,
                                                   DataInstruction operation,
                                                   uint value1, uint value2);
extern nonnull uint ParseStateWriteTernaryOperation(ParseState *state,
                                                    DataInstruction operation,
                                                    uint value1, uint value2,
                                                    uint value3);

extern nonnull boolean ParseStateWriteIf(ParseState *state, uint value);
extern nonnull boolean ParseStateWriteWhile(ParseState *state, uint value);
extern nonnull boolean ParseStateWriteReturn(ParseState *state);
extern nonnull uint ParseStateWriteNativeInvocation(
    ParseState *state, nativefunctionref nativeFunction, uint parameterCount);
