#include <stdio.h>
#include "builder.h"
#include "bytevector.h"
#include "intvector.h"
#include "stringpool.h"
#include "fileindex.h"
#include "targetindex.h"
#include "log.h"
#include "native.h"
#include "parser.h"
#include "parsestate.h"

static char errorBuffer[256];

static stringref keywordElse;
static stringref keywordIf;
static stringref keywordWhile;

static stringref maxStatementKeyword;
static stringref maxKeyword;

static boolean isInitialIdentifierCharacter(byte c)
{
    return (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z');
}

static boolean isIdentifierCharacter(byte c)
{
    return (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9');
}

static void error(const ParseState* state, const char* message)
{
    LogParseError(state->file, state->line, message);
}

static void errorOnLine(const ParseState* state, uint line, const char* message)
{
    LogParseError(state->file, line, message);
}

static void statementError(const ParseState* state, const char* message)
{
    LogParseError(state->file, state->statementLine, message);
}

static uint getOffset(const ParseState* state, const byte* begin)
{
    ParseStateCheck(state);
    return (uint)(state->current - begin);
}


static int unwindBlocks(ParseState* state, uint indent, stringref identifier)
{
    while (!ParseStateBlockEmpty(state))
    {
        uint oldIndent = ParseStateBlockIndent(state);
        if (!ParseStateBlockEnd(
                state, oldIndent == indent && identifier == keywordElse))
        {
            assert(false); /* TODO: Error handling */
            return -1;
        }
        if (oldIndent == indent)
        {
            return (int)indent;
        }
        if (oldIndent < indent)
        {
            statementError(state, "Mismatched indentation level.");
            return -1;
        }
    }
    if (indent == 0)
    {
        return 0;
    }
    statementError(state, "Mismatched indentation level.");
    return -1;
}

static boolean eof(const ParseState* state)
{
    ParseStateCheck(state);
    return state->current == state->start + FileIndexGetSize(state->file);
}

static void skipWhitespace(ParseState* state)
{
    ParseStateCheck(state);
    while (state->current[0] == ' ')
    {
        state->current++;
    }
}

static void skipEndOfLine(ParseState* state)
{
    ParseStateCheck(state);
    while (!eof(state) && *state->current++ != '\n');
    state->line++;
}

static boolean peekNewline(ParseState* state)
{
    return state->current[0] == '\n';
}

static boolean readNewline(ParseState* state)
{
    ParseStateCheck(state);
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
    ParseStateCheck(state);
    return state->current[0] == ' ';
}

static uint readIndent(ParseState* state)
{
    const byte* begin = state->current;

    ParseStateCheck(state);
    skipWhitespace(state);
    return getOffset(state, begin);
}

static boolean peekComment(const ParseState* state)
{
    ParseStateCheck(state);
    return state->current[0] == ';';
}

static boolean peekIdentifier(const ParseState* state)
{
    ParseStateCheck(state);
    return isInitialIdentifierCharacter(state->current[0]);
}

static stringref readIdentifier(ParseState* state)
{
    const byte* begin = state->current;

    ParseStateCheck(state);
    assert(peekIdentifier(state));
    while (isIdentifierCharacter(*++state->current));
    return StringPoolAdd2((const char*)begin, getOffset(state, begin));
}

static stringref peekReadIdentifier(ParseState* state)
{
    return peekIdentifier(state) ? readIdentifier(state) : 0;
}

static boolean isKeyword(stringref identifier)
{
    return identifier <= maxKeyword;
}

static boolean peekString(const ParseState* state)
{
    ParseStateCheck(state);
    return state->current[0] == '"';
}

static stringref readString(ParseState* state)
{
    const byte* begin;
    stringref s;
    ParseStateCheck(state);
    assert(peekString(state));
    begin = ++state->current;
    while (state->current[0] != '"')
    {
        assert(!eof(state)); /* TODO: error handling */
        assert(!peekNewline(state)); /* TODO: error handling */
        state->current++;
    }
    s = StringPoolAdd2((const char*)begin, getOffset(state, begin));
    state->current++;
    return s;
}

static boolean readOperator(ParseState* state, byte op)
{
    if (state->current[0] == op)
    {
        state->current++;
        return true;
    }
    return false;
}

static boolean readExpectedOperator(ParseState* state, byte op)
{
    if (!readOperator(state, op))
    {
        sprintf(errorBuffer, "Expected operator %c. Got %c", op,
                state->current[0]);
        error(state, errorBuffer);
        return false;
    }
    return true;
}


static int parseExpression(ParseState* state)
{
    stringref identifier;
    ParseStateCheck(state);
    if (peekIdentifier(state))
    {
        identifier = readIdentifier(state);
        assert(!isKeyword(identifier));
        return ParseStateGetVariable(state, identifier);
    }
    else if (peekString(state))
    {
        return ParseStateWriteStringLiteral(state, readString(state));
    }
    statementError(state, "Invalid expression.");
    return -1;
}

static boolean parseInvocationRest(ParseState* state, stringref name)
{
    nativefunctionref nativeFunction = NativeFindFunction(name);
    uint parameterCount = NativeGetParameterCount(nativeFunction);
    stringref* parameterNames = NativeGetParameterNames(nativeFunction);
    uint argumentOutputOffset = ParseStateWriteArguments(state, parameterCount);
    uint argumentCount = 0;
    uint line = state->line;
    int value;

    ParseStateCheck(state);
    assert(nativeFunction >= 0);
    assert(parameterNames);

    if (argumentOutputOffset == 0)
    {
        return false;
    }

    if (!readOperator(state, ')'))
    {
        for (;;)
        {
            value = parseExpression(state);
            if (value < 0)
            {
                return false;
            }
            ParseStateSetArgument(
                state,
                argumentOutputOffset + argumentCount++ * (uint)sizeof(int),
                value);
            if (readOperator(state, ')'))
            {
                break;
            }
            if (!readExpectedOperator(state, ','))
            {
                return false;
            }
        }
    }
    if (argumentCount > parameterCount)
    {
        sprintf(errorBuffer,
                "Too many arguments. Got %d arguments, but at most %d were expected.",
                argumentCount, parameterCount);
        errorOnLine(state, line, errorBuffer);
        return false;
    }
    if (argumentCount < NativeGetMinimumArgumentCount(nativeFunction))
    {
        sprintf(errorBuffer,
                "Too few arguments. Got %d arguments, but at least %d were expected.",
                argumentCount, parameterCount);
        errorOnLine(state, line, errorBuffer);
        return false;
    }
    return ParseStateWriteNativeInvocation(state, nativeFunction,
                                           argumentOutputOffset);
}

static boolean parseFunctionBody(ParseState* state)
{
    int indent;
    int currentIndent = -1;
    int prevIndent = 0;
    stringref identifier;
    int value;

    for (;;)
    {
        state->statementLine = state->line;
        if (eof(state))
        {
            unwindBlocks(state, 0, 0);
            if (!ParseStateWriteReturn(state))
            {
                return false;
            }
            break;
        }

        indent = (int)readIndent(state);
        if (readNewline(state))
        {
        }
        else if (peekComment(state))
        {
            skipEndOfLine(state);
        }
        else
        {
            identifier = peekReadIdentifier(state);
            if (indent != currentIndent)
            {
                if (currentIndent < 0)
                {
                    if (indent <= prevIndent)
                    {
                        statementError(state, "Expected increased indentation level.");
                        return false;
                    }
                    currentIndent = indent;
                }
                else if (indent < currentIndent)
                {
                    currentIndent = unwindBlocks(state, (uint)indent, identifier);
                    if (currentIndent < 0)
                    {
                        return false;
                    }
                    if (indent == 0)
                    {
                        if (!ParseStateWriteReturn(state))
                        {
                            return false;
                        }
                        break;
                    }
                    if (identifier == keywordElse)
                    {
                        prevIndent = currentIndent;
                        currentIndent = -1;
                        if (!peekNewline(state))
                        {
                            error(state, "Garbage after else statement.");
                            return false;
                        }
                        skipEndOfLine(state);
                        continue;
                    }
                }
                else
                {
                    statementError(state, "Mismatched indentation level.");
                    return false;
                }
            }
            if (identifier)
            {
                skipWhitespace(state);
                if (isKeyword(identifier))
                {
                    if (identifier > maxStatementKeyword)
                    {
                        statementError(state, "Not a statement.");
                        return false;
                    }
                    if (identifier == keywordIf)
                    {
                        if (!ParseStateBlockBegin(state, (uint)currentIndent,
                                                  false, true))
                        {
                            return false;
                        }
                        prevIndent = currentIndent;
                        currentIndent = -1;
                        value = parseExpression(state);
                        if (value < 0 || !ParseStateWriteIf(state, (uint)value))
                        {
                            return false;
                        }
                        if (!peekNewline(state))
                        {
                            error(state, "Garbage after if statement.");
                            return false;
                        }
                        skipEndOfLine(state);
                    }
                    else if (identifier == keywordElse)
                    {
                        statementError(state, "else without matching if.");
                        return false;
                    }
                    else if (identifier == keywordWhile)
                    {
                        if (!ParseStateBlockBegin(state, (uint)currentIndent,
                                                  true, false))
                        {
                            return false;
                        }
                        prevIndent = currentIndent;
                        currentIndent = -1;
                        value = parseExpression(state);
                        if (value < 0 || !ParseStateWriteWhile(state, (uint)value))
                        {
                            return false;
                        }
                        if (!peekNewline(state))
                        {
                            error(state, "Garbage after while statement.");
                            return false;
                        }
                        skipEndOfLine(state);
                    }
                    else
                    {
                        assert(false);
                        return false;
                    }
                }
                else
                {
                    if (readOperator(state, '('))
                    {
                        if (!parseInvocationRest(state, identifier))
                        {
                            return false;
                        }
                    }
                    else if (readOperator(state, '='))
                    {
                        skipWhitespace(state);
                        value = parseExpression(state);
                        if (value < 0)
                        {
                            return false;
                        }
                        ParseStateSetVariable(state, identifier, value);
                    }
                    else
                    {
                        statementError(state, "Not a statement1.");
                        return false;
                    }
                    assert(peekNewline(state));
                    skipEndOfLine(state);
                }
            }
            else if (peekNewline(state) || peekComment(state))
            {
                skipEndOfLine(state);
            }
            else
            {
                statementError(state, "Not a statement2.");
                return false;
            }
        }
    }
    assert(ParseStateBlockEmpty(state));
    return true;
}

static boolean parseScript(ParseState* state)
{
    boolean inFunction = false;

    ParseStateCheck(state);
    while (!eof(state))
    {
        if (peekIdentifier(state))
        {
            TargetIndexAdd(readIdentifier(state), state->file, (int)state->line,
                           (int)getOffset(state, state->start));
            skipEndOfLine(state);
            inFunction = true;
        }
        else if ((peekIndent(state) && inFunction) ||
                 peekComment(state))
        {
            skipEndOfLine(state);
        }
        else if (!readNewline(state))
        {
            sprintf(errorBuffer, "Unsupported character: %d",
                    state->current[0]);
            error(state, errorBuffer);
            return false;
        }
    }
    return true;
}

void ParserAddKeywords(void)
{
    keywordElse = StringPoolAdd("else");
    keywordIf = StringPoolAdd("if");
    keywordWhile = StringPoolAdd("while");
    maxStatementKeyword = keywordWhile;
    maxKeyword = keywordWhile;
}

boolean ParseFile(fileref file)
{
    ParseState state;
    boolean result;
    ParseStateInit(&state, file, 1, 0);
    result = parseScript(&state);
    ParseStateDispose(&state);
    return result;
}

boolean ParseTarget(targetref target)
{
    ParseState state;
    stringref name;
    boolean result;
    assert(target >= 0);
    ParseStateInit(&state,
                   TargetIndexGetFile(target),
                   TargetIndexGetLine(target),
                   TargetIndexGetOffset(target));
    name = readIdentifier(&state);
    assert(name == TargetIndexGetName(target));
    if (readOperator(&state, ':'))
    {
        assert(peekNewline(&state)); /* TODO: Error handling */
        skipEndOfLine(&state);
        result = parseFunctionBody(&state);
    }
    else
    {
        error(&state, "Expected ':' after target name.");
        result = false;
    }
    ParseStateDispose(&state);
    return result;
}
