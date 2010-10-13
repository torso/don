#include <memory.h>
#include <stdio.h>
#include "common.h"
#include "bytevector.h"
#include "fieldindex.h"
#include "file.h"
#include "functionindex.h"
#include "instruction.h"
#include "inthashmap.h"
#include "intvector.h"
#include "log.h"
#include "namespace.h"
#include "native.h"
#include "parser.h"
#include "parsestate.h"
#include "stringpool.h"
#include "util.h"

typedef enum
{
    VALUE_SIMPLE,
    VALUE_NONNUMBER,
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
    boolean constant;
} ExpressionState;

static char errorBuffer[256];

static stringref keywordElse;
static stringref keywordFalse;
static stringref keywordFor;
static stringref keywordIf;
static stringref keywordIn;
static stringref keywordNull;
static stringref keywordPipe;
static stringref keywordReturn;
static stringref keywordTrue;
static stringref keywordWhile;

static stringref maxStatementKeyword;
static stringref maxKeyword;

static boolean parseExpression(ParseState *state, ExpressionState *estate);
static boolean parseRValue(ParseState *state, boolean constant);

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

static boolean isFilenameCharacter(byte c)
{
    return (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '/' || c == '.' || c == '*';
}

static void error(ParseState *state, const char *message)
{
    ParseStateSetError(state, ERROR_FAIL);
    LogParseError(state->file, state->line, message);
}

static void errorOnLine(ParseState *state, size_t line, const char *message)
{
    ParseStateSetError(state, ERROR_FAIL);
    LogParseError(state->file, line, message);
}

static void statementError(ParseState *state, const char *message)
{
    ParseStateSetError(state, ERROR_FAIL);
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
    return state->current == state->limit;
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

static boolean isKeyword(stringref identifier)
{
    return identifier <= maxKeyword;
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
        return 0;
    }
    return identifier;
}

static stringref peekReadIdentifier(ParseState *state)
{
    if (peekIdentifier(state))
    {
        return readIdentifier(state);
    }
    return 0;
}

static stringref readVariableName(ParseState *state)
{
    stringref identifier = peekReadIdentifier(state);
    if (!identifier || isKeyword(identifier))
    {
        if (!state->error)
        {
            error(state, "Expected variable name.");
        }
        return 0;
    }
    return identifier;
}

static boolean readExpectedKeyword(ParseState *state, stringref keyword)
{
    stringref identifier = peekReadIdentifier(state);
    if (identifier == keyword)
    {
        return true;
    }
    if (!state->error)
    {
        sprintf(errorBuffer, "Expected keyword %s.", StringPoolGetString(keyword));
        statementError(state, errorBuffer);
    }
    return false;
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
    bytevector string;
    boolean copied = false;
    const byte *begin;
    stringref s;

    ParseStateCheck(state);
    assert(peekString(state));
    begin = ++state->current;
    for (;;)
    {
        assert(!eof(state)); /* TODO: error handling */
        switch (state->current[0])
        {
        case '\"':
            if (copied)
            {
                ByteVectorAddData(&string, begin, getOffset(state, begin));
                s = StringPoolAdd2(
                    (const char*)ByteVectorGetPointer(&string, 0),
                    ByteVectorSize(&string));
                ByteVectorDispose(&string);
            }
            else
            {
                s = StringPoolAdd2((const char*)begin, getOffset(state, begin));
            }
            if (!s)
            {
                ParseStateSetError(state, OUT_OF_MEMORY);
                return 0;
            }
            state->current++;
            return s;

        case  '\\':
            if (!copied)
            {
                ByteVectorInit(&string, 128);
                copied = true;
            }
            ByteVectorAddData(&string, begin, getOffset(state, begin));
            state->current++;
            switch (state->current[0])
            {
            case 'n':
                ByteVectorAdd(&string, '\n');
                break;

            default:
                error(state, "Invalid escape sequence.");
                break;
            }
            state->current++;
            begin = state->current;
            break;

        case '\r':
        case '\n':
            error(state, "Newline in string literal.");
            if (copied)
            {
                ByteVectorDispose(&string);
            }
            return 0;

        default:
            state->current++;
            break;
        }
    }
}

static stringref readFilename(ParseState *state)
{
    const byte *begin;
    stringref s;

    /* TODO: Quoted filenames. */
    /* TODO: Escape sequences in filenames. */
    ParseStateCheck(state);
    begin = state->current;
    while (isFilenameCharacter(state->current[0]))
    {
        assert(!eof(state)); /* TODO: error handling */
        assert(!peekNewline(state)); /* TODO: error handling */
        state->current++;
    }
    if (begin == state->current)
    {
        error(state, "Expected filename.");
        return 0;
    }
    s = StringPoolAdd2((const char*)begin, getOffset(state, begin));
    if (!s)
    {
        ParseStateSetError(state, OUT_OF_MEMORY);
        return 0;
    }
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

static boolean peekOperator(ParseState *state, byte op)
{
    return state->current[0] == op;
}

static boolean reverseIfOperator(ParseState *state, byte op)
{
    if (peekOperator(state, op))
    {
        state->current--;
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

static boolean peekOperator2(ParseState *state, byte op1, byte op2)
{
    if (state->current[0] == op1 && state->current[1] == op2)
    {
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

    if (isIdentifierCharacter(state->current[0]))
    {
        error(state, "Invalid character in number literal.");
        return false;
    }

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
        if (!parseRValue(state, false))
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
    fieldref field;
    switch (estate->valueType)
    {
    case VALUE_SIMPLE:
    case VALUE_NONNUMBER:
    case VALUE_INVOCATION:
        statementError(state, "Invalid target for assignment.");
        return false;

    case VALUE_VARIABLE:
        field = NamespaceGetField(estate->valueIdentifier);
        return (boolean)(field ?
                         ParseStateSetField(state, field) :
                         ParseStateSetVariable(state, estate->valueIdentifier));
    }
    assert(false);
    return false;
}

static boolean finishRValue(ParseState *state, ExpressionState *estate)
{
    fieldref field;
    switch (estate->valueType)
    {
    case VALUE_SIMPLE:
    case VALUE_NONNUMBER:
        return true;

    case VALUE_VARIABLE:
        field = NamespaceGetField(estate->valueIdentifier);
        return (boolean)(field ?
                         ParseStateGetField(state, field) :
                         ParseStateGetVariable(state, estate->valueIdentifier));

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
    case VALUE_NONNUMBER:
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
    ExpressionState estateArgument;
    nativefunctionref nativeFunction = NativeFindFunction(name);
    functionref function = 0;
    uint parameterCount;
    const ParameterInfo *parameterInfo;
    uint minimumArgumentCount;
    uint argumentCount = 0;
    uint line = state->line;
    boolean requireNamedParameters = false;
    intvector namedParameters;
    uint position;
    uint firstOutOfOrder;
    boolean inOrder;
    uint i;

    ParseStateCheck(state);
    if (nativeFunction)
    {
        parameterCount = NativeGetParameterCount(nativeFunction);
        parameterInfo = NativeGetParameterInfo(nativeFunction);
        minimumArgumentCount = NativeGetMinimumArgumentCount(nativeFunction);
    }
    else
    {
        function = NamespaceGetFunction(name);
        if (!function)
        {
            sprintf(errorBuffer, "Unknown function '%s'.",
                    StringPoolGetString(name));
            statementError(state, errorBuffer);
            return false;
        }
        parameterCount = FunctionIndexGetParameterCount(function);
        parameterInfo = FunctionIndexGetParameterInfo(function);
        minimumArgumentCount = FunctionIndexGetMinimumArgumentCount(function);
    }
    assert(parameterInfo || !parameterCount);

    if (!readOperator(state, ')'))
    {
        for (;;)
        {
            if (requireNamedParameters)
            {
                if (!peekIdentifier(state))
                {
                    IntVectorDispose(&namedParameters);
                    error(state, "Expected parameter name.");
                    return false;
                }
                estateArgument.identifier = readIdentifier(state);
                if (state->error || !readExpectedOperator(state, ':'))
                {
                    IntVectorDispose(&namedParameters);
                    return false;
                }
            }
            else
            {
                estateArgument.identifier = peekReadIdentifier(state);
                if (state->error)
                {
                    return false;
                }
                if (estateArgument.identifier && readOperator(state, ':'))
                {
                    requireNamedParameters = true;
                    state->error = IntVectorInit(&namedParameters);
                    if (state->error)
                    {
                        return false;
                    }
                    for (i = 0; i++ < argumentCount;)
                    {
                        state->error = IntVectorAdd(&namedParameters, i);
                        if (state->error)
                        {
                            IntVectorDispose(&namedParameters);
                            return false;
                        }
                    }
                    state->error = IntVectorGrowZero(
                        &namedParameters, parameterCount - argumentCount);
                    if (state->error)
                    {
                        IntVectorDispose(&namedParameters);
                        return false;
                    }
                }
            }
            if (requireNamedParameters)
            {
                skipWhitespace(state);
                for (i = 0;; i++)
                {
                    if (i == parameterCount)
                    {
                        IntVectorDispose(&namedParameters);
                        sprintf(errorBuffer, "Invalid parameter name '%s'.",
                                StringPoolGetString(estateArgument.identifier));
                        error(state, errorBuffer);
                        return false;
                    }
                    if (parameterInfo[i].name == estateArgument.identifier)
                    {
                        if (IntVectorGet(&namedParameters, i))
                        {
                            IntVectorDispose(&namedParameters);
                            sprintf(errorBuffer,
                                    "More than one value for parameter '%s'.",
                                    StringPoolGetString(estateArgument.identifier));
                            error(state, errorBuffer);
                            return false;
                        }
                        IntVectorSet(&namedParameters, i, argumentCount + 1);
                        estateArgument.identifier = 0;
                        break;
                    }
                }
            }
            estateArgument.constant = false;
            if (!parseExpression(state, &estateArgument) ||
                !finishRValue(state, &estateArgument))
            {
                if (requireNamedParameters)
                {
                    IntVectorDispose(&namedParameters);
                }
                return false;
            }
            argumentCount++;
            if (readOperator(state, ')'))
            {
                break;
            }
            if (!readExpectedOperator(state, ','))
            {
                if (requireNamedParameters)
                {
                    IntVectorDispose(&namedParameters);
                }
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
        if (requireNamedParameters)
        {
            IntVectorDispose(&namedParameters);
        }
        return false;
    }
    if (requireNamedParameters)
    {
        inOrder = true;
        for (i = 0; i < parameterCount; i++)
        {
            position = IntVectorGet(&namedParameters, i);
            if (!position)
            {
                if (!parameterInfo[i].value)
                {
                    sprintf(errorBuffer, "No value for parameter '%s' given.",
                            StringPoolGetString(parameterInfo[i].name));
                    errorOnLine(state, line, errorBuffer);
                    return false;
                }
                if (!ParseStateGetField(state, parameterInfo[i].value))
                {
                    return false;
                }
                position = ++argumentCount;
                IntVectorSet(&namedParameters, i, position);
            }
            if (position - 1 != i || !inOrder)
            {
                if (inOrder)
                {
                    firstOutOfOrder = i;
                    inOrder = false;
                }
            }
        }
        if (!inOrder && !ParseStateReorderStack(
                state, &namedParameters, firstOutOfOrder,
                parameterCount - firstOutOfOrder))
        {
            IntVectorDispose(&namedParameters);
            return false;
        }
        IntVectorDispose(&namedParameters);
        argumentCount = parameterCount;
    }
    else if (argumentCount < minimumArgumentCount)
    {
        sprintf(errorBuffer, "No value for parameter '%s' given.",
                StringPoolGetString(parameterInfo[argumentCount].name));
        errorOnLine(state, line, errorBuffer);
        return false;
    }
    else
    {
        while (argumentCount < parameterCount)
        {
            assert(parameterInfo[argumentCount].value);
            if (!ParseStateGetField(state, parameterInfo[argumentCount].value))
            {
                return false;
            }
            argumentCount++;
        }
    }
    estate->valueType = VALUE_INVOCATION;
    estate->nativeFunction = nativeFunction;
    estate->function = function;
    estate->argumentCount = argumentCount;
    return true;
}

static boolean parseBinaryOperationRest(
    ParseState *state, ExpressionState *estate,
    boolean (*parseExpressionRest)(ParseState*, ExpressionState*),
    Instruction instruction)
{
    skipWhitespace(state);
    if (!finishRValue(state, estate) ||
        !parseExpressionRest(state, estate) ||
        !finishRValue(state, estate) ||
        !ParseStateWriteInstruction(state, instruction))
    {
        return false;
    }
    estate->valueType = VALUE_SIMPLE;
    skipWhitespace(state);
    return true;
}

static boolean parseExpression12(ParseState *state, ExpressionState *estate)
{
    stringref identifier = estate->identifier;
    stringref string;
    uint size;

    ParseStateCheck(state);
    estate->valueType = VALUE_NONNUMBER;
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
        estate->valueType = VALUE_SIMPLE;
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
        if (estate->constant)
        {
            statementError(state, "Expected constant.");
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
    if (peekNumber(state))
    {
        estate->valueType = VALUE_SIMPLE;
        return parseNumber(state);
    }
    if (peekString(state))
    {
        string = readString(state);
        if (state->error)
        {
            return false;
        }
        return ParseStateWriteStringLiteral(state, string);
    }
    if (readOperator(state, '('))
    {
        skipWhitespace(state);
        if (!parseExpression(state, estate) ||
            !finishRValue(state, estate))
        {
            return false;
        }
        if (estate->valueType != VALUE_NONNUMBER)
        {
            estate->valueType = VALUE_SIMPLE;
        }
        return readExpectedOperator(state, ')');
    }
    if (readOperator(state, '['))
    {
        skipWhitespace(state);
        if (readOperator(state, ']'))
        {
            skipWhitespace(state);
            return ParseStateWriteInstruction(state, OP_EMPTY_LIST);
        }
        size = 0;
        do
        {
            size++;
            skipWhitespace(state);
            if (!parseRValue(state, estate->constant))
            {
                return false;
            }
            skipWhitespace(state);
        }
        while (readOperator(state, ','));
        skipWhitespace(state);
        if (!readExpectedOperator(state, ']'))
        {
            return false;
        }
        skipWhitespace(state);
        return ParseStateWriteList(state, size);
    }
    if (readOperator(state, '@'))
    {
        string = readFilename(state);
        if (state->error)
        {
            return false;
        }
        if (!strchr(StringPoolGetString(string), '*'))
        {
            return ParseStateWriteFile(state, string);
        }
        /* TODO: @{} syntax */
        return ParseStateWriteFileset(state, string);
    }
    statementError(state, "Invalid expression.");
    return false;
}

static boolean parseExpression11(ParseState *state, ExpressionState *estate)
{
    if (!parseExpression12(state, estate))
    {
        return false;
    }
    for (;;)
    {
        if (readOperator(state, '['))
        {
            skipWhitespace(state);
            if (!finishRValue(state, estate) ||
                !parseRValue(state, estate->constant))
            {
                return false;
            }
            skipWhitespace(state);
            if (!readExpectedOperator(state, ']') ||
                !ParseStateWriteInstruction(state, OP_INDEXED_ACCESS))
            {
                return false;
            }
            estate->valueType = VALUE_SIMPLE;
            continue;
        }
        if (!peekOperator2(state, '.', '.') &&
            readOperator(state, '.'))
        {
            assert(false); /* TODO: handle namespace */
        }
        break;
    }
    skipWhitespace(state);
    return true;
}

static boolean parseExpression10(ParseState *state, ExpressionState *estate)
{
    if (readOperator(state, '-'))
    {
        assert(!readOperator(state, '-')); /* TODO: -- operator */
        if (!parseExpression11(state, estate) ||
            !finishRValue(state, estate) ||
            !ParseStateWriteInstruction(state, OP_NEG))
        {
            return false;
        }
        skipWhitespace(state);
        estate->valueType = VALUE_SIMPLE;
        return true;
    }
    if (readOperator(state, '!'))
    {
        if (!parseExpression11(state, estate) ||
            !finishRValue(state, estate) ||
            !ParseStateWriteInstruction(state, OP_NOT))
        {
            return false;
        }
        skipWhitespace(state);
        estate->valueType = VALUE_SIMPLE;
        return true;
    }
    if (readOperator(state, '~'))
    {
        if (!parseExpression11(state, estate) ||
            !finishRValue(state, estate) ||
            !ParseStateWriteInstruction(state, OP_INV))
        {
            return false;
        }
        skipWhitespace(state);
        estate->valueType = VALUE_SIMPLE;
        return true;
    }
    if (!parseExpression11(state, estate))
    {
        return false;
    }
    skipWhitespace(state);
    return true;
}

static boolean parseExpression9(ParseState *state, ExpressionState *estate)
{
    if (!parseExpression10(state, estate))
    {
        return false;
    }
    for (;;)
    {
        if (readOperator(state, '*'))
        {
            if (reverseIfOperator(state, '='))
            {
                return true;
            }
            if (!parseBinaryOperationRest(state, estate,
                                          parseExpression10, OP_MUL))
            {
                return false;
            }
            continue;
        }
        if (readOperator(state, '/'))
        {
            if (reverseIfOperator(state, '='))
            {
                return true;
            }
            if (!parseBinaryOperationRest(state, estate,
                                          parseExpression10, OP_DIV))
            {
                return false;
            }
            continue;
        }
        if (readOperator(state, '%'))
        {
            if (reverseIfOperator(state, '='))
            {
                return true;
            }
            if (!parseBinaryOperationRest(state, estate,
                                          parseExpression10, OP_REM))
            {
                return false;
            }
            continue;
        }
        break;
    }
    return true;
}

static boolean parseExpression8(ParseState *state, ExpressionState *estate)
{
    if (!parseExpression9(state, estate))
    {
        return false;
    }
    for (;;)
    {
        if (readOperator(state, '+'))
        {
            if (reverseIfOperator(state, '='))
            {
                return true;
            }
            assert(!readOperator(state, '+')); /* TODO: ++ operator */
            if (!parseBinaryOperationRest(state, estate,
                                          parseExpression9, OP_ADD))
            {
                return false;
            }
            continue;
        }
        else if (readOperator(state, '-'))
        {
            if (peekOperator(state, '=') ||
                (estate->valueType == VALUE_NONNUMBER &&
                 state->current[0] != ' '))
            {
                state->current--;
                return true;
            }
            assert(!readOperator(state, '-')); /* TODO: -- operator */
            if (!parseBinaryOperationRest(state, estate,
                                          parseExpression9, OP_SUB))
            {
                return false;
            }
            continue;
        }
        break;
    }
    return true;
}

static boolean parseExpression7(ParseState *state, ExpressionState *estate)
{
    if (!parseExpression8(state, estate))
    {
        return false;
    }
    for (;;)
    {
        /* TODO: Parse operators << >> */
        break;
    }
    return true;
}

static boolean parseExpression6(ParseState *state, ExpressionState *estate)
{
    if (!parseExpression7(state, estate))
    {
        return false;
    }
    for (;;)
    {
        /* TODO: Parse operators & | ^ */
        break;
    }
    return true;
}

static boolean parseExpression5(ParseState *state, ExpressionState *estate)
{
    if (!parseExpression6(state, estate))
    {
        return false;
    }
    for (;;)
    {
        if (readOperator2(state, '.', '.'))
        {
            if (!parseBinaryOperationRest(state, estate,
                                          parseExpression6, OP_RANGE))
            {
                return false;
            }
            continue;
        }
        break;
    }
    return true;
}

static boolean parseExpression4(ParseState *state, ExpressionState *estate)
{
    if (!parseExpression5(state, estate))
    {
        return false;
    }
    for (;;)
    {
        switch (state->current[0])
        {
        case ',':
        case ':':
        case ')':
        case ']':
        case '=':
        case '<':
        case '>':
        case '&':
        case '|':
        case '?':
            return true;
        }
        if (peekNewline(state) ||
            peekOperator2(state, '!', '=') ||
            peekOperator2(state, '+', '=') ||
            peekOperator2(state, '-', '=') ||
            peekOperator2(state, '*', '=') ||
            peekOperator2(state, '/', '=') ||
            peekOperator2(state, '%', '='))
        {
            return true;;
        }
        if (!finishRValue(state, estate) ||
            !parseExpression5(state, estate) ||
            !finishRValue(state, estate) ||
            !ParseStateWriteInstruction(state, OP_CONCAT))
        {
            return false;
        }
        estate->valueType = VALUE_NONNUMBER;
        skipWhitespace(state);
    }
}

static boolean parseExpression3(ParseState *state, ExpressionState *estate)
{
    if (!parseExpression4(state, estate))
    {
        return false;
    }
    for (;;)
    {
        if (readOperator2(state, '=', '='))
        {
            if (!parseBinaryOperationRest(state, estate,
                                          parseExpression4, OP_EQUALS))
            {
                return false;
            }
            continue;
        }
        if (readOperator2(state, '!', '='))
        {
            if (!parseBinaryOperationRest(state, estate,
                                          parseExpression4, OP_NOT_EQUALS))
            {
                return false;
            }
            continue;
        }
        if (readOperator2(state, '<', '='))
        {
            if (!parseBinaryOperationRest(state, estate,
                                          parseExpression4, OP_LESS_EQUALS))
            {
                return false;
            }
            continue;
        }
        if (readOperator2(state, '>', '='))
        {
            if (!parseBinaryOperationRest(state, estate,
                                          parseExpression4, OP_GREATER_EQUALS))
            {
                return false;
            }
            continue;
        }
        if (readOperator(state, '<'))
        {
            if (!parseBinaryOperationRest(state, estate,
                                          parseExpression4, OP_LESS))
            {
                return false;
            }
            continue;
        }
        if (readOperator(state, '>'))
        {
            if (!parseBinaryOperationRest(state, estate,
                                          parseExpression4, OP_GREATER))
            {
                return false;
            }
            continue;
        }
        break;
    }
    return true;
}

static boolean parseExpression2(ParseState *state, ExpressionState *estate)
{
    size_t branch;

    if (!parseExpression3(state, estate))
    {
        return false;
    }
    for (;;)
    {
        if (readOperator2(state, '&', '&'))
        {
            skipWhitespace(state);
            if (!finishRValue(state, estate) ||
                !ParseStateWriteInstruction(state, OP_CAST_BOOLEAN) ||
                !ParseStateWriteInstruction(state, OP_DUP) ||
                !ParseStateBeginForwardJump(state, OP_BRANCH_FALSE, &branch) ||
                !ParseStateWriteInstruction(state, OP_POP) ||
                !parseExpression3(state, estate) ||
                !finishRValue(state, estate) ||
                !ParseStateWriteInstruction(state, OP_CAST_BOOLEAN) ||
                !ParseStateFinishJump(state, branch))
            {
                return false;
            }
            estate->valueType = VALUE_SIMPLE;
            skipWhitespace(state);
            continue;
        }
        if (readOperator2(state, '|', '|'))
        {
            skipWhitespace(state);
            if (!finishRValue(state, estate) ||
                !ParseStateWriteInstruction(state, OP_CAST_BOOLEAN) ||
                !ParseStateWriteInstruction(state, OP_DUP) ||
                !ParseStateBeginForwardJump(state, OP_BRANCH_TRUE, &branch) ||
                !ParseStateWriteInstruction(state, OP_POP) ||
                !parseExpression3(state, estate) ||
                !finishRValue(state, estate) ||
                !ParseStateWriteInstruction(state, OP_CAST_BOOLEAN) ||
                !ParseStateFinishJump(state, branch))
            {
                return false;
            }
            estate->valueType = VALUE_SIMPLE;
            skipWhitespace(state);
            continue;
        }
        break;
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
            !parseRValue(state, estate->constant) ||
            !readExpectedOperator(state, ':') ||
            !ParseStateWriteSecondConsequent(state))
        {
            return false;
        }
        skipWhitespace(state);
        if (!parseRValue(state, estate->constant) ||
            !ParseStateWriteFinishCondition(state))
        {
            return false;
        }
        estate->valueType = VALUE_SIMPLE;
        return true;
    }
    return true;
}

static boolean parseRValue(ParseState *state, boolean constant)
{
    ExpressionState estate;

    estate.identifier = 0;
    estate.constant = constant;
    return parseExpression(state, &estate) &&
        finishRValue(state, &estate);
}

static boolean parseAssignmentExpressionRest(ParseState *state,
                                             ExpressionState *estate,
                                             Instruction instruction)
{
    skipWhitespace(state);
    if (!finishRValue(state, estate) ||
        !parseRValue(state, false) ||
        !ParseStateWriteInstruction(state, instruction) ||
        !finishLValue(state, estate))
    {
        return false;
    }
    return true;
}

static boolean parseMultiAssignmentRest(ParseState *state)
{
    ExpressionState estate;
    bytevector lvalues;

    ByteVectorInit(&lvalues, 16);
    do
    {
        skipWhitespace(state);
        estate.identifier = 0;
        estate.constant = false;
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
    estate.constant = false;
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
    estate.constant = false;
    if (!parseExpression(state, &estate))
    {
        return false;
    }
    if (readOperator(state, '='))
    {
        skipWhitespace(state);
        return parseRValue(state, false) && finishLValue(state, &estate);
    }
    else if (readOperator2(state, '+', '='))
    {
        return parseAssignmentExpressionRest(state, &estate, OP_ADD);
    }
    else if (readOperator2(state, '-', '='))
    {
        return parseAssignmentExpressionRest(state, &estate, OP_SUB);
    }
    else if (readOperator2(state, '*', '='))
    {
        return parseAssignmentExpressionRest(state, &estate, OP_MUL);
    }
    else if (readOperator2(state, '/', '='))
    {
        return parseAssignmentExpressionRest(state, &estate, OP_DIV);
    }
    else if (readOperator2(state, '%', '='))
    {
        return parseAssignmentExpressionRest(state, &estate, OP_REM);
    }
    else if (readOperator(state, ','))
    {
        return parseMultiAssignmentRest(state) && finishLValue(state, &estate);
    }
    return finishVoidValue(state, &estate);
}

static boolean parseFunctionBody(ParseState *state)
{
    uint indent;
    uint currentIndent = 0;
    uint prevIndent = 0;
    stringref identifier;
    stringref identifier2;
    size_t target;
    uint16 iterVariable;

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
                        if (!parseRValue(state, false) ||
                            !ParseStateWriteInstruction(state, OP_CAST_BOOLEAN))
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
                    else if (identifier == keywordFor)
                    {
                        prevIndent = currentIndent;
                        currentIndent = 0;
                        identifier = readVariableName(state);
                        if (state->error)
                        {
                            return false;
                        }
                        iterVariable = ParseStateCreateUnnamedVariable(state);
                        skipWhitespace(state);
                        if (state->error ||
                            !readExpectedKeyword(state, keywordIn))
                        {
                            return false;
                        }
                        skipWhitespace(state);
                        if (!parseRValue(state, false) ||
                            !ParseStateWriteInstruction(state, OP_ITER_INIT) ||
                            !ParseStateSetUnnamedVariable(state, iterVariable))
                        {
                            return false;
                        }
                        if (!peekNewline(state))
                        {
                            error(state, "Garbage after for statement.");
                            return false;
                        }
                        skipEndOfLine(state);
                        target = ParseStateGetJumpTarget(state);
                        if (!ParseStateGetUnnamedVariable(state, iterVariable) ||
                            !ParseStateWriteInstruction(state, OP_ITER_NEXT) ||
                            !ParseStateSetVariable(state, identifier) ||
                            !ParseStateWriteWhile(state, target))
                        {
                            return false;
                        }
                    }
                    else if (identifier == keywordPipe)
                    {
                        prevIndent = currentIndent;
                        currentIndent = 0;
                        identifier = readVariableName(state);
                        if (state->error ||
                            !readExpectedOperator(state, ','))
                        {
                            return false;
                        }
                        skipWhitespace(state);
                        identifier2 = readVariableName(state);
                        if (state->error ||
                            !ParseStateWritePipe(state,
                                                 identifier, identifier2))
                        {
                            return false;
                        }
                        if (!peekNewline(state))
                        {
                            error(state, "Garbage after pipe statement.");
                            return false;
                        }
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
                        if (!parseRValue(state, false) ||
                            !ParseStateWriteInstruction(state, OP_CAST_BOOLEAN))
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

static void parseFunctionDeclaration(ParseState *state, functionref function)
{
    stringref parameterName;
    fieldref field = 0;
    boolean requireDefaultValues = false;
    uint start;

    if (readOperator(state, ':'))
    {
        if (!peekNewline(state))
        {
            error(state, "Garbage after target declaration.");
            return;
        }
    }
    else
    {
        assert(peekOperator(state, '('));
        state->current++;
        if (!readOperator(state, ')'))
        {
            for (;;)
            {
                parameterName = peekReadIdentifier(state);
                if (state->error)
                {
                    return;
                }
                if (!parameterName || isKeyword(parameterName))
                {
                    error(state, "Expected parameter name or ')'.");
                    return;
                }
                if (NamespaceGetField(parameterName))
                {
                    sprintf(errorBuffer,
                            "Invalid parameter name. '%s' is a global variable.",
                            StringPoolGetString(parameterName));
                    error(state, errorBuffer);
                    return;
                }
                if (NamespaceGetFunction(parameterName))
                {
                    sprintf(errorBuffer,
                            "Invalid parameter name. '%s' is a function.",
                            StringPoolGetString(parameterName));
                    error(state, errorBuffer);
                    return;
                }
                skipWhitespace(state);
                if (readOperator(state, '='))
                {
                    requireDefaultValues = true;
                    skipWhitespace(state);
                    field = FieldIndexAdd(state->file, state->line,
                                          getOffset(state, state->start));
                    if (!field)
                    {
                        state->error = OUT_OF_MEMORY;
                        return;
                    }
                    start = (uint)ByteVectorSize(state->bytecode);
                    if (!parseRValue(state, true))
                    {
                        return;
                    }
                    FieldIndexSetBytecodeOffset(
                        field, start, (uint)ByteVectorSize(state->bytecode));
                }
                else if (requireDefaultValues)
                {
                    sprintf(errorBuffer,
                            "Default value for parameter '%s' required.",
                            StringPoolGetString(parameterName));
                    error(state, errorBuffer);
                    return;
                }
                state->error = FunctionIndexAddParameter(function, parameterName,
                                                         field, field == 0);
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
        if (!peekNewline(state))
        {
            error(state, "Garbage after function declaration.");
            return;
        }
    }
    skipEndOfLine(state);
    FunctionIndexFinishParameters(function, state->line,
                                  getOffset(state, state->start));
}

static void parseScript(ParseState *state)
{
    functionref function;
    fieldref field;
    stringref name;
    boolean allowIndent = false;

    ParseStateCheck(state);
    while (!eof(state))
    {
        if (peekIdentifier(state))
        {
            state->statementLine = state->line;
            name = readIdentifier(state);
            if (state->error)
            {
                return;
            }
            if (peekOperator(state, ':'))
            {
                function = FunctionIndexAddFunction(
                    name, state->file, state->line,
                    getOffset(state, state->start));
                if (!function)
                {
                    state->error = OUT_OF_MEMORY;
                    return;
                }
                state->error = NamespaceAddTarget(name, function);
                if (state->error)
                {
                    return;
                }
                skipEndOfLine(state);
                allowIndent = true;
            }
            else if (peekOperator(state, '('))
            {
                function = FunctionIndexAddFunction(
                    name, state->file, state->line,
                    getOffset(state, state->start));
                if (!function)
                {
                    state->error = OUT_OF_MEMORY;
                    return;
                }
                state->error = NamespaceAddFunction(name, function);
                if (state->error)
                {
                    return;
                }
                skipEndOfLine(state);
                allowIndent = true;
            }
            else
            {
                skipWhitespace(state);
                if (!readOperator(state, '='))
                {
                    error(state, "Invalid field declaration.");
                    return;
                }
                skipWhitespace(state);
                field = FieldIndexAdd(state->file, state->line,
                                      getOffset(state, state->start));
                if (!field)
                {
                    state->error = OUT_OF_MEMORY;
                    return;
                }
                state->error = NamespaceAddField(name, field);
                if (state->error)
                {
                    return;
                }
                skipEndOfLine(state);
                allowIndent = false;
            }
        }
        else if ((peekIndent(state) && allowIndent) ||
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
        !(keywordFor = StringPoolAdd("for")) ||
        !(keywordIf = StringPoolAdd("if")) ||
        !(keywordIn = StringPoolAdd("in")) ||
        !(keywordNull = StringPoolAdd("null")) ||
        !(keywordPipe = StringPoolAdd("pipe")) ||
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
    if (state.current != state.limit && state.limit[-1] != '\n')
    {
        /* TODO: Provide fallback */
        errorOnLine(&state,
                    UtilCountNewlines((const char*)state.start,
                                      (size_t)(state.limit - state.start)) + 1,
                    "File does not end with newline.");
        ParseStateDispose(&state);
        return ERROR_FAIL;
    }
    parseScript(&state);
    ParseStateDispose(&state);
    return state.error;
}

ErrorCode ParseField(fieldref field, bytevector *bytecode)
{
    ParseState state;
    uint start = (uint)ByteVectorSize(bytecode);

    assert(field);
    ParseStateInit(&state, bytecode, 0, FieldIndexGetFile(field),
                   FieldIndexGetLine(field), FieldIndexGetFileOffset(field));
    if (state.error)
    {
        return state.error;
    }

    state.statementLine = state.line;
    if (parseRValue(&state, true))
    {
        if (!peekNewline(&state))
        {
            statementError(&state, "Garbage after variable declaration.");
        }
        else
        {
            /* TODO: Look for code before next field/function. */
            FieldIndexSetBytecodeOffset(field, start, (uint)ByteVectorSize(bytecode));
        }
    }

    ParseStateDispose(&state);
    return state.error;
}

ErrorCode ParseFunctionDeclaration(functionref function, bytevector *bytecode)
{
    ParseState state;

    assert(function);
    ParseStateInit(&state, bytecode, function,
                   FunctionIndexGetFile(function),
                   FunctionIndexGetLine(function),
                   FunctionIndexGetFileOffset(function));
    if (state.error)
    {
        return state.error;
    }
    parseFunctionDeclaration(&state, function);
    ParseStateDispose(&state);
    return state.error;
}

ErrorCode ParseFunctionBody(functionref function, bytevector *bytecode)
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
