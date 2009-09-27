#include <stdio.h>
#include "builder.h"
#include "stringpool.h"
#include "fileindex.h"
#include "log.h"
#include "parser.h"

typedef struct
{
    const byte* start;
    const byte* current;
    fileref file;
    uint line;
} ParseState;

static char errorBuffer[256];

static boolean isInitialIdentifierCharacter(char c)
{
    return (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z');
}

static boolean isIdentifierCharacter(char c)
{
    return (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9');
}

static void error(const char* message)
{
    LogParseError(message);
}

static void checkState(const ParseState* state)
{
    assert(state->start != null);
    assert(state->current >= state->start);
    assert(state->current <= state->start + FileIndexGetSize(state->file));
}

static boolean eof(const ParseState* state)
{
    checkState(state);
    return state->current == state->start + FileIndexGetSize(state->file);
}

static void skipEndOfLine(ParseState* state)
{
    checkState(state);
    while (!eof(state) && *state->current++ != '\n');
    state->line++;
}

static boolean readNewline(ParseState* state)
{
    checkState(state);
    if (state->current[0] == '\n')
    {
        state->current++;
        state->line++;
        return true;
    }
    return false;
}

static boolean peekIndent(const ParseState* state)
{
    checkState(state);
    return state->current[0] == ' ';
}

static boolean peekComment(const ParseState* state)
{
    checkState(state);
    return state->current[0] == ';';
}

static boolean peekIdentifier(const ParseState* state)
{
    checkState(state);
    return isInitialIdentifierCharacter(state->current[0]);
}

static int readIdentifier(ParseState* state)
{
    const byte* begin = state->current;

    checkState(state);
    assert(peekIdentifier(state));
    while (isIdentifierCharacter(*++state->current));
    return StringPoolAdd2((const char*)begin, state->current - begin);
}

static boolean ParseScript(ParseState* state)
{
    boolean inFunction = false;

    checkState(state);
    while (!eof(state))
    {
        if (peekIdentifier(state))
        {
            printf("Target: %s line: %d\n", StringPoolGetString(readIdentifier(state)), state->line);
            skipEndOfLine(state);
            inFunction = true;
        }
        else if ((peekIndent(state) && inFunction) ||
                 peekComment(state))
        {
            skipEndOfLine(state);
        }
        else if (readNewline(state))
        {
        }
        else
        {
            sprintf(errorBuffer, "Unsupported character: %d",
                    state->current[0]);
            error(errorBuffer);
            return false;
        }
    }
    return true;
}

boolean ParseFile(fileref file)
{
    ParseState state;
    state.start = FileIndexGetContents(file);
    state.current = state.start;
    state.file = file;
    state.line = 1;
    return ParseScript(&state);
}
