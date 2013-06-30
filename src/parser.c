#include <memory.h>
#include <stdarg.h>
#include <stdio.h>
#include "common.h"
#include "bytevector.h"
#include "fieldindex.h"
#include "file.h"
#include "functionindex.h"
#include "heap.h"
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
    EXPRESSION_SIMPLE,
    EXPRESSION_VARIABLE,
    EXPRESSION_FIELD,
    EXPRESSION_CONSTANT,
    EXPRESSION_INVOCATION,
    EXPRESSION_NATIVE_INVOCATION
} ExpressionType;

typedef enum
{
    VALUE_UNKNOWN,
    VALUE_NULL,
    VALUE_BOOLEAN,
    VALUE_NUMBER,
    VALUE_LIST,
    VALUE_STRING,
    VALUE_FILE
} ValueType;

typedef struct
{
    vref identifier;
    ExpressionType expressionType;
    ValueType valueType;
    vref valueIdentifier;
    vref constant;
    fieldref field;
    nativefunctionref nativeFunction;
    functionref function;
    boolean parseConstant;
    boolean allowSpace;
} ExpressionState;

static vref keywordElse;
static vref keywordFalse;
static vref keywordFor;
static vref keywordIf;
static vref keywordIn;
static vref keywordNull;
static vref keywordReturn;
static vref keywordTrue;
static vref keywordWhile;

static vref maxStatementKeyword;
static vref maxKeyword;

static boolean parseExpression(ParseState *state, ExpressionState *estate,
                               boolean constant);
static boolean parseUnquotedExpression(ParseState *state,
                                       ExpressionState *estate,
                                       boolean constant);
static boolean parseRValue(ParseState *state, boolean constant);
static int parseBlock(ParseState *state, int parentIndent);

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
        memchr("/.*-+~_=!@#$%^&", c, 15);
}

static attrprintf(2, 3) void error(ParseState *state, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    LogParseError(state->filename, state->line, format, args);
    va_end(args);
}

static attrprintf(3, 4) void errorOnLine(ParseState *state, size_t line,
                                         const char *format, ...)
{
    va_list args;

    va_start(args, format);
    LogParseError(state->filename, line, format, args);
    va_end(args);
}

static attrprintf(2, 3) void statementError(ParseState *state,
                                            const char *format, ...)
{
    va_list args;

    va_start(args, format);
    LogParseError(state->filename, state->statementLine, format, args);
    va_end(args);
}

