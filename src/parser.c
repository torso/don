#include <stdlib.h>
#include <stdio.h>
#include "builder.h"
#include "bytevector.h"
#include "fileindex.h"
#include "inthashmap.h"
#include "instruction.h"
#include "intvector.h"
#include "log.h"
#include "native.h"
#include "parser.h"
#include "parsestate.h"
#include "stringpool.h"
#include "targetindex.h"

static char errorBuffer[256];

static stringref keywordElse;
static stringref keywordFalse;
static stringref keywordIf;
static stringref keywordNull;
static stringref keywordReturn;
static stringref keywordTrue;
static stringref keywordWhile;

static stringref maxStatementKeyword;
static stringref maxKeyword;

static boolean parseExpression(ParseState *state);

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
    ParseStateSetError(state, BUILD_ERROR);
    LogParseError(state->file, state->line, message);
}

static void errorOnLine(ParseState *state, uint line, const char *message)
{
    ParseStateSetError(state, BUILD_ERROR);
    LogParseError(state->file, line, message);
}

static void statementError(ParseState *state, const char *message)
{
    ParseStateSetError(state, BUILD_ERROR);
    LogParseError(state->file, state->statementLine, message);
}

static uint getOffset(const ParseState *state, const byte *begin)
{
    ParseStateCheck(state);
    return (uint)(state->current - begin);
}


static boolean unwindBlocks(ParseState *restrict state,
                            uint indent, boolean trailingElse)
{
    while (ParseStateBlockIndent(state) > indent)
    {
        if (!ParseStateFinishBlock(state, indent, trailingElse))
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
        ParseStateSetError(state, OUT_OF_MEMORY);
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
        ParseStateSetError(state, OUT_OF_MEMORY);
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
static boolean parseNumber(ParseState *state)
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

static boolean parseInvocationRest(ParseState *state, stringref name,
                                   uint returnValues)
{
    nativefunctionref nativeFunction = NativeFindFunction(name);
    targetref target = 0;
    uint parameterCount;
    const stringref *parameterNames;
    uint minimumArgumentCount;
    uint argumentCount = 0;
    uint line = state->line;

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
            return false;
        }
        TargetIndexMarkForParsing(target);
        parameterCount = TargetIndexGetParameterCount(target);
        parameterNames = TargetIndexGetParameterNames(target);
        minimumArgumentCount = TargetIndexGetMinimumArgumentCount(target);
    }
    assert(parameterNames || !parameterCount);

    if (!readOperator(state, ')'))
    {
        for (;;)
        {
            if (!parseExpression(state))
            {
                return false;
            }
            argumentCount++;
            if (readOperator(state, ')'))
            {
                break;
            }
            if (!readExpectedOperator(state, ','))
            {
                return false;
            }
            skipWhitespace(state);
        }
    }
    if (argumentCount > parameterCount)
    {
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
        sprintf(errorBuffer,
                "Too few arguments for function '%s'. Got %d arguments, but at least %d were expected.",
                StringPoolGetString(name), argumentCount, parameterCount);
        errorOnLine(state, line, errorBuffer);
        return 0;
    }
    return ParseStateWriteInvocation(state, nativeFunction, target,
                                     argumentCount, returnValues);
}

static boolean parseReturnRest(ParseState *state)
{
    uint values = 0;

    if (peekNewline(state))
    {
        return ParseStateWriteReturnVoid(state);
    }
    for (;;)
    {
        if (!parseExpression(state))
        {
            return false;
        }
        values++;
        if (peekNewline(state))
        {
            return ParseStateWriteReturn(state, values);
        }
        if (!readExpectedOperator(state, ','))
        {
            return false;
        }
        skipWhitespace(state);
    }
}

static boolean parseMultiAssignmentRest(ParseState *state)
{
    intvector variables;
    stringref name;

    IntVectorInit(&variables);
    do
    {
        skipWhitespace(state);
        name = peekReadIdentifier(state);
        if (state->error)
        {
            return false;
        }
        if (!name)
        {
            statementError(state, "Expected variable name.");
            return false;
        }
        state->error = IntVectorAdd(&variables, (uint)name);
        if (state->error)
        {
            return false;
        }
        skipWhitespace(state);
    }
    while (readOperator(state, ','));
    if (!readExpectedOperator(state, '='))
    {
        return false;
    }
    skipWhitespace(state);
    name = peekReadIdentifier(state);
    if (state->error)
    {
        return false;
    }
    if (!name || !readOperator(state, '('))
    {
        statementError(state, "Expected function invocation.");
        return false;
    }
    if (!parseInvocationRest(state, name, IntVectorSize(&variables) + 1))
    {
        return false;
    }
    while (IntVectorSize(&variables))
    {
        if (!ParseStateSetVariable(state, (stringref)IntVectorPop(&variables)))
        {
            return false;
        }
    }
    IntVectorDispose(&variables);
    return true;
}

