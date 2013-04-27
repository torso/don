#include <memory.h>
#include <stdarg.h>
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
    stringref identifier;
    ExpressionType expressionType;
    ValueType valueType;
    stringref valueIdentifier;
    fieldref field;
    nativefunctionref nativeFunction;
    functionref function;
    uint argumentCount;
    int *arguments;
    boolean parseConstant;
} ExpressionState;

static stringref keywordElse;
static stringref keywordFalse;
static stringref keywordFor;
static stringref keywordIf;
static stringref keywordIn;
static stringref keywordNull;
static stringref keywordReturn;
static stringref keywordTrue;
static stringref keywordWhile;

static stringref maxStatementKeyword;
static stringref maxKeyword;

static boolean parseExpression(ParseState *state, ExpressionState *estate,
                               boolean constant);
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

    ParseStateCheck(state);
    assert(peekIdentifier(state));
    while (isIdentifierCharacter(*++state->current));
    return StringPoolAdd2((const char*)begin, getOffset(state, begin));
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
        error(state, "Expected variable name.");
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
    statementError(state, "Expected keyword %s.", StringPoolGetString(keyword));
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

static stringref readFilename(ParseState *state)
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
    estate->field = FieldIndexAddIntegerConstant(value);
    return true;
}

static boolean parseReturnRest(ParseState *state)
{
    uint values = 0;

    if (peekNewline(state))
    {
        ParseStateWriteReturnVoid(state);
        return true;
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
            ParseStateWriteReturn(state, values);
            return true;
        }
        if (!readExpectedOperator(state, ','))
        {
            return false;
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
        switch (estate->field)
        {
        case FIELD_NULL + 1:
            ParseStateWriteInstruction(state, OP_NULL);
            return true;
        case FIELD_TRUE + 1:
            ParseStateWriteInstruction(state, OP_TRUE);
            return true;
        case FIELD_FALSE + 1:
            ParseStateWriteInstruction(state, OP_FALSE);
            return true;
        case FIELD_EMPTY_LIST + 1:
            ParseStateWriteInstruction(state, OP_EMPTY_LIST);
            return true;
        }
        /* fallthrough */
    case EXPRESSION_FIELD:
        ParseStateGetField(state, estate->field);
        return true;

    case EXPRESSION_INVOCATION:
        ParseStateWriteInvocation(state, estate->function,
                                  estate->argumentCount, estate->arguments, 1);
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

static boolean finishVoidValue(ParseState *state, ExpressionState *estate)
{
    switch (estate->expressionType)
    {
    case EXPRESSION_SIMPLE:
    case EXPRESSION_VARIABLE:
    case EXPRESSION_CONSTANT:
    case EXPRESSION_FIELD:
        statementError(state, "Not a statement.");
        return false;

    case EXPRESSION_INVOCATION:
        ParseStateWriteInvocation(state, estate->function,
                                  estate->argumentCount, estate->arguments, 0);
        return true;

    case EXPRESSION_NATIVE_INVOCATION:
        if (!checkNativeFunctionReturnValueCount(state,
                                                 estate->nativeFunction, 0))
        {
            return false;
        }
        ParseStateWriteNativeInvocation(state, estate->nativeFunction);
        return true;
    }
    assert(false);
    return false;
}

static boolean parseInvocationRest(ParseState *state, ExpressionState *estate,
                                   namespaceref ns, stringref name)
{
    ExpressionState estateArgument;
    functionref function = 0;
    uint parameterCount;
    const ParameterInfo *parameterInfo;
    uint varargIndex;
    uint argumentCount = 0;
    uint argumentIndex;
    uint line = state->line;
    boolean requireNamedParameters = false;
    int position;
    uint i;
    int variableIndex;
    size_t bytecodeSize;
    boolean constant;
    intvector values;

    ParseStateCheck(state);
    if (ns)
    {
        function = NamespaceGetFunction(ns, name);
        if (!function)
        {
            statementError(state, "Unknown function '%s.%s'.",
                           StringPoolGetString(NamespaceGetName(ns)),
                           StringPoolGetString(name));
            return false;
        }
    }
    else
    {
        function = NamespaceLookupFunction(state->ns, name);
        if (!function)
        {
            statementError(state, "Unknown function '%s'.",
                           StringPoolGetString(name));
            return false;
        }
    }
    parameterCount = FunctionIndexGetParameterCount(function);
    parameterInfo = FunctionIndexGetParameterInfo(function);
    varargIndex = FunctionIndexHasVararg(function) ?
        FunctionIndexGetVarargIndex(function) : UINT_MAX;

    estate->expressionType = EXPRESSION_INVOCATION;
    estate->valueType = VALUE_UNKNOWN;
    estate->function = function;
    estate->argumentCount = 0;
    estate->arguments = null;
    if (parameterCount)
    {
        estate->arguments = (int*)calloc(parameterCount, sizeof(int));
    }

    if (!readOperator(state, ')'))
    {
        estateArgument.identifier = peekReadIdentifier(state);
        for (;;)
        {
            if (estateArgument.identifier && readOperator(state, ':'))
            {
                requireNamedParameters = true;
                for (argumentIndex = 0;; argumentIndex++)
                {
                    if (argumentIndex == parameterCount)
                    {
                        free(estate->arguments);
                        error(state, "Invalid parameter name '%s'.",
                              StringPoolGetString(estateArgument.identifier));
                        return false;
                    }
                    if (parameterInfo[argumentIndex].name ==
                        estateArgument.identifier)
                    {
                        if (estate->arguments[argumentIndex])
                        {
                            free(estate->arguments);
                            error(state, "More than one value for parameter '%s'.",
                                  StringPoolGetString(estateArgument.identifier));
                            return false;
                        }
                        estateArgument.identifier = peekReadIdentifier(state);
                        break;
                    }
                }
            }
            else if (requireNamedParameters)
            {
                free(estate->arguments);
                error(state, "Expected parameter name.");
                return false;
            }
            else if (argumentCount == varargIndex)
            {
                argumentCount++;
                i = 0;
                bytecodeSize = BVSize(state->bytecode);
                constant = true;
                IVInit(&values, 16);
                for (;;)
                {
                    i++;
                    if (!parseExpression(state, &estateArgument, false))
                    {
                        free(estate->arguments);
                        if (constant)
                        {
                            IVDispose(&values);
                        }
                        return false;
                    }
                    if (constant)
                    {
                        if (estateArgument.expressionType == EXPRESSION_CONSTANT)
                        {
                            IVAdd(&values, estateArgument.field);
                        }
                        else
                        {
                            constant = false;
                            IVDispose(&values);
                        }
                    }
                    if (!finishRValue(state, &estateArgument))
                    {
                        if (constant)
                        {
                            IVDispose(&values);
                        }
                        return false;
                    }
                    if (peekOperator(state, ')'))
                    {
                        break;
                    }
                    if (!readExpectedOperator(state, ','))
                    {
                        free(estate->arguments);
                        if (constant)
                        {
                            IVDispose(&values);
                        }
                        return false;
                    }
                    skipWhitespace(state);

                    estateArgument.identifier = peekReadIdentifier(state);
                    if (estateArgument.identifier && peekOperator(state, ':'))
                    {
                        break;
                    }
                }
                if (constant)
                {
                    BVSetSize(state->bytecode, bytecodeSize);
                    estate->arguments[varargIndex] =
                        ((int)FieldIndexGetIndex(
                            FieldIndexAddListConstant(&values)) << 2) + 1;
                    IVDispose(&values);
                }
                else
                {
                    ParseStateWriteList(state, i);
                    estate->argumentCount++;
                    estate->arguments[varargIndex] = -(int)estate->argumentCount;
                }
                if (readOperator(state, ')'))
                {
                    break;
                }
                continue;
            }
            else
            {
                argumentIndex = argumentCount;
            }

            argumentCount++;
            if (!parseExpression(state, &estateArgument, false))
            {
                free(estate->arguments);
                return false;
            }
            if (estateArgument.expressionType == EXPRESSION_VARIABLE)
            {
                if (argumentIndex < parameterCount)
                {
                    variableIndex = ParseStateGetVariableIndex(
                        state, estateArgument.valueIdentifier);
                    if (variableIndex < 0)
                    {
                        free(estate->arguments);
                        return false;
                    }
                    estate->arguments[argumentIndex] = (variableIndex << 2) + 3;
                }
            }
            else if (estateArgument.expressionType == EXPRESSION_CONSTANT)
            {
                if (argumentIndex < parameterCount)
                {
                    estate->arguments[argumentIndex] =
                        ((int)FieldIndexGetIndex(estateArgument.field) << 2) + 1;
                }
            }
            else if (finishRValue(state, &estateArgument))
            {
                if (argumentIndex < parameterCount)
                {
                    estate->argumentCount++;
                    estate->arguments[argumentIndex] = -(int16)estate->argumentCount;
                }
            }
            else
            {
                free(estate->arguments);
                return false;
            }
            if (readOperator(state, ')'))
            {
                break;
            }
            if (!readExpectedOperator(state, ','))
            {
                free(estate->arguments);
                return false;
            }
            skipWhitespace(state);
            estateArgument.identifier = peekReadIdentifier(state);
        }
    }
    if (argumentCount > parameterCount)
    {
        if (!parameterCount)
        {
            errorOnLine(state, line,
                        "Function '%s' does not take any arguments.",
                        StringPoolGetString(name));
        }
        else
        {
            errorOnLine(
                state, line,
                "Too many arguments for function '%s'. Got %d arguments, but at most %d were expected.",
                StringPoolGetString(name), argumentCount, parameterCount);
        }
        free(estate->arguments);
        return false;
    }

    for (i = 0; i < parameterCount; i++)
    {
        position = estate->arguments[i];
        if (position)
        {
            if (position < 0)
            {
                estate->arguments[i] =
                    -position - (int)estate->argumentCount - 1;
            }
            else
            {
                estate->arguments[i] >>= 1;
            }
        }
        else if (parameterInfo[i].value)
        {
            estate->arguments[i] =
                (int)FieldIndexGetIndex(parameterInfo[i].value) << 1;
        }
        else if (i == varargIndex)
        {
            estate->arguments[i] = FIELD_EMPTY_LIST << 1;
        }
        else
        {
            free(estate->arguments);
            errorOnLine(state, line, "No value for parameter '%s' given.",
                        StringPoolGetString(parameterInfo[i].name));
            return false;
        }
    }
    return true;
}

static boolean parseNativeInvocationRest(ParseState *state,
                                         ExpressionState *estate,
                                         stringref name)
{
    nativefunctionref function = NativeFindFunction(name);
    uint parameterCount;

    ParseStateCheck(state);
    if (!function)
    {
        statementError(state, "Unknown native function '%s'.",
                       StringPoolGetString(name));
        return false;
    }
    parameterCount = NativeGetParameterCount(function);
    estate->expressionType = EXPRESSION_NATIVE_INVOCATION;
    estate->valueType = VALUE_UNKNOWN;
    estate->nativeFunction = function;
    estate->argumentCount = parameterCount;

    assert(parameterCount);
    while (parameterCount--)
    {
        if (!parseRValue(state, false) ||
            !readExpectedOperator(state, parameterCount ? ',' : ')'))
        {
            return false;
        }
        skipWhitespace(state);
    }
    return true;
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

static boolean parseExpression12(ParseState *state, ExpressionState *estate)
{
    ExpressionState estate2;
    stringref identifier = estate->identifier;
    stringref string;
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
                estate->field = FIELD_TRUE + 1;
                return true;
            }
            else if (identifier == keywordFalse)
            {
                estate->valueType = VALUE_BOOLEAN;
                estate->field = FIELD_FALSE + 1;
                return true;
            }
            else if (identifier == keywordNull)
            {
                estate->valueType = VALUE_NULL;
                estate->field = FIELD_NULL + 1;
                return true;
            }
            statementError(state, "Unexpected keyword '%s'.",
                           StringPoolGetString(identifier));
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
                               StringPoolGetString(identifier));
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
                               StringPoolGetString(NamespaceGetName(ns)),
                               StringPoolGetString(identifier));
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
        estate->field = FieldIndexAddStringConstant(string);
        estate->expressionType = EXPRESSION_CONSTANT;
        estate->valueType = VALUE_STRING;
        return true;
    }
    if (readOperator(state, '('))
    {
        skipWhitespace(state);
        if (!parseExpression(state, estate, estate->parseConstant) ||
            !finishRValue(state, estate))
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
            estate->field = FIELD_EMPTY_LIST + 1;
            return true;
        }
        size = 0;
        bytecodeSize = BVSize(state->bytecode);
        constant = true;
        IVInit(&values, 16);
        do
        {
            size++;
            skipWhitespace(state);
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
                    IVAdd(&values, estate2.field);
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
        while (readOperator(state, ','));
        if (constant)
        {
            BVSetSize(state->bytecode, bytecodeSize);
            estate->expressionType = EXPRESSION_CONSTANT;
            estate->field = FieldIndexAddListConstant(&values);
            IVDispose(&values);
        }
        else
        {
            ParseStateWriteList(state, size);
        }
        skipWhitespace(state);
        return readExpectedOperator(state, '}');
    }
    if (readOperator(state, '@'))
    {
        string = readFilename(state);
        if (!string)
        {
            return false;
        }
        estate->valueType = VALUE_FILE;
        if (!strchr(StringPoolGetString(string), '*'))
        {
            estate->field = FieldIndexAddFileConstant(string);
            estate->expressionType = EXPRESSION_CONSTANT;
            return true;
        }
        /* TODO: @{} syntax */
        ParseStateWriteFileset(state, string);
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
        skipWhitespace(state);
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
        skipWhitespace(state);
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
        skipWhitespace(state);
        estate->expressionType = EXPRESSION_SIMPLE;
        estate->valueType = VALUE_NUMBER;
        return true;
    }
    if (!parseExpression10(state, estate))
    {
        return false;
    }
    skipWhitespace(state);
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
            skipWhitespace(state);
            if (!finishBoolean(state, estate))
            {
                return false;
            }
            ParseStateWriteInstruction(state, OP_DUP);
            ParseStateBeginForwardJump(state, OP_BRANCH_FALSE, &branch);
            ParseStateWriteInstruction(state, OP_POP);
            if (!parseExpression3(state, estate) ||
                !finishBoolean(state, estate))
            {
                return false;
            }
            ParseStateFinishJump(state, branch);
            estate->expressionType = EXPRESSION_SIMPLE;
            estate->valueType = VALUE_BOOLEAN;
            skipWhitespace(state);
            continue;
        }
        if (readOperator2(state, '|', '|'))
        {
            skipWhitespace(state);
            if (!finishBoolean(state, estate))
            {
                return false;
            }
            ParseStateWriteInstruction(state, OP_DUP);
            ParseStateBeginForwardJump(state, OP_BRANCH_TRUE, &branch);
            ParseStateWriteInstruction(state, OP_POP);
            if (!parseExpression3(state, estate) ||
                !finishBoolean(state, estate))
            {
                return false;
            }
            ParseStateFinishJump(state, branch);
            estate->expressionType = EXPRESSION_SIMPLE;
            estate->valueType = VALUE_BOOLEAN;
            skipWhitespace(state);
            continue;
        }
        break;
    }
    return true;
}