static uint getOffset(const ParseState *state, const byte *begin)
{
    ParseStateCheck(state);
    return (uint)(state->current - begin);
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

static void skipExpressionWhitespace(ParseState *state, ExpressionState *estate)
{
    if (estate->allowSpace)
    {
        skipWhitespace(state);
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

static int readIndent(ParseState *state)
{
    const byte *begin = state->current;

    ParseStateCheck(state);
    skipWhitespace(state);
    return (int)getOffset(state, begin);
}

static boolean peekComment(const ParseState *state)
{
    ParseStateCheck(state);
    return state->current[0] == ';';
}

static boolean readComment(ParseState *state)
{
    ParseStateCheck(state);
    if (state->current[0] == ';')
    {
        skipEndOfLine(state);
        return true;
    }
    return false;
}

static boolean isKeyword(vref identifier)
{
    return identifier <= maxKeyword;
}

static boolean peekIdentifier(const ParseState *state)
{
    ParseStateCheck(state);
    return isInitialIdentifierCharacter(state->current[0]);
}

static vref readIdentifier(ParseState *state)
{
    const byte *begin = state->current;

    ParseStateCheck(state);
    assert(peekIdentifier(state));
    while (isIdentifierCharacter(*++state->current));
    return StringPoolAdd2((const char*)begin, getOffset(state, begin));
}

static vref peekReadIdentifier(ParseState *state)
{
    if (peekIdentifier(state))
    {
        return readIdentifier(state);
    }
    return 0;
}

static vref readVariableName(ParseState *state)
{
    vref identifier = peekReadIdentifier(state);
    if (!identifier || isKeyword(identifier))
    {
        error(state, "Expected variable name.");
        return 0;
    }
    return identifier;
}

static boolean readExpectedKeyword(ParseState *state, vref keyword)
{
    vref identifier = peekReadIdentifier(state);
    if (identifier == keyword)
    {
        return true;
    }
    statementError(state, "Expected keyword %s.", HeapGetString(keyword));
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

static boolean skipWhitespaceAndNewline(ParseState *state)
{
    ParseStateCheck(state);
    skipWhitespace(state);
    while (peekNewline(state))
    {
        skipEndOfLine(state);
        if (readIndent(state) <= state->indent && !peekNewline(state))
        {
            error(state, "Continued line must have increased indentation.");
            return false;
        }
    }
    return true;
}

static vref readString(ParseState *state)
{
    bytevector string;
    boolean copied = false;
    const byte *begin;
    vref s;

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
                BVAddData(&string, begin, getOffset(state, begin));
                s = StringPoolAdd2(
                    (const char*)BVGetPointer(&string, 0),
                    BVSize(&string));
                BVDispose(&string);
            }
            else
            {
                s = StringPoolAdd2((const char*)begin, getOffset(state, begin));
            }
            state->current++;
            return s;

        case  '\\':
            if (!copied)
            {
                BVInit(&string, 128);
                copied = true;
            }
            BVAddData(&string, begin, getOffset(state, begin));
            state->current++;
            switch (state->current[0])
            {
            case '\\': BVAdd(&string, '\\'); break;
            case '\'': BVAdd(&string, '\''); break;
            case '"': BVAdd(&string, '"'); break;
            case '0': BVAdd(&string, '\0'); break;
            case 'f': BVAdd(&string, '\f'); break;
            case 'n': BVAdd(&string, '\n'); break;
            case 'r': BVAdd(&string, '\r'); break;
            case 't': BVAdd(&string, '\t'); break;
            case 'v': BVAdd(&string, '\v'); break;

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
                BVDispose(&string);
            }
            return 0;

        default:
            state->current++;
            break;
        }
    }
}

static vref readFilename(ParseState *state)
{
    const byte *begin;

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
    return StringPoolAdd2((const char*)begin, getOffset(state, begin));
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

static boolean readOperator3(ParseState *state, byte op1, byte op2, byte op3)
{
    if (state->current[0] == op1 &&
        state->current[1] == op2 &&
        state->current[2] == op3)
    {
        state->current += 3;
        return true;
    }
    return false;
}

static boolean readExpectedOperator(ParseState *state, byte op)
{
    if (!readOperator(state, op))
    {
        error(state, "Expected operator '%c'. Got '%c'", op,
              state->current[0]);
        return false;
    }
    return true;
}


/* TODO: Parse big numbers */
/* TODO: Parse non-decimal numbers */
/* TODO: Parse non-integer numbers */
static boolean parseNumber(ParseState *state, ExpressionState *estate)
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

    estate->expressionType = EXPRESSION_CONSTANT;
    estate->valueType = VALUE_NUMBER;
    estate->constant = HeapBoxInteger(value);
    return true;
}

static boolean parseReturnRest(ParseState *state)
{
    uint values = 0;

    if (readNewline(state))
    {
        ParseStateWriteInstruction(state, OP_RETURN_VOID);
        return true;
    }
    for (;;)
    {
        if (!parseRValue(state, false))
        {
            return false;
        }
        values++;
        if (readNewline(state))
        {
            ParseStateWriteReturn(state, values);
            return true;
        }
        skipWhitespace(state);
    }
}

static boolean checkNativeFunctionReturnValueCount(ParseState *state,
                                                   nativefunctionref function,
                                                   uint returnValues)
{
    if (NativeGetReturnValueCount(function) != returnValues)
    {
        statementError(
            state, "Native function returns %d values, but %d are handled.",
            NativeGetReturnValueCount(function), returnValues);
        return false;
    }
    return true;
}

static boolean finishLValue(ParseState *state, const ExpressionState *estate)
{
    switch (estate->expressionType)
    {
    case EXPRESSION_SIMPLE:
    case EXPRESSION_CONSTANT:
    case EXPRESSION_INVOCATION:
    case EXPRESSION_NATIVE_INVOCATION:
        statementError(state, "Invalid target for assignment.");
        return false;

    case EXPRESSION_VARIABLE:
        return ParseStateSetVariable(state, estate->valueIdentifier);

    case EXPRESSION_FIELD:
        ParseStateSetField(state, estate->field);
        return true;
    }
    assert(false);
    return false;
}

static boolean finishExpression(ParseState *state, const ExpressionState *estate)
{
    switch (estate->expressionType)
    {
    case EXPRESSION_SIMPLE:
        return true;

    case EXPRESSION_VARIABLE:
        return ParseStateGetVariable(state, estate->valueIdentifier);

    case EXPRESSION_CONSTANT:
        ParseStateWritePush(state, estate->constant);
        return true;

    case EXPRESSION_FIELD:
        ParseStateGetField(state, estate->field);
        return true;

    case EXPRESSION_INVOCATION:
        ParseStateWriteInvocation(state, estate->function, 1);
        return true;

    case EXPRESSION_NATIVE_INVOCATION:
        if (!checkNativeFunctionReturnValueCount(state,
                                                 estate->nativeFunction, 1))
        {
            return false;
        }
        ParseStateWriteNativeInvocation(state, estate->nativeFunction);
        return true;
    }
    assert(false);
    return false;
}

static boolean finishRValue(ParseState *state, ExpressionState *estate)
{
    return finishExpression(state, estate);
}

static boolean finishBoolean(ParseState *state, ExpressionState *estate)
{
    if (!finishExpression(state, estate))
    {
        return false;
    }
    if (estate->valueType != VALUE_BOOLEAN)
    {
        ParseStateWriteInstruction(state, OP_CAST_BOOLEAN);
        estate->valueType = VALUE_BOOLEAN;
    }
    return true;
}

static functionref lookupFunction(ParseState *state, namespaceref ns,
                                  vref name)
{
    functionref function;
    if (ns)
    {
        function = NamespaceGetFunction(ns, name);
        if (!function)
        {
            statementError(state, "Unknown function '%s.%s'.",
                           HeapGetString(NamespaceGetName(ns)),
                           HeapGetString(name));
        }
    }
    else
    {
        function = NamespaceLookupFunction(state->ns, name);
        if (!function)
        {
            statementError(state, "Unknown function '%s'.",
                           HeapGetString(name));
        }
    }
    return function;
}

static boolean parseUnnamedArguments(ParseState *state, ExpressionState *estate,
                                     uint *count)
{
    assert(!estate->identifier);
    for (;;)
    {
        estate->identifier = peekReadIdentifier(state);
        if (estate->identifier && peekOperator(state, ':'))
        {
            break;
        }
        if (!parseExpression(state, estate, false) ||
            !finishRValue(state, estate))
        {
            return false;
        }
        (*count)++;
        if (peekOperator(state, ')'))
        {
            break;
        }
        if (!skipWhitespaceAndNewline(state))
        {
            return false;
        }
    }
    return true;
}

static boolean parseOrderedNamedArguments(ParseState *state,
                                          ExpressionState *estate,
                                          const ParameterInfo *parameterInfo,
                                          uint parameterCount, uint *count)
{
    while (*count < parameterCount && estate->identifier &&
           estate->identifier == parameterInfo[*count].name &&
           readOperator(state, ':'))
    {
        if (!parseRValue(state, false))
        {
            return false;
        }
        (*count)++;
        if (peekOperator(state, ')'))
        {
            estate->identifier = 0;
            break;
        }
        if (!skipWhitespaceAndNewline(state))
        {
            return false;
        }
        estate->identifier = peekReadIdentifier(state);
    }
    return true;
}

static uint findArgumentIndex(const ParameterInfo *parameterInfo,
                              uint parameterCount, vref name)
{
    uint i;
    for (i = 0; i < parameterCount; i++)
    {
        if (parameterInfo[i].name == name)
        {
            break;
        }
    }
    return i;
}

static boolean parseNamedArguments(ParseState *state,
                                   ExpressionState *estate,
                                   const ParameterInfo *parameterInfo,
                                   uint parameterCount,
                                   uint requiredArgumentCount, uint varargIndex,
                                   uint *count)
{
    uint orderedArgumentCount = *count;
    uint argumentIndex;
    uint16 *unorderedValues = (uint16*)calloc(
        max(parameterCount - orderedArgumentCount, 1),
        sizeof(*unorderedValues));
    uint16 unorderedArgumentCount = 0;

    assert(estate->identifier);
    do
    {
        argumentIndex = findArgumentIndex(parameterInfo, parameterCount,
                                          estate->identifier);
        if (argumentIndex >= parameterCount)
        {
            free(unorderedValues);
            error(state, "Invalid parameter name '%s'.",
                  HeapGetString(estate->identifier));
            return false;
        }
        if (argumentIndex < orderedArgumentCount ||
            unorderedValues[argumentIndex - orderedArgumentCount])
        {
            free(unorderedValues);
            error(state, "More than one value for parameter '%s'.",
                  HeapGetString(estate->identifier));
            return false;
        }
        if (!readExpectedOperator(state, ':') ||
            !parseRValue(state, false))
        {
            free(unorderedValues);
            return false;
        }
        unorderedValues[argumentIndex - orderedArgumentCount] =
            ++unorderedArgumentCount;
        if (!skipWhitespaceAndNewline(state))
        {
            free(unorderedValues);
            return false;
        }
        estate->identifier = peekReadIdentifier(state);
    }
    while (estate->identifier);

    for (argumentIndex = 0; argumentIndex < unorderedArgumentCount;
         argumentIndex++)
    {
        if (unorderedValues[argumentIndex])
        {
            /* Zero is a valid value. To distinguish it from unset values (this
             * if statement), all written values were one too high. */
            unorderedValues[argumentIndex]--;
        }
        else
        {
            uint index = orderedArgumentCount + argumentIndex;
            vref value;
            if (index >= requiredArgumentCount)
            {
                value = parameterInfo[index].value;
            }
            else if (index == varargIndex)
            {
                value = HeapEmptyList;
            }
            else
            {
                statementError(
                    state, "No value for parameter '%s' given.",
                    HeapGetString(parameterInfo[index].name));
                free(unorderedValues);
                return false;
            }
            ParseStateWritePush(state, value);
            unorderedValues[argumentIndex] = unorderedArgumentCount++;
        }
    }
    ParseStateReorderStack(state, unorderedValues, unorderedArgumentCount);
    *count += unorderedArgumentCount;
    free(unorderedValues);
    return true;
}

static boolean parseInvocationRest(ParseState *state, ExpressionState *estate,
                                   namespaceref ns, vref name)
{
    ExpressionState estateArgument;
    functionref function;
    uint parameterCount;
    const ParameterInfo *parameterInfo;
    uint varargIndex;
    uint argumentCount = 0;
    uint requiredArgumentCount;
    uint line = state->line;

    ParseStateCheck(state);
    function = lookupFunction(state, ns, name);
    if (!function)
    {
        return false;
    }
    parameterCount = FunctionIndexGetParameterCount(function);
    requiredArgumentCount = FunctionIndexGetRequiredArgumentCount(function);
    parameterInfo = FunctionIndexGetParameterInfo(function);
    varargIndex = FunctionIndexHasVararg(function) ?
        FunctionIndexGetVarargIndex(function) : UINT_MAX;

    estate->expressionType = EXPRESSION_INVOCATION;
    estate->valueType = VALUE_UNKNOWN;
    estate->function = function;

    assert(!estate->identifier);
    estateArgument.identifier = 0;
    if (!readOperator(state, ')'))
    {
        if (!parseUnnamedArguments(state, &estateArgument, &argumentCount))
        {
            return false;
        }
        if (argumentCount > varargIndex)
        {
            ParseStateWriteList(state, argumentCount - varargIndex);
            argumentCount = varargIndex + 1;
        }
        if (!parseOrderedNamedArguments(state, &estateArgument, parameterInfo,
                                        parameterCount, &argumentCount))
        {
            return false;
        }
        if (estateArgument.identifier)
        {
            if (!parseNamedArguments(state, &estateArgument, parameterInfo,
                                     parameterCount, requiredArgumentCount,
                                     varargIndex, &argumentCount))
            {
                return false;
            }
        }
        if (!readExpectedOperator(state, ')'))
        {
            return false;
        }
    }

    if (argumentCount == varargIndex)
    {
        ParseStateWriteList(state, 0);
        argumentCount++;
    }
    if (argumentCount < requiredArgumentCount)
    {
        errorOnLine(state, line, "No value for parameter '%s' given.",
                    HeapGetString(parameterInfo[argumentCount].name));
        return false;
    }
    while (argumentCount < parameterCount)
    {
        ParseStateWritePush(state, parameterInfo[argumentCount++].value);
    }
    return true;
}

static boolean parseNativeInvocationRest(ParseState *state,
                                         ExpressionState *estate,
                                         vref name)
{
    nativefunctionref function = NativeFindFunction(name);
    uint parameterCount;

    ParseStateCheck(state);
    if (!function)
    {
        statementError(state, "Unknown native function '%s'.",
                       HeapGetString(name));
        return false;
    }
    parameterCount = NativeGetParameterCount(function);
    estate->expressionType = EXPRESSION_NATIVE_INVOCATION;
    estate->valueType = VALUE_UNKNOWN;
    estate->nativeFunction = function;

    while (parameterCount--)
    {
        if (!parseRValue(state, false))
        {
            return false;
        }
        skipWhitespace(state);
    }
    return readExpectedOperator(state, ')');
}

static boolean parseBinaryOperationRest(
    ParseState *state, ExpressionState *estate,
    boolean (*parseExpressionRest)(ParseState*, ExpressionState*),
    Instruction instruction, ValueType valueType)
{
    skipWhitespace(state);
    if (!finishRValue(state, estate) ||
        !parseExpressionRest(state, estate) ||
        !finishRValue(state, estate))
    {
        return false;
    }
    ParseStateWriteInstruction(state, instruction);
    estate->expressionType = EXPRESSION_SIMPLE;
    estate->valueType = valueType;
    skipWhitespace(state);
    return true;
}

static boolean parseQuotedValue(ParseState *state, ExpressionState *estate)
{
    static const char terminators[] = " \n\r(){}[]";
    const byte *begin = state->current;
    ParseStateCheck(state);
    assert(!estate->identifier);
    while (!eof(state) &&
           !memchr(terminators, *state->current, sizeof(terminators)))
    {
        state->current++;
    }
    if (state->current == begin)
    {
        error(state, "Invalid quoted value.");
        return false;
    }
    estate->expressionType = EXPRESSION_CONSTANT;
    estate->valueType = VALUE_STRING;
    estate->constant = StringPoolAdd2((const char*)begin,
                                      getOffset(state, begin));
    return true;
}

static boolean parseQuotedListRest(ParseState *state, ExpressionState *estate)
{
    ExpressionState estate2;
    const size_t bytecodeSize = BVSize(state->bytecode);
    boolean constant = true;
    intvector values;
    uint size = 0;

    ParseStateCheck(state);
    assert(!estate->identifier);
    if (!skipWhitespaceAndNewline(state))
    {
        return false;
    }
    IVInit(&values, 16);
    while (!readOperator(state, '}'))
    {
        size++;
        estate2.identifier = 0;
        if (readOperator(state, '$'))
        {
            if (!parseUnquotedExpression(state, &estate2, estate->parseConstant))
            {
                goto fail;
            }
        }
        else if (!parseQuotedValue(state, &estate2))
        {
            goto fail;
        }
        if (constant)
        {
            if (estate2.expressionType == EXPRESSION_CONSTANT)
            {
                IVAdd(&values, estate2.constant);
            }
            else
            {
                constant = false;
                IVDispose(&values);
            }
        }
        if (!finishRValue(state, &estate2) || !skipWhitespaceAndNewline(state))
        {
            goto fail;
        }
    }
    estate->valueType = VALUE_LIST;
    if (constant)
    {
        BVSetSize(state->bytecode, bytecodeSize);
        estate->expressionType = EXPRESSION_CONSTANT;
        estate->constant = HeapCreateArrayFromVector(&values);
        IVDispose(&values);
    }
    else
    {
        estate->expressionType = EXPRESSION_SIMPLE;
        ParseStateWriteList(state, size);
    }
    return true;
fail:
    if (constant)
    {
        IVDispose(&values);
    }
    return false;
}

static boolean parseExpression12(ParseState *state, ExpressionState *estate)
{
    ExpressionState estate2;
    vref identifier = estate->identifier;
    vref string;
    namespaceref ns;
    uint size;
    size_t bytecodeSize;
    boolean constant;
    intvector values;

    ParseStateCheck(state);
    estate->expressionType = EXPRESSION_SIMPLE;
    estate->valueType = VALUE_UNKNOWN;
    estate->identifier = 0;
    if (!identifier)
    {
        identifier = peekReadIdentifier(state);
    }
    if (identifier)
    {
        if (isKeyword(identifier))
        {
            estate->expressionType = EXPRESSION_CONSTANT;
            if (identifier == keywordTrue)
            {
                estate->valueType = VALUE_BOOLEAN;
                estate->constant = HeapTrue;
                return true;
            }
            else if (identifier == keywordFalse)
            {
                estate->valueType = VALUE_BOOLEAN;
                estate->constant = HeapFalse;
                return true;
            }
            else if (identifier == keywordNull)
            {
                estate->valueType = VALUE_NULL;
                estate->constant = 0;
                return true;
            }
            statementError(state, "Unexpected keyword '%s'.",
                           HeapGetString(identifier));
            return false;
        }
        if (estate->parseConstant)
        {
            statementError(state, "Expected constant.");
            return false;
        }
        if (!peekOperator2(state, '.', '.') &&
            readOperator(state, '.'))
        {
            ns = NamespaceGetNamespace(state->ns, identifier);
            if (!ns)
            {
                if (state->ns == NAMESPACE_DON &&
                    identifier == StringPoolAdd("native"))
                {
                    identifier = readVariableName(state);
                    if (!identifier || !readExpectedOperator(state, '('))
                    {
                        return false;
                    }
                    return parseNativeInvocationRest(state, estate, identifier);
                }
                statementError(state, "Unknown namespace '%s'.",
                               HeapGetString(identifier));
                return false;
            }
            identifier = readVariableName(state);
            if (!identifier)
            {
                return false;
            }
            if (readOperator(state, '('))
            {
                return parseInvocationRest(state, estate, ns, identifier);
            }
            estate->expressionType = EXPRESSION_FIELD;
            estate->field = NamespaceGetField(ns, identifier);
            if (!estate->field)
            {
                statementError(state, "Unknown field '%s.%s'.",
                               HeapGetString(NamespaceGetName(ns)),
                               HeapGetString(identifier));
            }
            return true;
        }
        if (ParseStateIsParameter(state, identifier))
        {
            estate->expressionType = EXPRESSION_VARIABLE;
            estate->valueIdentifier = identifier;
            return true;
        }
        if (readOperator(state, '('))
        {
            return parseInvocationRest(state, estate, 0, identifier);
        }
        estate->field = NamespaceLookupField(state->ns, identifier);
        if (estate->field)
        {
            estate->expressionType = EXPRESSION_FIELD;
            return true;
        }
        estate->expressionType = EXPRESSION_VARIABLE;
        estate->valueIdentifier = identifier;
        return true;
    }
    if (readOperator(state, '\''))
    {
        if (readOperator(state, '{'))
        {
            return parseQuotedListRest(state, estate);
        }
        return parseQuotedValue(state, estate);
    }
    if (peekNumber(state))
    {
        return parseNumber(state, estate);
    }
    if (peekString(state))
    {
        string = readString(state);
        if (!string)
        {
            return false;
        }
        estate->expressionType = EXPRESSION_CONSTANT;
        estate->valueType = VALUE_STRING;
        estate->constant = string;
        return true;
    }
    if (readOperator(state, '('))
    {
        skipWhitespace(state);
        estate2.identifier = 0;
        if (!parseRValue(state, estate->parseConstant))
        {
            return false;
        }
        estate->expressionType = EXPRESSION_SIMPLE;
        return readExpectedOperator(state, ')');
    }
    if (readOperator(state, '{'))
    {
        estate->valueType = VALUE_LIST;
        skipWhitespace(state);
        if (readOperator(state, '}'))
        {
            estate->expressionType = EXPRESSION_CONSTANT;
            estate->constant = HeapEmptyList;
            return true;
        }
        size = 0;
        bytecodeSize = BVSize(state->bytecode);
        constant = true;
        IVInit(&values, 16);
        do
        {
            size++;
            estate2.identifier = 0;
            if (!parseExpression(state, &estate2, false))
            {
                if (constant)
                {
                    IVDispose(&values);
                }
                return false;
            }
            if (constant)
            {
                if (estate2.expressionType == EXPRESSION_CONSTANT)
                {
                    IVAdd(&values, estate2.constant);
                }
                else
                {
                    constant = false;
                    IVDispose(&values);
                }
            }
            if (!finishRValue(state, &estate2))
            {
                if (constant)
                {
                    IVDispose(&values);
                }
                return false;
            }
            skipWhitespace(state);
        }
        while (!readOperator(state, '}'));
        if (constant)
        {
            BVSetSize(state->bytecode, bytecodeSize);
            estate->expressionType = EXPRESSION_CONSTANT;
            estate->constant = HeapCreateArrayFromVector(&values);
            IVDispose(&values);
        }
        else
        {
            ParseStateWriteList(state, size);
        }
        return true;
    }
    if (readOperator(state, '@'))
    {
        string = readFilename(state);
        if (!string)
        {
            return false;
        }
        estate->valueType = VALUE_FILE;
        if (!strchr(HeapGetString(string), '*'))
        {
            estate->expressionType = EXPRESSION_CONSTANT;
            estate->constant = HeapCreatePath(string);
            return true;
        }
        /* TODO: @{} syntax */
        ParseStateWriteFilelist(state, string);
        return true;
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
                !parseRValue(state, estate->parseConstant))
            {
                return false;
            }
            skipWhitespace(state);
            if (!readExpectedOperator(state, ']'))
            {
                return false;
            }
            ParseStateWriteInstruction(state, OP_INDEXED_ACCESS);
            estate->expressionType = EXPRESSION_SIMPLE;
            estate->valueType = VALUE_UNKNOWN;
            continue;
        }
        if (!peekOperator2(state, '.', '.') &&
            readOperator(state, '.'))
        {
            assert(false); /* TODO: handle namespace */
        }
        break;
    }
    return true;
}

