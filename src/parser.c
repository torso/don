#include <stdlib.h>
#include <stdio.h>
#include "builder.h"
#include "bytevector.h"
#include "intvector.h"
#include "stringpool.h"
#include "fileindex.h"
#include "targetindex.h"
#include "log.h"
#include "interpreterstate.h"
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

static uint parseExpression(ParseState *state);

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
    ParseStateSetFailed(state, BUILD_ERROR);
    LogParseError(state->file, state->line, message);
}

static void errorOnLine(ParseState *state, uint line, const char *message)
{
    ParseStateSetFailed(state, BUILD_ERROR);
    LogParseError(state->file, line, message);
}

static void statementError(ParseState *state, const char *message)
{
    ParseStateSetFailed(state, BUILD_ERROR);
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
    stringref identifier;

    ParseStateCheck(state);
    assert(peekIdentifier(state));
    while (isIdentifierCharacter(*++state->current));
    identifier = StringPoolAdd2((const char*)begin, getOffset(state, begin));
    if (!identifier)
    {
        ParseStateSetFailed(state, OUT_OF_MEMORY);
    }
    return identifier;
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
    if (!s)
    {
        ParseStateSetFailed(state, OUT_OF_MEMORY);
        return 0;
    }
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
        sprintf(errorBuffer, "Expected operator '%c'. Got '%c'", op,
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

static uint parseInvocationRest(ParseState *state, stringref name)
{
    nativefunctionref nativeFunction = NativeFindFunction(name);
    targetref target = 0;
    uint parameterCount;
    const stringref *parameterNames;
    uint minimumArgumentCount;
    uint *arguments;
    uint argumentCount = 0;
    uint returnValue;
    uint line = state->line;
    uint expression;

    ParseStateCheck(state);
    if (nativeFunction >= 0)
    {
        parameterCount = NativeGetParameterCount(nativeFunction);
        parameterNames = NativeGetParameterNames(nativeFunction);
        minimumArgumentCount = NativeGetMinimumArgumentCount(nativeFunction);
    }
    else
    {
        target = TargetIndexGet(name);
        if (!target)
        {
            sprintf(errorBuffer, "Unknown function '%s'.",
                    StringPoolGetString(name));
            statementError(state, errorBuffer);
            return 0;
        }
        TargetIndexMarkForParsing(target);
        parameterCount = TargetIndexGetParameterCount(target);
        parameterNames = TargetIndexGetParameterNames(target);
        minimumArgumentCount = TargetIndexGetMinimumArgumentCount(target);
    }
    assert(parameterNames || !parameterCount);

    /* TODO: Avoid malloc */
    arguments = (uint*)zmalloc(parameterCount * sizeof(uint));
    assert(arguments); /* TODO: Error handling */

    if (!readOperator(state, ')'))
    {
        for (;;)
        {
            expression = parseExpression(state);
            if (state->error)
            {
                free(arguments);
                return 0;
            }
            if (argumentCount < parameterCount)
            {
                arguments[argumentCount] = expression;
            }
            argumentCount++;
            if (readOperator(state, ')'))
            {
                break;
            }
            if (!readExpectedOperator(state, ','))
            {
                free(arguments);
                return 0;
            }
            skipWhitespace(state);
        }
    }
    if (argumentCount > parameterCount)
    {
        free(arguments);
        if (!parameterCount)
        {
            sprintf(errorBuffer,
                    "Function '%s' does not take any arguments.",
                    StringPoolGetString(name));
        }
        else
        {
            sprintf(errorBuffer,
                    "Too many arguments for function '%s'. Got %d arguments, but at most %d were expected.",
                    StringPoolGetString(name), argumentCount, parameterCount);
        }
        errorOnLine(state, line, errorBuffer);
        return 0;
    }
    if (argumentCount < minimumArgumentCount)
    {
        free(arguments);
        sprintf(errorBuffer,
                "Too few arguments for function '%s'. Got %d arguments, but at least %d were expected.",
                StringPoolGetString(name), argumentCount, parameterCount);
        errorOnLine(state, line, errorBuffer);
        return 0;
    }
    returnValue = ParseStateWriteInvocation(state, nativeFunction, target,
                                            parameterCount, arguments);
    free(arguments);
    return returnValue;
}

static uint parseListRest(ParseState *state)
{
    intvector values;
    uint value;

    if (ParseStateSetError(state, IntVectorInit(&values)))
    {
        return 0;
    }
    skipWhitespace(state);
    while (!readOperator(state, ']'))
    {
        value = parseExpression(state);
        skipWhitespace(state);
        if (state->error)
        {
            IntVectorDispose(&values);
            return 0;
        }
        if (ParseStateSetError(state, IntVectorAdd(&values, value)))
        {
            return 0;
        }
    }
    value = ParseStateWriteList(state, &values);
    IntVectorDispose(&values);
    return value;
}

static uint parseExpression5(ParseState *state)
{
    stringref identifier;
    stringref string;

    ParseStateCheck(state);
    if (peekIdentifier(state))
    {
        identifier = readIdentifier(state);
        if (state->error)
        {
            return 0;
        }
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
        if (readOperator(state, '('))
        {
            return parseInvocationRest(state, identifier);
        }
        return ParseStateGetVariable(state, identifier);
    }
    else if (peekNumber(state))
    {
        return parseNumber(state);
    }
    else if (peekString(state))
    {
        string = readString(state);
        if (state->error)
        {
            return 0;
        }
        return ParseStateWriteStringLiteral(state, string);
    }
    else if (readOperator(state, '['))
    {
        return parseListRest(state);
    }
    statementError(state, "Invalid expression.");
    return 0;
}

static uint parseExpression4(ParseState *state)
{
    uint value = parseExpression5(state);
    uint indexValue;

    do
    {
        if (readOperator(state, '['))
        {
            skipWhitespace(state);
            indexValue = parseExpression(state);
            if (state->error)
            {
                return 0;
            }
            skipWhitespace(state);
            if (!readExpectedOperator(state, ']'))
            {
                return 0;
            }
            value = ParseStateWriteBinaryOperation(state, DATAOP_INDEXED_ACCESS,
                                                   value, indexValue);
        }
        else
        {
            break;
        }
    }
    while (!state->error);
    return value;
}

static uint parseExpression3(ParseState *state)
{
    uint value = parseExpression4(state);
    uint value2;

    if (state->error)
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

    if (state->error)
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

    if (state->error)
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
            if (state->error)
            {
                return false;
            }
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
                        if (state->error)
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
                        if (state->error)
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
                        if (state->error)
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
                        parseInvocationRest(state, identifier);
                        if (state->error)
                        {
                            return false;
                        }
                    }
                    else if (readOperator(state, '='))
                    {
                        skipWhitespace(state);
                        value = parseExpression(state);
                        if (state->error ||
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

static void parseScript(ParseState *state)
{
    boolean inFunction = false;
    boolean isTarget;
    stringref target;
    stringref parameterName;

    ParseStateCheck(state);
    while (!eof(state))
    {
        if (peekIdentifier(state))
        {
            target = readIdentifier(state);
            if (state->error)
            {
                return;
            }
            state->error = TargetIndexBeginTarget(target);
            if (state->error)
            {
                return;
            }
            if (readOperator(state, ':'))
            {
                isTarget = true;
            }
            else if (readOperator(state, '('))
            {
                isTarget = false;
                skipWhitespace(state);
                if (!readOperator(state, ')'))
                {
                    for (;;)
                    {
                        parameterName = peekReadIdentifier(state);
                        if (state->error)
                        {
                            return;
                        }
                        if (!parameterName)
                        {
                            error(state, "Expected parameter name or ')'.");
                            return;
                        }
                        skipWhitespace(state);
                        state->error =
                            TargetIndexAddParameter(parameterName, true);
                        if (state->error)
                        {
                            return;
                        }
                        if (readOperator(state, ')'))
                        {
                            break;
                        }
                        if (!readOperator(state, ','))
                        {
                            error(state, "Expected ',' or ')'.");
                            return;
                        }
                        skipWhitespace(state);
                    }
                }
            }
            else
            {
                error(state, "Invalid function declaration.");
                return;
            }
            /* TODO: Parse arguments */
            assert(peekNewline(state));
            skipEndOfLine(state);
            TargetIndexFinishTarget(state->file, state->line,
                                    getOffset(state, state->start), isTarget);
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
            return;
        }
    }
}

ErrorCode ParserAddKeywords(void)
{
    if (!(keywordElse = StringPoolAdd("else")) ||
        !(keywordFalse = StringPoolAdd("false")) ||
        !(keywordIf = StringPoolAdd("if")) ||
        !(keywordNull = StringPoolAdd("null")) ||
        !(keywordTrue = StringPoolAdd("true")) ||
        !(keywordWhile = StringPoolAdd("while")))
    {
        return OUT_OF_MEMORY;
    }
    maxStatementKeyword = keywordWhile;
    maxKeyword = keywordWhile;
    return NO_ERROR;
}

ErrorCode ParseFile(fileref file)
{
    ParseState state;
    ParseStateInit(&state, file, 1, 0);
    if (state.error)
    {
        return state.error;
    }
    parseScript(&state);
    ParseStateDispose(&state);
    return state.error;
}

static boolean parseFunctionRest(ParseState *state, targetref target,
                                 bytevector *parsed)
{
    if (!parseFunctionBody(state, parsed) || state->error)
    {
        ParseStateDispose(state);
        return false;
    }
    TargetIndexSetBytecodeOffset(target, state->parsedOffset);
    return true;
}

ErrorCode ParseFunction(targetref target, bytevector *parsed)
{
    ParseState state;
    assert(target);
    ParseStateInit(&state,
                   TargetIndexGetFile(target),
                   TargetIndexGetLine(target),
                   TargetIndexGetFileOffset(target));
    if (state.error)
    {
        return state.error;
    }
    parseFunctionRest(&state, target, parsed);
    ParseStateDispose(&state);
    return state.error;
}
