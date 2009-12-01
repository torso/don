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

typedef struct
{
    const byte* start;
    const byte* current;
    fileref file;
    uint line;
    uint statementLine;

    intvector blocks;
    intvector locals;
    bytevector data;
    bytevector control;
} ParseState;

extern nonnull pure void ParseStateCheck(const ParseState* state);
extern nonnull void ParseStateInit(ParseState* state, fileref file, uint line,
                                   uint offset);
extern nonnull void ParseStateDispose(ParseState* state);

extern nonnull boolean ParseStateBlockBegin(ParseState* state, int indent, boolean loop);
extern nonnull void ParseStateBlockEnd(ParseState* state);
extern nonnull pure boolean ParseStateBlockEmpty(ParseState* state);
extern nonnull int ParseStateBlockIndent(ParseState* state);

extern nonnull uint ParseStateWriteArguments(ParseState* state, uint size);
extern nonnull void ParseStateSetArgument(ParseState* state, uint offset,
                                          int value);

extern nonnull int ParseStateWriteStringLiteral(ParseState* state,
                                                stringref value);

extern nonnull boolean ParseStateWriteWhile(ParseState* state, int value);
extern nonnull boolean ParseStateWriteReturn(ParseState* state);
extern nonnull boolean ParseStateWriteNativeInvocation(
    ParseState* state, nativefunctionref nativeFunction, uint argumentOffset);