static boolean parseExpression10(ParseState *state, ExpressionState *estate)
{
    boolean first = true;
    boolean acceptNonString;
    for (;;)
    {
        if (!parseExpression11(state, estate))
        {
            return false;
        }
        acceptNonString = estate->expressionType == EXPRESSION_CONSTANT &&
            estate->valueType == VALUE_STRING;
        if (!first)
        {
            if (!finishRValue(state, estate))
            {
                return false;
            }
            ParseStateWriteInstruction(state, OP_CONCAT_STRING);
            estate->expressionType = EXPRESSION_SIMPLE;
            estate->valueType = VALUE_STRING;
        }
        if (!peekString(state) &&
            (!acceptNonString ||
             (!peekIdentifier(state) && !peekNumber(state) &&
              !peekOperator(state, '(') &&
              !peekOperator(state, '{') && !peekOperator(state, '@'))))
        {
            return true;
        }
        /* TODO: Parse concatenated string as constant if possible. */
        if (estate->parseConstant)
        {
            statementError(state, "Expected constant.");
            return false;
        }
        if (!finishRValue(state, estate))
        {
            return false;
        }
        first = false;
    }
}

static boolean parseExpression9(ParseState *state, ExpressionState *estate)
{
    if (readOperator(state, '-'))
    {
        assert(!readOperator(state, '-')); /* TODO: -- operator */
        if (!parseExpression10(state, estate) ||
            !finishRValue(state, estate))
        {
            return false;
        }
        ParseStateWriteInstruction(state, OP_NEG);
        skipExpressionWhitespace(state, estate);
        estate->expressionType = EXPRESSION_SIMPLE;
        estate->valueType = VALUE_NUMBER;
        return true;
    }
    if (readOperator(state, '!'))
    {
        if (!parseExpression10(state, estate) ||
            !finishBoolean(state, estate))
        {
            return false;
        }
        ParseStateWriteInstruction(state, OP_NOT);
        skipExpressionWhitespace(state, estate);
        estate->expressionType = EXPRESSION_SIMPLE;
        return true;
    }
    if (readOperator(state, '~'))
    {
        if (!parseExpression10(state, estate) ||
            !finishRValue(state, estate))
        {
            return false;
        }
        ParseStateWriteInstruction(state, OP_INV);
        skipExpressionWhitespace(state, estate);
        estate->expressionType = EXPRESSION_SIMPLE;
        estate->valueType = VALUE_NUMBER;
        return true;
    }
    return parseExpression10(state, estate);
}

