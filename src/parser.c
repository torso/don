#include <stdio.h>
#include "builder.h"
#include "bytevector.h"
#include "intvector.h"
#include "stringpool.h"
#include "fileindex.h"
#include "targetindex.h"
#include "log.h"
#include "native.h"
#include "instruction.h"
#include "parser.h"
#include "parsestate.h"

static char errorBuffer[256];

static stringref keywordElse;
static stringref keywordIf;
static stringref keywordFalse;
static stringref keywordNull;
static stringref keywordTrue;
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

static void error(ParseState *state, const char *message)
{
    ParseStateSetFailed(state);
    LogParseError(state->file, state->line, message);
}

static void errorOnLine(ParseState *state, uint line, const char *message)
{
    ParseStateSetFailed(state);
    LogParseError(state->file, line, message);
}

static void statementError(ParseState *state, const char *message)
{
    ParseStateSetFailed(state);
    LogParseError(state->file, state->statementLine, message);
}

static uint getOffset(const ParseState *state, const byte *begin)
{
    ParseStateCheck(state);
    return (uint)(state->current - begin);
}


static boolean unwindBlocks(ParseState *restrict state,
                            bytevector *restrict parsed,
                            uint indent, boolean trailingElse)
{
    while (ParseStateBlockIndent(state) > indent)
    {
        if (!ParseStateFinishBlock(state, parsed, indent, trailingElse))
        {
            return false;
        }
    }
    return true;
}

static boolean eof(const ParseState *state)
{
    ParseStateCheck(state);
    return state->current == state->start + FileIndexGetSize(state->file);
}

static void skipWhitespace(ParseState *state)
{
    ParseStateCheck(state);
    while (state->current[0] == ' ')
    {
        state->current++;
    }
}

static void skipEndOfLine(ParseState *state)
{
    ParseStateCheck(state);
    while (!eof(state) && *state->current++ != '\n');
    state->line++;
}

static boolean peekNewline(ParseState *state)
{
    return state->current[0] == '\n';
}

static boolean readNewline(ParseState *state)
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

static boolean peekIndent(const ParseState *state)
{
    ParseStateCheck(state);
    return state->current[0] == ' ';
}

static uint readIndent(ParseState *state)
{
    const byte *begin = state->current;

    ParseStateCheck(state);
    skipWhitespace(state);
    return getOffset(state, begin);
}

static boolean peekComment(const ParseState *state)
{
    ParseStateCheck(state);
    return state->current[0] == ';';
}

static boolean peekIdentifier(const ParseState *state)
{
    ParseStateCheck(state);
    return isInitialIdentifierCharacter(state->current[0]);
}

static stringref readIdentifier(ParseState *state)
{
    const byte *begin = state->current;

    ParseStateCheck(state);
    assert(peekIdentifier(state));
    while (isIdentifierCharacter(*++state->current));
    return StringPoolAdd2((const char*)begin, getOffset(state, begin));
}

static stringref peekReadIdentifier(ParseState *state)
{
    return peekIdentifier(state) ? readIdentifier(state) : 0;
}

static boolean isKeyword(stringref identifier)
{
    return identifier <= maxKeyword;
}

static boolean isDigit(byte b)
{
    return b >= '0' && b <= '9';
}

static boolean peekNumber(const ParseState *state)
{
    ParseStateCheck(state);
    return isDigit(state->current[0]);
}

static boolean peekString(const ParseState *state)
{
    ParseStateCheck(state);
    return state->current[0] == '"';
}

