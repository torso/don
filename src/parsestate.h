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

#define PARSESTATE_H

struct _Block;
typedef struct _Block Block;
struct _Block
{
    Block *parent;

    /* Holds the then-block of an if statement while parsing the else-block. */
    Block *unfinished;

    uint indent;
    uint loopBegin;
    uint conditionOffset;
    uint condition;
    boolean loop;
    boolean allowTrailingElse;
    intvector locals;
};

typedef struct
{
    const byte *start;
    const byte *current;
    fileref file;
    uint line;
    uint statementLine;
    uint loopLevel;
    boolean allowElse;

    bytevector data;
    bytevector control;

    Block *currentBlock;
    Block firstBlock;
} ParseState;

extern nonnull void ParseStateCheck(const ParseState *state);
extern nonnull void ParseStateInit(ParseState *state, fileref file, uint line,
                                   uint offset);
extern nonnull void ParseStateDispose(ParseState *state);

extern nonnull boolean ParseStateBlockBegin(ParseState *state, uint indent,
                                            boolean loop,
                                            boolean allowTrailingElse);
extern nonnull boolean ParseStateBlockEnd(ParseState *state, boolean isElse);
extern nonnull boolean ParseStateBlockEmpty(ParseState *state);
extern nonnull uint ParseStateBlockIndent(ParseState *state);

extern nonnull int ParseStateGetVariable(ParseState *state,
                                         stringref identifier);
extern nonnull boolean ParseStateSetVariable(ParseState *state,
                                             stringref identifier, int value);

extern nonnull uint ParseStateWriteArguments(ParseState *state, uint size);
extern nonnull void ParseStateSetArgument(ParseState *state, uint offset,
                                          int value);

extern nonnull int ParseStateWriteStringLiteral(ParseState *state,
                                                stringref value);

extern nonnull boolean ParseStateWriteIf(ParseState *state, uint value);
extern nonnull boolean ParseStateWriteWhile(ParseState *state, uint value);
extern nonnull boolean ParseStateWriteReturn(ParseState *state);
extern nonnull boolean ParseStateWriteNativeInvocation(
    ParseState *state, nativefunctionref nativeFunction, uint argumentOffset);