static boolean parseExpression8(ParseState *state, ExpressionState *estate)
{
    if (!parseExpression9(state, estate))
    {
        return false;
    }
    for (;;)
    {
        skipExpressionWhitespace(state, estate);
        if (readOperator(state, '*'))
        {
            if (reverseIfOperator(state, '='))
            {
                return true;
            }
            if (!parseBinaryOperationRest(
                    state, estate, parseExpression9, OP_MUL, VALUE_NUMBER))
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
            if (!parseBinaryOperationRest(
                    state, estate, parseExpression9, OP_DIV, VALUE_NUMBER))
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
            if (!parseBinaryOperationRest(
                    state, estate, parseExpression9, OP_REM, VALUE_NUMBER))
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
        if (readOperator(state, '+'))
        {
            if (reverseIfOperator(state, '='))
            {
                return true;
            }
            assert(!readOperator(state, '+')); /* TODO: ++ operator */
            if (!parseBinaryOperationRest(
                    state, estate, parseExpression8, OP_ADD, VALUE_NUMBER))
            {
                return false;
            }
            continue;
        }
        else if (readOperator(state, '-'))
        {
            if (peekOperator(state, '=') ||
                (estate->valueType != VALUE_UNKNOWN &&
                 estate->valueType != VALUE_NUMBER &&
                 state->current[0] != ' '))
            {
                state->current--;
                return true;
            }
            assert(!readOperator(state, '-')); /* TODO: -- operator */
            if (!parseBinaryOperationRest(
                    state, estate, parseExpression8, OP_SUB, VALUE_NUMBER))
            {
                return false;
            }
            continue;
        }
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
        /* TODO: Parse operators << >> */
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
        /* TODO: Parse operators & | ^ */
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
        if (readOperator2(state, '.', '.'))
        {
            if (!parseBinaryOperationRest(
                    state, estate, parseExpression5, OP_RANGE, VALUE_LIST))
            {
                return false;
            }
            continue;
        }
        if (readOperator2(state, ':', ':'))
        {
            if (!parseBinaryOperationRest(
                    state, estate, parseExpression5, OP_CONCAT_LIST, VALUE_LIST))
            {
                return false;
            }
            continue;
        }
        break;
    }
    return true;
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
            if (!parseBinaryOperationRest(state, estate, parseExpression4,
                                          OP_EQUALS, VALUE_BOOLEAN))
            {
                return false;
            }
            continue;
        }
        if (readOperator2(state, '!', '='))
        {
            if (!parseBinaryOperationRest(state, estate, parseExpression4,
                                          OP_NOT_EQUALS, VALUE_BOOLEAN))
            {
                return false;
            }
            continue;
        }
        if (readOperator2(state, '<', '='))
        {
            if (!parseBinaryOperationRest(state, estate, parseExpression4,
                                          OP_LESS_EQUALS, VALUE_BOOLEAN))
            {
                return false;
            }
            continue;
        }
        if (readOperator2(state, '>', '='))
        {
            if (!parseBinaryOperationRest(state, estate, parseExpression4,
                                          OP_GREATER_EQUALS, VALUE_BOOLEAN))
            {
                return false;
            }
            continue;
        }
        if (readOperator(state, '<'))
        {
            if (!parseBinaryOperationRest(state, estate, parseExpression4,
                                          OP_LESS, VALUE_BOOLEAN))
            {
                return false;
            }
            continue;
        }
        if (readOperator(state, '>'))
        {
            if (!parseBinaryOperationRest(state, estate, parseExpression4,
                                          OP_GREATER, VALUE_BOOLEAN))
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
            skipExpressionWhitespace(state, estate);
            if (!finishBoolean(state, estate))
            {
                return false;
            }
            ParseStateWriteInstruction(state, OP_DUP);
            branch = ParseStateWriteForwardJump(state, OP_BRANCH_FALSE);
            ParseStateWriteInstruction(state, OP_POP);
            if (!parseExpression3(state, estate) ||
                !finishBoolean(state, estate))
            {
                return false;
            }
            ParseStateFinishJump(state, branch);
            estate->expressionType = EXPRESSION_SIMPLE;
            estate->valueType = VALUE_BOOLEAN;
            skipExpressionWhitespace(state, estate);
            continue;
        }
        if (readOperator2(state, '|', '|'))
        {
            skipExpressionWhitespace(state, estate);
            if (!finishBoolean(state, estate))
            {
                return false;
            }
            ParseStateWriteInstruction(state, OP_DUP);
            branch = ParseStateWriteForwardJump(state, OP_BRANCH_TRUE);
            ParseStateWriteInstruction(state, OP_POP);
            if (!parseExpression3(state, estate) ||
                !finishBoolean(state, estate))
            {
                return false;
            }
            ParseStateFinishJump(state, branch);
            estate->expressionType = EXPRESSION_SIMPLE;
            estate->valueType = VALUE_BOOLEAN;
            skipExpressionWhitespace(state, estate);
            continue;
        }
        break;
    }
    return true;
}