static boolean parseExpression5(ParseState *state)
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
            return parseInvocationRest(state, identifier, 1);
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
            return false;
        }
        return ParseStateWriteStringLiteral(state, string);
    }
    statementError(state, "Invalid expression.");
    return false;
}

static boolean parseExpression4(ParseState *state)
{
    if (!parseExpression5(state))
    {
        return false;
    }
    skipWhitespace(state);
    return true;
}

static boolean parseExpression3(ParseState *state)
{
    if (!parseExpression4(state))
    {
        return false;
    }
    if (readOperator(state, '+'))
    {
        assert(!readOperator(state, '+')); /* TODO: ++ operator */
        skipWhitespace(state);
        return parseExpression4(state) &&
            ParseStateWriteBinaryOperation(state, OP_ADD);
    }
    else if (readOperator(state, '-'))
    {
        assert(!readOperator(state, '-')); /* TODO: -- operator */
        skipWhitespace(state);
        return parseExpression4(state) &&
            ParseStateWriteBinaryOperation(state, OP_SUB);
    }
    return true;
}

static boolean parseExpression2(ParseState *state)
{
    if (!parseExpression3(state))
    {
        return false;
    }
    if (readOperator(state, '='))
    {
        if (!readOperator(state, '='))
        {
            statementError(state, "Assignment not allowed here.");
            return 0;
        }
        skipWhitespace(state);
        return parseExpression3(state) &&
            ParseStateWriteBinaryOperation(state, OP_EQUALS);
    }
    if (readOperator(state, '!'))
    {
        if (!readOperator(state, '='))
        {
            statementError(state, "Invalid expression.");
            return 0;
        }
        skipWhitespace(state);
        return parseExpression3(state) &&
            ParseStateWriteBinaryOperation(state, OP_NOT_EQUALS);
    }
    return true;
}

static boolean parseExpression(ParseState *state)
{
    if (!parseExpression2(state))
    {
        return false;
    }
    if (readOperator(state, '?'))
    {
        skipWhitespace(state);
        /* TODO: Avoid recursion. */
        if (!ParseStateWriteBeginCondition(state) ||
            !parseExpression(state) ||
            !readExpectedOperator(state, ':') ||
            !ParseStateWriteSecondConsequent(state))
        {
            return false;
        }
        skipWhitespace(state);
        if (!parseExpression(state) ||
            !ParseStateWriteFinishCondition(state))
        {
            return false;
        }
    }
    return true;
}

static boolean parseFunctionBody(ParseState *state)
{
    uint indent;
    uint currentIndent = 0;
    uint prevIndent = 0;
    stringref identifier;
    uint target;

    for (;;)
    {
        if (eof(state))
        {
            return unwindBlocks(state, 0, false);
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
                    if (!unwindBlocks(state, indent, identifier == keywordElse))
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
                        if (!parseExpression(state))
                        {
                            return false;
                        }
                        if (!peekNewline(state))
                        {
                            error(state, "Garbage after if statement.");
                            return false;
                        }
                        skipEndOfLine(state);
                        if (!ParseStateWriteIf(state))
                        {
                            return false;
                        }
                    }
                    else if (identifier == keywordElse)
                    {
                        statementError(state, "else without matching if.");
                        return false;
                    }
                    else if (identifier == keywordReturn)
                    {
                        if (!parseReturnRest(state))
                        {
                            return false;
                        }
                    }
                    else if (identifier == keywordWhile)
                    {
                        prevIndent = currentIndent;
                        currentIndent = 0;
                        target = ParseStateGetJumpTarget(state);
                        if (!parseExpression(state))
                        {
                            return false;
                        }
                        if (!peekNewline(state))
                        {
                            error(state, "Garbage after while statement.");
                            return false;
                        }
                        skipEndOfLine(state);
                        if (!ParseStateWriteWhile(state, target))
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
                        parseInvocationRest(state, identifier, 0);
                        if (state->error)
                        {
                            return false;
                        }
                    }
                    else if (readOperator(state, '='))
                    {
                        skipWhitespace(state);
                        if (!parseExpression(state) ||
                            !ParseStateSetVariable(state, identifier))
                        {
                            return false;
                        }
                    }
                    else if (readOperator(state, ','))
                    {
                        if (!parseMultiAssignmentRest(state) ||
                            !ParseStateSetVariable(state, identifier))
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
        !(keywordReturn = StringPoolAdd("return")) ||
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
    ParseStateInit(&state, null, 0, file, 1, 0);
    if (state.error)
    {
        return state.error;
    }
    parseScript(&state);
    ParseStateDispose(&state);
    return state.error;
}

ErrorCode ParseFunction(targetref target, bytevector *bytecode)
{
    ParseState state;
    assert(target);
    TargetIndexSetBytecodeOffset(target, ByteVectorSize(bytecode));
    ParseStateInit(&state, bytecode, target,
                   TargetIndexGetFile(target),
                   TargetIndexGetLine(target),
                   TargetIndexGetFileOffset(target));
    if (state.error)
    {
        return state.error;
    }
    parseFunctionBody(&state);
    ParseStateDispose(&state);
    return state.error;
}