static stringref readString(ParseState *state)
{
    const byte *begin;
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

static boolean readOperator(ParseState *state, byte op)
{
    if (state->current[0] == op)
    {
        state->current++;
        return true;
    }
    return false;
}

static boolean readExpectedOperator(ParseState *state, byte op)
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


/* TODO: Parse big numbers */
/* TODO: Parse non-decimal numbers */
/* TODO: Parse non-integer numbers */
static uint parseNumber(ParseState *state)
{
    int value = 0;

    assert(peekNumber(state));

    do
    {
        value = value * 10 + state->current[0] - '0';
        assert(value >= 0);
        state->current++;
    }
    while (isDigit(state->current[0]));

    return ParseStateWriteIntegerLiteral(state, value);
}

static uint parseExpression4(ParseState *state)
{
    stringref identifier;
    ParseStateCheck(state);
    if (peekIdentifier(state))
    {
        identifier = readIdentifier(state);
        if (isKeyword(identifier))
        {
            if (identifier == keywordTrue)
            {
                return ParseStateWriteTrueLiteral(state);
            }
            else if (identifier == keywordFalse)
            {
                return ParseStateWriteFalseLiteral(state);
            }
            else if (identifier == keywordNull)
            {
                return ParseStateWriteNullLiteral(state);
            }
            sprintf(errorBuffer, "Unexpected keyword '%s'.",
                    StringPoolGetString(identifier));
            statementError(state, errorBuffer);
            return 0;
        }
        return ParseStateGetVariable(state, identifier);
    }
    else if (peekNumber(state))
    {
        return parseNumber(state);
    }
    else if (peekString(state))
    {
        return ParseStateWriteStringLiteral(state, readString(state));
    }
    statementError(state, "Invalid expression.");
    return 0;
}

static uint parseExpression3(ParseState *state)
{
    uint value = parseExpression4(state);
    uint value2;

    if (state->failed)
    {
        return 0;
    }
    skipWhitespace(state);
    if (readOperator(state, '+'))
    {
        assert(!readOperator(state, '+')); /* TODO: ++ operator */
        skipWhitespace(state);
        value2 = parseExpression4(state);
        value = ParseStateWriteBinaryOperation(
            state, DATAOP_ADD, value, value2);
    }
    else if (readOperator(state, '-'))
    {
        assert(!readOperator(state, '-')); /* TODO: -- operator */
        skipWhitespace(state);
        value2 = parseExpression4(state);
        value = ParseStateWriteBinaryOperation(
            state, DATAOP_SUB, value, value2);
    }
    return value;
}

static uint parseExpression2(ParseState *state)
{
    uint value = parseExpression3(state);
    uint value2;

    if (state->failed)
    {
        return 0;
    }
    skipWhitespace(state);
    if (readOperator(state, '='))
    {
        if (!readOperator(state, '='))
        {
            statementError(state, "Assignment not allowed here.");
            return 0;
        }
        skipWhitespace(state);
        value2 = parseExpression3(state);
        value = ParseStateWriteBinaryOperation(
            state, DATAOP_EQUALS, value, value2);
    }
    return value;
}

static uint parseExpression(ParseState *state)
{
    uint value = parseExpression2(state);
    uint value2;
    uint value3;

    if (state->failed)
    {
        return 0;
    }
    skipWhitespace(state);
    if (readOperator(state, '?'))
    {
        skipWhitespace(state);
        value2 = parseExpression2(state);
        skipWhitespace(state);
        if (!readOperator(state, ':'))
        {
            statementError(state, "Expected operator ':'.");
            return 0;
        }
        skipWhitespace(state);
        value3 = parseExpression2(state);
        value = ParseStateWriteTernaryOperation(
            state, DATAOP_CONDITION, value, value3, value2);
    }
    return value;
}

static boolean parseInvocationRest(ParseState *state, stringref name)
{
    nativefunctionref nativeFunction = NativeFindFunction(name);
    uint parameterCount = NativeGetParameterCount(nativeFunction);
    stringref *parameterNames = NativeGetParameterNames(nativeFunction);
    uint argumentOutputOffset;
    uint argumentCount = 0;
    uint line = state->line;
    uint value;

    ParseStateCheck(state);
    assert(nativeFunction >= 0);
    assert(parameterNames);

    argumentOutputOffset = ParseStateWriteNativeInvocation(
        state, nativeFunction, parameterCount);
    if (argumentOutputOffset == 0)
    {
        return false;
    }

    if (!readOperator(state, ')'))
    {
        for (;;)
        {
            value = parseExpression(state);
            if (state->failed)
            {
                return false;
            }
            ParseStateSetArgument(
                state,
                argumentOutputOffset,
                argumentCount++,
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
    return true;
}

static boolean parseFunctionBody(ParseState *state,
                                 bytevector *restrict parsed)
{
    uint indent;
    uint currentIndent = 0;
    uint prevIndent = 0;
    stringref identifier;
    uint value;

    for (;;)
    {
        if (eof(state))
        {
            return unwindBlocks(state, parsed, 0, false);
        }

        indent = readIndent(state);
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
                if (!currentIndent)
                {
                    if (indent <= prevIndent)
                    {
                        error(state, "Expected increased indentation level.");
                        return false;
                    }
                    ParseStateSetIndent(state, indent);
                    currentIndent = indent;
                }
                else if (indent < currentIndent)
                {
                    if (!unwindBlocks(state, parsed, indent,
                                      identifier == keywordElse))
                    {
                        return false;
                    }
                    if (indent == 0)
                    {
                        return true;
                    }
                    currentIndent = indent;
                    if (identifier == keywordElse)
                    {
                        if (state->failed)
                        {
                            statementError(state, "else without matching if.");
                            return false;
                        }
                        prevIndent = indent;
                        currentIndent = 0;
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
                    error(state, "Mismatched indentation level.");
                    return false;
                }
            }
            state->statementLine = state->line;
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
                        prevIndent = currentIndent;
                        currentIndent = 0;
                        value = parseExpression(state);
                        if (state->failed)
                        {
                            return false;
                        }
                        if (!peekNewline(state))
                        {
                            error(state, "Garbage after if statement.");
                            return false;
                        }
                        skipEndOfLine(state);
                        if (!ParseStateWriteIf(state, value))
                        {
                            return false;
                        }
                    }
                    else if (identifier == keywordElse)
                    {
                        statementError(state, "else without matching if.");
                        return false;
                    }
                    else if (identifier == keywordWhile)
                    {
                        prevIndent = currentIndent;
                        currentIndent = 0;
                        value = parseExpression(state);
                        if (state->failed)
                        {
                            return false;
                        }
                        if (!peekNewline(state))
                        {
                            error(state, "Garbage after while statement.");
                            return false;
                        }
                        skipEndOfLine(state);
                        if (!ParseStateWriteWhile(state, value))
                        {
                            return false;
                        }
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
                        if (state->failed ||
                            !ParseStateSetVariable(state, identifier, value))
                        {
                            return false;
                        }
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
}

static boolean parseScript(ParseState *state)
{
    boolean inFunction = false;

    ParseStateCheck(state);
    while (!eof(state))
    {
        if (peekIdentifier(state))
        {
            TargetIndexAdd(readIdentifier(state), state->file, state->line,
                           getOffset(state, state->start));
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
    keywordFalse = StringPoolAdd("false");
    keywordNull = StringPoolAdd("null");
    keywordTrue = StringPoolAdd("true");
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

boolean ParseTarget(targetref target, bytevector *parsed)
{
    ParseState state;
    stringref name;
    assert(target >= 0);
    ParseStateInit(&state,
                   TargetIndexGetFile(target),
                   TargetIndexGetLine(target),
                   TargetIndexGetOffset(target));
    name = readIdentifier(&state);
    assert(name == TargetIndexGetName(target));
    if (!readOperator(&state, ':'))
    {
        error(&state, "Expected ':' after target name.");
        ParseStateDispose(&state);
        return false;
    }
    assert(peekNewline(&state)); /* TODO: Error handling */
    skipEndOfLine(&state);
    if (!parseFunctionBody(&state, parsed) ||
        state.failed)
    {
        ParseStateDispose(&state);
        return false;
    }
    TargetIndexSetParsedOffset(target, state.parsedOffset);
    ParseStateDispose(&state);
    return true;
}