static boolean parseExpressionRest(ParseState *state, ExpressionState *estate)
{
    const boolean parseConstant = estate->parseConstant;

    if (!parseExpression2(state, estate))
    {
        return false;
    }
    if (readOperator(state, '?'))
    {
        size_t offset, offset2;
        skipExpressionWhitespace(state, estate);
        if (!finishBoolean(state, estate))
        {
            return false;
        }
        offset = ParseStateWriteJump(state, OP_BRANCH_FALSE, 0);
        if (!parseRValue(state, parseConstant) ||
            !readExpectedOperator(state, ':'))
        {
            return false;
        }
        offset2 = ParseStateWriteJump(state, OP_JUMP, 0);
        ParseStateSetJumpOffset(state, offset, (int)(BVSize(state->bytecode) - offset));
        skipExpressionWhitespace(state, estate);
        if (!parseRValue(state, parseConstant))
        {
            return false;
        }
        ParseStateSetJumpOffset(state, offset2, (int)(BVSize(state->bytecode) - offset2));
        estate->expressionType = EXPRESSION_SIMPLE;
        estate->valueType = VALUE_UNKNOWN;
        return true;
    }
    return true;
}

static boolean parseExpression(ParseState *state, ExpressionState *estate,
                               boolean constant)
{
    estate->parseConstant = constant;
    estate->allowSpace = true;
    return parseExpressionRest(state, estate);
}