static boolean parseExpression(ParseState *state, ExpressionState *estate,
                               boolean constant)
{
    estate->parseConstant = constant;
    if (!parseExpression2(state, estate))
    {
        return false;
    }
    if (readOperator(state, '?'))
    {
        skipWhitespace(state);
        /* TODO: Avoid recursion. */
        if (!finishBoolean(state, estate))
        {
            return false;
        }
        ParseStateWriteBeginCondition(state);
        if (!parseRValue(state, constant) ||
            !readExpectedOperator(state, ':') ||
            !ParseStateWriteSecondConsequent(state))
        {
            return false;
        }
        skipWhitespace(state);
        if (!parseRValue(state, constant) ||
            !ParseStateWriteFinishCondition(state))
        {
            return false;
        }
        estate->expressionType = EXPRESSION_SIMPLE;
        estate->valueType = VALUE_UNKNOWN;
        return true;
    }
    return true;
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
    while (readOperator(state, ','));
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
        ParseStateWriteInvocation(
            state, estate.function, estate.argumentCount, estate.arguments,
            returnValueCount);
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
                                        stringref identifier)
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
    size_t target;
    uint16 iterCollection;
    uint16 iterIndex;

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
            else if (!indent)
            {
                return true;
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
                        if (!parseBooleanValue(state))
                        {
                            return false;
                        }
                        if (!peekNewline(state))
                        {
                            error(state, "Garbage after if statement.");
                            return false;
                        }
                        skipEndOfLine(state);
                        ParseStateWriteIf(state);
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
                        if (!identifier)
                        {
                            return false;
                        }
                        skipWhitespace(state);
                        if (!ParseStateCreateUnnamedVariable(state, &iterCollection) ||
                            !ParseStateCreateUnnamedVariable(state, &iterIndex) ||
                            !readExpectedKeyword(state, keywordIn))
                        {
                            return false;
                        }
                        skipWhitespace(state);
                        if (!parseRValue(state, false))
                        {
                            return false;
                        }
                        ParseStateSetUnnamedVariable(state, iterCollection);
                        ParseStateGetField(state, FieldIndexAddIntegerConstant(-1));
                        ParseStateSetUnnamedVariable(state, iterIndex);
                        if (!peekNewline(state))
                        {
                            error(state, "Garbage after for statement.");
                            return false;
                        }
                        skipEndOfLine(state);
                        target = ParseStateGetJumpTarget(state);
                        ParseStateGetUnnamedVariable(state, iterCollection);
                        ParseStateGetUnnamedVariable(state, iterIndex);
                        ParseStateGetField(state, FieldIndexAddIntegerConstant(1));
                        ParseStateWriteInstruction(state, OP_ADD);
                        ParseStateWriteInstruction(state, OP_DUP);
                        ParseStateSetUnnamedVariable(state, iterIndex);
                        ParseStateWriteInstruction(state, OP_ITER_GET);
                        if (!ParseStateSetVariable(state, identifier))
                        {
                            return false;
                        }
                        ParseStateWriteWhile(state, target);
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
                        if (!parseBooleanValue(state))
                        {
                            return false;
                        }
                        if (!peekNewline(state))
                        {
                            error(state, "Garbage after while statement.");
                            return false;
                        }
                        skipEndOfLine(state);
                        ParseStateWriteWhile(state, target);
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

static boolean parseFunctionDeclaration(ParseState *state, functionref function)
{
    ExpressionState estate;
    stringref parameterName;
    fieldref field = 0;
    boolean vararg;
    boolean requireDefaultValues = false;
    size_t start;

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
                if (readOperator(state, '='))
                {
                    requireDefaultValues = true;
                    skipWhitespace(state);
                    start = BVSize(state->bytecode);
                    estate.identifier = 0;
                    if (!parseExpression(state, &estate, true))
                    {
                        return false;
                    }
                    if (estate.expressionType == EXPRESSION_CONSTANT)
                    {
                        field = estate.field;
                    }
                    else
                    {
                        if (!finishRValue(state, &estate))
                        {
                            return false;
                        }
                        field = FieldIndexAddConstant(
                            state->ns,
                            state->filename, state->line,
                            getOffset(state, state->start),
                            state->bytecode, start);
                    }
                }
                else if (requireDefaultValues)
                {
                    error(state, "Default value for parameter '%s' required.",
                          StringPoolGetString(parameterName));
                    return false;
                }
                else if (readOperator3(state, '.', '.', '.'))
                {
                    requireDefaultValues = true;
                    skipWhitespace(state);
                    vararg = true;
                }
                FunctionIndexAddParameter(function, parameterName,
                                          field, vararg);
                if (readOperator(state, ')'))
                {
                    break;
                }
                if (!readOperator(state, ','))
                {
                    error(state, "Expected ',' or ')'.");
                    return false;
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
    stringref name;
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

void ParseFile(stringref filename, namespaceref ns)
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
    uint line;

    assert(function);
    line = FunctionIndexGetLine(function);
    if (!line)
    {
        return;
    }
    ParseStateInit(&state, bytecode,
                   FunctionIndexGetNamespace(function),
                   function,
                   FunctionIndexGetFilename(function),
                   line,
                   FunctionIndexGetFileOffset(function));
    if (parseFunctionBody(&state))
    {
        if (BVSize(bytecode) == start)
        {
            ParseStateWriteReturnVoid(&state);
        }
        FunctionIndexSetBytecodeOffset(function, start);
    }
    ParseStateDispose(&state);
}
