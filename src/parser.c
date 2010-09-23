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
#include "functionindex.h"

typedef enum
{
    VALUE_SIMPLE,
    VALUE_VARIABLE,
    VALUE_INVOCATION
} ValueType;

typedef struct
{
    stringref identifier;
    ValueType valueType;
    stringref valueIdentifier;
    nativefunctionref nativeFunction;
    functionref function;
    uint argumentCount;
} ExpressionState;

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

static boolean parseRValue(ParseState *state);

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

static boolean readOperator2(ParseState *state, byte op1, byte op2)
{
    if (state->current[0] == op1 && state->current[1] == op2)
    {
        state->current += 2;
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

static boolean parseReturnRest(ParseState *state)
{
    uint values = 0;

    if (peekNewline(state))
    {
        return ParseStateWriteReturnVoid(state);
    }
    for (;;)
    {
        if (!parseRValue(state))
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

static boolean finishLValue(ParseState *state, ExpressionState *estate)
{
    switch (estate->valueType)
    {
    case VALUE_SIMPLE:
    case VALUE_INVOCATION:
        statementError(state, "Invalid target for assignment.");
        return false;

    case VALUE_VARIABLE:
        return ParseStateSetVariable(state, estate->valueIdentifier);
    }
    assert(false);
    return false;
}

static boolean finishRValue(ParseState *state, ExpressionState *estate)
{
    switch (estate->valueType)
    {
    case VALUE_SIMPLE:
        return true;

    case VALUE_VARIABLE:
        return ParseStateGetVariable(state, estate->valueIdentifier);

    case VALUE_INVOCATION:
        return ParseStateWriteInvocation(state, estate->nativeFunction,
                                         estate->function,
                                         estate->argumentCount, 1);
    }
    assert(false);
    return false;
}

static boolean finishVoidValue(ParseState *state, ExpressionState *estate)
{
    switch (estate->valueType)
    {
    case VALUE_SIMPLE:
    case VALUE_VARIABLE:
        statementError(state, "Not a statement.");
        return false;

    case VALUE_INVOCATION:
        return ParseStateWriteInvocation(state, estate->nativeFunction,
                                         estate->function,
                                         estate->argumentCount, 0);
    }
    assert(false);
    return false;
}

static boolean parseInvocationRest(ParseState *state, ExpressionState *estate,
                                   stringref name)
{
    nativefunctionref nativeFunction = NativeFindFunction(name);
    functionref function = 0;
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
        function = FunctionIndexGet(name);
        if (!function)
        {
            sprintf(errorBuffer, "Unknown function '%s'.",
                    StringPoolGetString(name));
            statementError(state, errorBuffer);
            return false;
        }
        parameterCount = FunctionIndexGetParameterCount(function);
        parameterNames = FunctionIndexGetParameterNames(function);
        minimumArgumentCount = FunctionIndexGetMinimumArgumentCount(function);
    }
    assert(parameterNames || !parameterCount);

    if (!readOperator(state, ')'))
    {
        for (;;)
        {
            if (!parseRValue(state))
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
        return false;
    }
    if (argumentCount < minimumArgumentCount)
    {
        sprintf(errorBuffer,
                "Too few arguments for function '%s'. Got %d arguments, but at least %d were expected.",
                StringPoolGetString(name), argumentCount, parameterCount);
        errorOnLine(state, line, errorBuffer);
        return false;
    }
    estate->valueType = VALUE_INVOCATION;
    estate->nativeFunction = nativeFunction;
    estate->function = function;
    estate->argumentCount = argumentCount;
    return true;
}

static boolean parseExpression6(ParseState *state, ExpressionState *estate)
{
    stringref identifier = estate->identifier;
    stringref string;

    ParseStateCheck(state);
    estate->valueType = VALUE_SIMPLE;
    estate->identifier = 0;
    if (!identifier && peekIdentifier(state))
    {
        identifier = readIdentifier(state);
        if (state->error)
        {
            return 0;
        }
    }
    if (identifier)
    {
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
            return false;
        }
        if (readOperator(state, '('))
        {
            return parseInvocationRest(state, estate, identifier);
        }
        estate->valueType = VALUE_VARIABLE;
        estate->valueIdentifier = identifier;
        return true;
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

static boolean parseExpression5(ParseState *state, ExpressionState *estate)
{
    if (!parseExpression6(state, estate))
    {
        return false;
    }
    if (readOperator(state, '.'))
    {
        assert(false); /* TODO: handle namespace */
    }
    skipWhitespace(state);
    return true;
}

static boolean parseExpression4(ParseState *state, ExpressionState *estate)
{
    if (!parseExpression5(state, estate))
    {
        return false;
    }
    if (readOperator(state, '+'))
    {
        assert(!readOperator(state, '+')); /* TODO: ++ operator */
        skipWhitespace(state);
        if (!finishRValue(state, estate) ||
            !parseExpression5(state, estate) ||
            !finishRValue(state, estate) ||
            !ParseStateWriteBinaryOperation(state, OP_ADD))
        {
            return false;
        }
        estate->valueType = VALUE_SIMPLE;
        return true;
    }
    else if (readOperator(state, '-'))
    {
        assert(!readOperator(state, '-')); /* TODO: -- operator */
        skipWhitespace(state);
        if (!finishRValue(state, estate) ||
            !parseExpression5(state, estate) ||
            !finishRValue(state, estate) ||
            !ParseStateWriteBinaryOperation(state, OP_SUB))
        {
            return false;
        }
        estate->valueType = VALUE_SIMPLE;
        return true;
    }
    return true;
}

static boolean parseExpression3(ParseState *state, ExpressionState *estate)
{
    if (!parseExpression4(state, estate))
    {
        return false;
    }
    if (readOperator(state, '.'))
    {
        skipWhitespace(state);
        if (!finishRValue(state, estate) ||
            !parseExpression4(state, estate) ||
            !finishRValue(state, estate) ||
            !ParseStateWriteBinaryOperation(state, OP_CONCAT))
        {
            return false;
        }
        estate->valueType = VALUE_SIMPLE;
        return true;
    }
    return true;
}

static boolean parseExpression2(ParseState *state, ExpressionState *estate)
{
    if (!parseExpression3(state, estate))
    {
        return false;
    }
    if (readOperator2(state, '=', '='))
    {
        skipWhitespace(state);
        if (!finishRValue(state, estate) ||
            !parseExpression3(state, estate) ||
            !finishRValue(state, estate) ||
            !ParseStateWriteBinaryOperation(state, OP_EQUALS))
        {
            return false;
        }
        estate->valueType = VALUE_SIMPLE;
        return true;
    }
    if (readOperator2(state, '!', '='))
    {
        skipWhitespace(state);
        if (!finishRValue(state, estate) ||
            !parseExpression3(state, estate) ||
            !finishRValue(state, estate) ||
            !ParseStateWriteBinaryOperation(state, OP_NOT_EQUALS))
        {
            return false;
        }
        estate->valueType = VALUE_SIMPLE;
        return true;
    }
    return true;
}

static boolean parseExpression(ParseState *state, ExpressionState *estate)
{
    if (!parseExpression2(state, estate))
    {
        return false;
    }
    if (readOperator(state, '?'))
    {
        skipWhitespace(state);
        /* TODO: Avoid recursion. */
        if (!finishRValue(state, estate) ||
            !ParseStateWriteBeginCondition(state) ||
            !parseRValue(state) ||
            !readExpectedOperator(state, ':') ||
            !ParseStateWriteSecondConsequent(state))
        {
            return false;
        }
        skipWhitespace(state);
        if (!parseRValue(state) ||
            !ParseStateWriteFinishCondition(state))
        {
            return false;
        }
        estate->valueType = VALUE_SIMPLE;
        return true;
    }
    return true;
}

static boolean parseRValue(ParseState *state)
{
    ExpressionState estate;

    estate.identifier = 0;
    return parseExpression(state, &estate) &&
        finishRValue(state, &estate);
}

static boolean parseMultiAssignmentRest(ParseState *state)
{
    ExpressionState estate;
    bytevector lvalues;

    ByteVectorInit(&lvalues);
    do
    {
        skipWhitespace(state);
        estate.identifier = 0;
        if (!parseExpression(state, &estate))
        {
            return false;
        }
        state->error = ByteVectorAddData(&lvalues, (byte*)&estate, sizeof(estate));
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
    estate.identifier = 0;
    parseExpression(state, &estate);
    if (estate.valueType != VALUE_INVOCATION)
    {
        statementError(state, "Expected function invocation.");
        return false;
    }
    if (!ParseStateWriteInvocation(
            state, estate.nativeFunction, estate.function, estate.argumentCount,
            (uint)(ByteVectorSize(&lvalues) / sizeof(estate) + 1)))
    {
        return false;
    }
    while (ByteVectorSize(&lvalues))
    {
        ByteVectorPopData(&lvalues, (byte*)&estate, sizeof(estate));
        if (!finishLValue(state, &estate))
        {
            return false;
        }
    }
    ByteVectorDispose(&lvalues);
    return true;
}

static boolean parseExpressionStatement(ParseState *state,
                                        stringref identifier)
{
    ExpressionState estate;

    estate.identifier = identifier;
    if (!parseExpression(state, &estate))
    {
        return false;
    }
    if (readOperator(state, '='))
    {
        skipWhitespace(state);
        if (!parseRValue(state) ||
            !finishLValue(state, &estate))
        {
            return false;
        }
        return true;
    }
    else if (readOperator(state, ','))
    {
        if (!parseMultiAssignmentRest(state) ||
            !finishLValue(state, &estate))
        {
            return false;
        }
        return true;
    }
    return finishVoidValue(state, &estate);
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
                if (isKeyword(identifier))
                {
                    if (identifier > maxStatementKeyword)
                    {
                        statementError(state, "Not a statement.");
                        return false;
                    }
                    skipWhitespace(state);
                    if (identifier == keywordIf)
                    {
                        prevIndent = currentIndent;
                        currentIndent = 0;
                        if (!parseRValue(state))
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
                        if (!parseRValue(state))
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
                    if (!parseExpressionStatement(state, identifier))
                    {
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
    stringref function;
    stringref parameterName;

    ParseStateCheck(state);
    while (!eof(state))
    {
        if (peekIdentifier(state))
        {
            function = readIdentifier(state);
            if (state->error)
            {
                return;
            }
            state->error = FunctionIndexBeginFunction(function);
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
                            FunctionIndexAddParameter(parameterName, true);
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
            FunctionIndexFinishFunction(state->file, state->line,
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

ErrorCode ParseFunction(functionref function, bytevector *bytecode)
{
    ParseState state;
    assert(function);
    FunctionIndexSetBytecodeOffset(function, (uint)ByteVectorSize(bytecode));
    ParseStateInit(&state, bytecode, function,
                   FunctionIndexGetFile(function),
                   FunctionIndexGetLine(function),
                   FunctionIndexGetFileOffset(function));
    if (state.error)
    {
        return state.error;
    }
    parseFunctionBody(&state);
    ParseStateDispose(&state);
    return state.error;
}