static boolean parseUnquotedExpression(ParseState *state,
                                       ExpressionState *estate,
                                       boolean constant)
{
    estate->parseConstant = constant;
    estate->allowSpace = false;
    return parseExpressionRest(state, estate);
}

static boolean parseRValue(ParseState *state, boolean constant)
{
    ExpressionState estate;

    estate.identifier = 0;
    return parseExpression(state, &estate, constant) &&
        finishRValue(state, &estate);
}

static boolean parseBooleanValue(ParseState *state)
{
    ExpressionState estate;

    estate.identifier = 0;
    if (!parseExpression(state, &estate, false) ||
        !finishExpression(state, &estate))
    {
        return false;
    }
    if (estate.valueType != VALUE_BOOLEAN)
    {
        ParseStateWriteInstruction(state, OP_CAST_BOOLEAN);
    }
    return true;
}

static boolean parseAssignmentExpressionRest(ParseState *state,
                                             ExpressionState *estate,
                                             Instruction instruction)
{
    skipWhitespace(state);
    if (!finishRValue(state, estate) ||
        !parseRValue(state, false))
    {
        return false;
    }
    ParseStateWriteInstruction(state, instruction);
    if (!finishLValue(state, estate))
    {
        return false;
    }
    return true;
}

static boolean parseMultiAssignmentRest(ParseState *state)
{
    ExpressionState estate;
    bytevector lvalues;
    uint returnValueCount;

    BVInit(&lvalues, 16);
    do
    {
        skipWhitespace(state);
        estate.identifier = 0;
        if (!parseExpression(state, &estate, false))
        {
            BVDispose(&lvalues);
            return false;
        }
        BVAddData(&lvalues, (byte*)&estate, sizeof(estate));
        skipWhitespace(state);
    }
    while (peekIdentifier(state));
    if (!readExpectedOperator(state, '='))
    {
        BVDispose(&lvalues);
        return false;
    }
    skipWhitespace(state);
    estate.identifier = 0;
    if (!parseExpression(state, &estate, false))
    {
        BVDispose(&lvalues);
        return false;
    }
    returnValueCount = (uint)(BVSize(&lvalues) / sizeof(estate) + 1);
    if (estate.expressionType == EXPRESSION_INVOCATION)
    {
        ParseStateWriteInvocation(state, estate.function, returnValueCount);
    }
    else if (estate.expressionType == EXPRESSION_NATIVE_INVOCATION)
    {
        if (!checkNativeFunctionReturnValueCount(
                state, estate.nativeFunction, returnValueCount))
        {
            BVDispose(&lvalues);
            return false;
        }
        ParseStateWriteNativeInvocation(state, estate.nativeFunction);
    }
    else
    {
        BVDispose(&lvalues);
        statementError(state, "Expected function invocation.");
        return false;
    }
    while (BVSize(&lvalues))
    {
        BVPopData(&lvalues, (byte*)&estate, sizeof(estate));
        if (!finishLValue(state, &estate))
        {
            BVDispose(&lvalues);
            return false;
        }
    }
    BVDispose(&lvalues);
    return true;
}

static boolean parseExpressionStatement(ParseState *state,
                                        vref identifier)
{
    ExpressionState estate;

    estate.identifier = identifier;
    if (!parseExpression(state, &estate, false))
    {
        return false;
    }
    if (readOperator(state, '='))
    {
        skipWhitespace(state);
        return parseRValue(state, false) && finishLValue(state, &estate);
    }
    if (readOperator2(state, '+', '='))
    {
        return parseAssignmentExpressionRest(state, &estate, OP_ADD);
    }
    if (readOperator2(state, '-', '='))
    {
        return parseAssignmentExpressionRest(state, &estate, OP_SUB);
    }
    if (readOperator2(state, '*', '='))
    {
        return parseAssignmentExpressionRest(state, &estate, OP_MUL);
    }
    if (readOperator2(state, '/', '='))
    {
        return parseAssignmentExpressionRest(state, &estate, OP_DIV);
    }
    if (readOperator2(state, '%', '='))
    {
        return parseAssignmentExpressionRest(state, &estate, OP_REM);
    }
    if (estate.expressionType == EXPRESSION_INVOCATION)
    {
        ParseStateWriteInvocation(state, estate.function, 0);
        return true;
    }
    if (estate.expressionType == EXPRESSION_NATIVE_INVOCATION)
    {
        if (!checkNativeFunctionReturnValueCount(state,
                                                 estate.nativeFunction, 0))
        {
            return false;
        }
        ParseStateWriteNativeInvocation(state, estate.nativeFunction);
        return true;
    }
    if (peekIdentifier(state))
    {
        return parseMultiAssignmentRest(state) && finishLValue(state, &estate);
    }
    statementError(state, "Not a statement.");
    return false;
}

static int parseBlockRest(ParseState *state, int indent)
{
    int indent2;
    vref identifier;

    identifier = peekReadIdentifier(state);
    for (;;)
    {
        /* Must be reset every iteration, as recursive calls will modify indent. */
        state->indent = indent;
        state->statementLine = state->line;

        if (identifier)
        {
            if (isKeyword(identifier))
            {
                if (identifier > maxStatementKeyword)
                {
                    statementError(state, "Not a statement.");
                    return -1;
                }
                skipWhitespace(state);
                if (identifier == keywordIf)
                {
                    int conditionOffset;
                    int offset = 0;
                    if (!parseBooleanValue(state))
                    {
                        return -1;
                    }
                    if (!readNewline(state) && !readComment(state))
                    {
                        error(state, "Garbage after if statement.");
                        return -1;
                    }
                    conditionOffset = (int)ParseStateWriteJump(state, OP_BRANCH_FALSE, 0);

                    indent2 = parseBlock(state, indent);
                    assert(indent2 <= indent);
                    if (indent2 != indent)
                    {
                        if (indent2 < 0)
                        {
                            return -1;
                        }
                        ParseStateSetJumpOffset(state, (size_t)conditionOffset,
                                                (int)BVSize(state->bytecode) - conditionOffset);
                        return indent2;
                    }

                    identifier = peekReadIdentifier(state);
                    if (identifier != keywordElse)
                    {
                        ParseStateSetJumpOffset(state, (size_t)conditionOffset,
                                                (int)BVSize(state->bytecode) - conditionOffset);
                        continue;
                    }

                    for (;;)
                    {
                        offset = (int)ParseStateWriteJump(state, OP_JUMP, (int)offset);
                        ParseStateSetJumpOffset(state, (size_t)conditionOffset,
                                                offset - conditionOffset);
                        skipWhitespace(state);
                        identifier = peekReadIdentifier(state);
                        if (identifier == keywordIf)
                        {
                            skipWhitespace(state);
                            if (!parseBooleanValue(state))
                            {
                                return -1;
                            }
                            if (!readNewline(state) && !readComment(state))
                            {
                                error(state, "Garbage after if statement.");
                                return -1;
                            }
                            conditionOffset = (int)ParseStateWriteJump(state, OP_BRANCH_FALSE, 0);
                            indent2 = parseBlock(state, indent);
                            assert(indent2 <= indent);
                            if (indent2 != indent)
                            {
                                if (indent2 < 0)
                                {
                                    return -1;
                                }
                                ParseStateSetJumpOffset(
                                    state, (size_t)conditionOffset,
                                    (int)BVSize(state->bytecode) - conditionOffset);
                                break;
                            }
                            identifier = peekReadIdentifier(state);
                            if (identifier != keywordElse)
                            {
                                ParseStateSetJumpOffset(
                                    state, (size_t)conditionOffset,
                                    (int)BVSize(state->bytecode) - conditionOffset);
                                break;
                            }
                        }
                        else
                        {
                            if (identifier || (!readNewline(state) && !readComment(state)))
                            {
                                error(state, "Garbage after else.");
                                return -1;
                            }
                            indent2 = parseBlock(state, indent);
                            assert(indent2 <= indent);
                            if (indent2 != indent)
                            {
                                if (indent2 < 0)
                                {
                                    return -1;
                                }
                                break;
                            }
                            identifier = peekReadIdentifier(state);
                            break;
                        }
                    }

                    while (offset)
                    {
                        offset = (int)ParseStateSetJumpOffset(state, (size_t)offset, (int)BVSize(state->bytecode) - offset);
                    }
                    if (indent != indent2)
                    {
                        return indent2;
                    }
                    continue;
                }
                if (identifier == keywordElse)
                {
                    error(state, "else without matching if.");
                    return -1;
                }
                else if (identifier == keywordFor)
                {
                    size_t loopTop, afterLoop;
                    uint16 iterCollection;
                    uint16 iterIndex;
                    identifier = readVariableName(state);
                    if (!identifier)
                    {
                        return -1;
                    }
                    skipWhitespace(state);
                    if (!ParseStateCreateUnnamedVariable(state, &iterCollection) ||
                        !ParseStateCreateUnnamedVariable(state, &iterIndex) ||
                        !readExpectedKeyword(state, keywordIn))
                    {
                        return -1;
                    }
                    skipWhitespace(state);
                    if (!parseRValue(state, false))
                    {
                        return -1;
                    }
                    ParseStateSetUnnamedVariable(state, iterCollection);
                    ParseStateWritePush(state, HeapBoxInteger(-1));
                    ParseStateSetUnnamedVariable(state, iterIndex);
                    if (!peekNewline(state) && !readComment(state))
                    {
                        error(state, "Garbage after for statement.");
                        return -1;
                    }
                    skipEndOfLine(state);
                    loopTop = ParseStateGetJumpTarget(state);
                    ParseStateGetUnnamedVariable(state, iterCollection);
                    ParseStateGetUnnamedVariable(state, iterIndex);
                    ParseStateWritePush(state, HeapBoxInteger(1));
                    ParseStateWriteInstruction(state, OP_ADD);
                    ParseStateWriteInstruction(state, OP_DUP);
                    ParseStateSetUnnamedVariable(state, iterIndex);
                    ParseStateWriteInstruction(state, OP_ITER_GET);
                    if (!ParseStateSetVariable(state, identifier))
                    {
                        return -1;
                    }
                    afterLoop = ParseStateWriteForwardJump(state, OP_BRANCH_FALSE);
                    indent2 = parseBlock(state, indent);
                    assert(indent2 <= indent);
                    if (indent2 < 0)
                    {
                        return -1;
                    }
                    ParseStateWriteBackwardJump(state, OP_JUMP, loopTop);
                    ParseStateFinishJump(state, afterLoop);
                    if (indent2 != indent)
                    {
                        return indent2;
                    }
                    identifier = peekReadIdentifier(state);
                    continue;
                }
                if (identifier == keywordReturn)
                {
                    if (!parseReturnRest(state))
                    {
                        return -1;
                    }
                }
                else if (identifier == keywordWhile)
                {
                    size_t afterLoop;
                    size_t loopTop = ParseStateGetJumpTarget(state);
                    if (!parseBooleanValue(state))
                    {
                        return -1;
                    }
                    if (!readNewline(state) && !readComment(state))
                    {
                        error(state, "Garbage after while statement.");
                        return -1;
                    }
                    afterLoop = ParseStateWriteForwardJump(state, OP_BRANCH_FALSE);
                    indent2 = parseBlock(state, indent);
                    assert(indent2 <= indent);
                    if (indent2 < 0)
                    {
                        return -1;
                    }
                    ParseStateWriteBackwardJump(state, OP_JUMP, loopTop);
                    ParseStateFinishJump(state, afterLoop);
                    if (indent2 != indent)
                    {
                        return indent2;
                    }
                    identifier = peekReadIdentifier(state);
                    continue;
                }
                else
                {
                    assert(false);
                    return -1;
                }
            }
            else
            {
                if (!parseExpressionStatement(state, identifier))
                {
                    return -1;
                }
                assert(peekNewline(state));
                skipEndOfLine(state);
            }
        }
        else
        {
            error(state, "Not a statement.");
            return -1;
        }

        for (;;)
        {
            if (eof(state))
            {
                return 0;
            }

            indent2 = readIndent(state);
            if (readComment(state))
            {
                continue;
            }
            if (peekNewline(state))
            {
                if (indent2)
                {
                    error(state, "Trailing whitespace.");
                }
                skipEndOfLine(state);
                continue;
            }
            break;
        }
        if (indent2 != indent)
        {
            if (indent2 < indent)
            {
                return indent2;
            }
            error(state, "Mismatched indentation level.");
            return -1;
        }
        identifier = peekReadIdentifier(state);
    }
}

static int parseBlock(ParseState *state, int parentIndent)
{
    int indent;
    for (;;)
    {
        if (eof(state))
        {
            error(state, "Expected code block.");
            return -1;
        }
        indent = readIndent(state);
        if (readComment(state))
        {
            continue;
        }
        if (peekNewline(state))
        {
            if (indent)
            {
                error(state, "Trailing whitespace.");
            }
            skipEndOfLine(state);
            continue;
        }
        break;
    }
    if (indent <= parentIndent)
    {
        error(state, "Expected increased indentation level.");
        return -1;
    }
    indent = parseBlockRest(state, indent);
    if (indent > parentIndent)
    {
        error(state, "Mismatched indentation level.");
        return -1;
    }
    return indent;
}

static boolean parseFunctionDeclaration(ParseState *state, functionref function)
{
    ExpressionState estate;
    vref parameterName;
    vref value = 0;
    boolean vararg;
    boolean requireDefaultValues = false;

    if (readOperator(state, ':'))
    {
        if (!peekNewline(state))
        {
            error(state, "Garbage after target declaration.");
            return false;
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
                vararg = false;
                parameterName = peekReadIdentifier(state);
                if (!parameterName || isKeyword(parameterName))
                {
                    error(state, "Expected parameter name or ')'.");
                    return false;
                }
                skipWhitespace(state);
                if (readOperator(state, ':'))
                {
                    requireDefaultValues = true;
                    skipWhitespace(state);
                    estate.identifier = 0;
                    if (!parseExpression(state, &estate, true))
                    {
                        return false;
                    }
                    assert(estate.expressionType == EXPRESSION_CONSTANT);
                    value = estate.constant;
                }
                else if (requireDefaultValues)
                {
                    error(state, "Default value for parameter '%s' required.",
                          HeapGetString(parameterName));
                    return false;
                }
                else if (readOperator3(state, '.', '.', '.'))
                {
                    requireDefaultValues = true;
                    skipWhitespace(state);
                    vararg = true;
                }
                FunctionIndexAddParameter(function, parameterName,
                                          requireDefaultValues && !vararg,
                                          value, vararg);
                if (readOperator(state, ')'))
                {
                    break;
                }
                skipWhitespace(state);
            }
        }
        if (!peekNewline(state))
        {
            error(state, "Garbage after function declaration.");
            return false;
        }
    }
    skipEndOfLine(state);
    FunctionIndexFinishParameters(function, state->line,
                                  getOffset(state, state->start));
    return true;
}

static void parseScript(ParseState *state)
{
    vref name;
    boolean allowIndent = false;

    ParseStateCheck(state);
    while (!eof(state))
    {
        if (peekIdentifier(state))
        {
            state->statementLine = state->line;
            name = readIdentifier(state);
            if (peekOperator(state, ':'))
            {
                NamespaceAddTarget(state->ns, name, FunctionIndexAddFunction(
                                       state->ns, name,
                                       state->filename, state->line,
                                       getOffset(state, state->start)));
                skipEndOfLine(state);
                allowIndent = true;
            }
            else if (peekOperator(state, '('))
            {
                NamespaceAddFunction(state->ns, name, FunctionIndexAddFunction(
                                         state->ns, name,
                                         state->filename, state->line,
                                         getOffset(state, state->start)));
                skipEndOfLine(state);
                allowIndent = true;
            }
            else
            {
                skipWhitespace(state);
                if (!readOperator(state, '='))
                {
                    error(state, "Invalid declaration.");
                    allowIndent = true;
                    skipEndOfLine(state);
                    continue;
                }
                skipWhitespace(state);
                NamespaceAddField(state->ns, name, FieldIndexAdd(
                                      state->ns,
                                      state->filename, state->line,
                                      getOffset(state, state->start)));
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
            error(state, "Unsupported character: '%c'", state->current[0]);
            skipEndOfLine(state);
        }
    }
}

void ParserAddKeywords(void)
{
    keywordElse = StringPoolAdd("else");
    keywordFalse = StringPoolAdd("false");
    keywordFor = StringPoolAdd("for");
    keywordIf = StringPoolAdd("if");
    keywordIn = StringPoolAdd("in");
    keywordNull = StringPoolAdd("null");
    keywordReturn = StringPoolAdd("return");
    keywordTrue = StringPoolAdd("true");
    keywordWhile = StringPoolAdd("while");
    maxStatementKeyword = keywordWhile;
    maxKeyword = keywordWhile;
}

void ParseFile(vref filename, namespaceref ns)
{
    ParseState state;

    ParseStateInit(&state, null, ns, 0, filename, 1, 0);
    if (state.current != state.limit && state.limit[-1] != '\n')
    {
        /* TODO: Provide fallback */
        errorOnLine(&state,
                    UtilCountNewlines((const char*)state.start,
                                      (size_t)(state.limit - state.start)) + 1,
                    "File does not end with newline.");
    }
    else
    {
        parseScript(&state);
    }
    ParseStateDispose(&state);
}

void ParseField(fieldref field, bytevector *bytecode)
{
    ParseState state;
    size_t start = BVSize(bytecode);

    assert(field);
    ParseStateInit(&state, bytecode, FieldIndexGetNamespace(field), 0,
                   FieldIndexGetFilename(field), FieldIndexGetLine(field),
                   FieldIndexGetFileOffset(field));

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
            FieldIndexSetBytecodeOffset(field, start, BVSize(bytecode));
        }
    }
    ParseStateDispose(&state);
}

void ParseFunctionDeclaration(functionref function, bytevector *bytecode)
{
    ParseState state;

    assert(function);
    ParseStateInit(&state, bytecode,
                   FunctionIndexGetNamespace(function),
                   function,
                   FunctionIndexGetFilename(function),
                   FunctionIndexGetLine(function),
                   FunctionIndexGetFileOffset(function));
    state.statementLine = state.line;
    if (!parseFunctionDeclaration(&state, function))
    {
        FunctionIndexSetFailedDeclaration(function);
    }
    ParseStateDispose(&state);
}

void ParseFunctionBody(functionref function, bytevector *bytecode)
{
    ParseState state;
    size_t start = BVSize(bytecode);
    size_t paramsOffset;
    size_t localsOffset;
    uint line;

    assert(function);
    line = FunctionIndexGetLine(function);
    if (!line)
    {
        return;
    }
    BVAdd(bytecode, OP_FUNCTION);
    BVAddRef(bytecode, function);
    paramsOffset = BVSize(bytecode);
    BVAddUint(bytecode, 0);
    localsOffset = BVSize(bytecode);
    BVAddUint(bytecode, 0);
    ParseStateInit(&state, bytecode,
                   FunctionIndexGetNamespace(function),
                   function,
                   FunctionIndexGetFilename(function),
                   line,
                   FunctionIndexGetFileOffset(function));
    if (parseBlock(&state, 0) >= 0)
    {
        FunctionIndexSetLocals(function, &state.locals, ParseStateLocalsCount(&state));
        ParseStateWriteInstruction(&state, OP_RETURN_VOID);
        FunctionIndexSetBytecodeOffset(function, start);
    }
    BVSetUint(bytecode, paramsOffset, FunctionIndexGetParameterCount(function));
    BVSetUint(bytecode, localsOffset, FunctionIndexGetLocalsCount(function));
    ParseStateDispose(&state);
}
