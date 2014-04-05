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
#include "stringpool.h"
#include "util.h"

typedef struct
{
    const byte *start;
    const byte *current;
    const byte *limit;
    namespaceref ns;
    File fh;
    vref filename;
    uint line;
    uint statementLine;
    uint statementIndent;

    /* Named local variables. */
    inthashmap locals;
    /* Number of named+anonymous local variables. */
    uint localsCount;

    bytevector *bytecode;
    boolean error;
} ParseState;

typedef enum
{
    EXPRESSION_CONSTANT,
    EXPRESSION_STORED,
    EXPRESSION_MISSING_STORE,
    EXPRESSION_VARIABLE,
    EXPRESSION_FIELD,
    EXPRESSION_MANY
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
    uint index;
    vref valueIdentifier;
    vref constant;
    fieldref field;
    nativefunctionref nativeFunction;
    functionref function;
    uint valueCount;
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

static intvector temp;
static bytevector btemp;


static boolean parseExpression(ParseState *state, ExpressionState *estate,
                               uint valueCount, boolean constant);
static boolean parseUnquotedExpression(ParseState *state,
                                       ExpressionState *estate,
                                       boolean constant);


static size_t getOffset(const ParseState *state, const byte *begin)
{
    return (size_t)(state->current - begin);
}

static size_t getColumnOffset(const ParseState *state, const byte *current)
{
    const byte *lineBegin = current;
    if (lineBegin == state->start)
    {
        return 0;
    }
    do
    {
        lineBegin--;
    }
    while (lineBegin != state->start && *lineBegin != '\n');
    return (size_t)(current - lineBegin - 1);
}

static boolean eof(const ParseState *state)
{
    return state->current == state->limit;
}


static attrprintf(2, 3) void error(ParseState *state, const char *format, ...)
{
    va_list args;

    state->error = true;
    va_start(args, format);
    LogParseError(state->filename, state->line, format, args);
    va_end(args);
}

static attrprintf(2, 3) void errorExpected(ParseState *state, const char *format, ...)
{
    va_list args;
    const byte *before = state->current;
    if (before != state->start)
    {
        for (;;)
        {
            before--;
            if (before == state->start)
            {
                break;
            }
            if (*before != ' ')
            {
                before++;
                break;
            }
        }
    }

    state->error = true;
    va_start(args, format);
    LogParseErrorWithColumn(state->filename, state->line, getColumnOffset(state, before),
                            format, args);
    va_end(args);
}

static attrprintf(3, 4) void errorOnLine(ParseState *state, size_t line,
                                         const char *format, ...)
{
    va_list args;

    state->error = true;
    va_start(args, format);
    LogParseError(state->filename, line, format, args);
    va_end(args);
}

static attrprintf(2, 3) void statementError(ParseState *state,
                                            const char *format, ...)
{
    va_list args;

    state->error = true;
    va_start(args, format);
    LogParseError(state->filename, state->statementLine, format, args);
    va_end(args);
}


static void writeOp(bytevector *bytecode, Instruction op, uint param)
{
    assert(((param << 8) >> 8) == param);
    BVAddUint(bytecode, op | (param << 8));
}


static uint variableIndex(ParseState *state, vref name)
{
    uint local = IntHashMapGet(&state->locals, uintFromRef(name));
    if (local)
    {
        return local - 1;
    }
    assert(state->localsCount < INT_MAX); /* TODO */
    local = state->localsCount++;
    IntHashMapAdd(&state->locals, uintFromRef(name), (uint)(local + 1));
    return local;
}

static uint createVariable(ParseState *state)
{
    assert(state->localsCount < INT_MAX); /* TODO */
    return state->localsCount++;
}


static size_t writeForwardJump(ParseState *state, Instruction instruction)
{
    writeOp(state->bytecode, instruction, 0);
    BVAddInt(state->bytecode, 0);
    return BVSize(state->bytecode);
}

static size_t writeForwardBranch(ParseState *state, Instruction instruction,
                                 uint variable)
{
    writeOp(state->bytecode, instruction, variable);
    BVAddInt(state->bytecode, 0);
    return BVSize(state->bytecode);
}

static void finishJump(ParseState *state, size_t branch)
{
    BVSetUint(state->bytecode, branch - sizeof(int), (uint)(BVSize(state->bytecode) - branch));
}

static void writeBackwardJump(ParseState *state, Instruction instruction, size_t target)
{
    writeOp(state->bytecode, instruction, 0);
    BVAddInt(state->bytecode, (int)(target - BVSize(state->bytecode) - sizeof(int)));
}

static int setJumpOffset(ParseState *state, size_t instructionOffset, int offset)
{
    int old = BVGetInt(state->bytecode, instructionOffset - sizeof(int));
    BVSetInt(state->bytecode, instructionOffset - sizeof(int), offset);
    return old;
}


static void storeConstant(ParseState *state, vref value, uint index)
{
    if (!value)
    {
        writeOp(state->bytecode, OP_NULL, index);
    }
    else if (value == HeapTrue)
    {
        writeOp(state->bytecode, OP_TRUE, index);
    }
    else if (value == HeapFalse)
    {
        writeOp(state->bytecode, OP_FALSE, index);
    }
    else if (value == HeapEmptyList)
    {
        writeOp(state->bytecode, OP_EMPTY_LIST, index);
    }
    else
    {
        writeOp(state->bytecode, OP_PUSH, index);
        BVAddUint(state->bytecode, uintFromRef(value));
    }
}

static uint variableFromConstant(ParseState *state, vref value)
{
    uint index = createVariable(state);
    storeConstant(state, value, index);
    return index;
}


static void parsedConstant(ExpressionState *estate, ValueType valueType, vref constant)
{
    estate->expressionType = EXPRESSION_CONSTANT;
    estate->valueType = valueType;
    estate->constant = constant;
}


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


static void skipWhitespace(ParseState *state)
{
    while (*state->current == ' ')
    {
        state->current++;
    }
}

static void skipEndOfLine(ParseState *state)
{
    while (!eof(state) && *state->current++ != '\n');
    state->line++;
}

static boolean peekNewline(ParseState *state)
{
    return *state->current == '\n' || *state->current == '#';
}

static boolean peekReadNewline(ParseState *state)
{
    if (*state->current == '\n')
    {
        state->current++;
        state->line++;
        return true;
    }
    return false;
}

static uint readNewline(ParseState *state)
{
    if (*state->current != '\n')
    {
        while (*state->current == ' ')
        {
            state->current++;
        }
        if (*state->current != '#')
        {
            errorExpected(state, "Expected linebreak");
        }
        do
        {
            state->current++;
        }
        while (*state->current != '\n');
    }

    for (;;)
    {
        const byte *lineBegin;

        do
        {
            state->current++;
            state->line++;
            if (state->current == state->limit)
            {
                return 0;
            }
        }
        while (*state->current == '\n');

        lineBegin = state->current;
        while (*state->current == ' ')
        {
            state->current++;
        }
        if (*state->current == '#')
        {
            do
            {
                state->current++;
            }
            while (*state->current != '\n');
        }
        else if (*state->current != '\n')
        {
            return (uint)getOffset(state, lineBegin);
        }
    }
}

static boolean skipWhitespaceAndNewline(ParseState *state)
{
    skipWhitespace(state);
    if (peekNewline(state) && readNewline(state) <= state->statementIndent)
    {
        error(state, "Expected increased indentation for continued line");
        return false;
    }
    return true;
}

static void skipExpressionWhitespace(ParseState *state, ExpressionState *estate)
{
    if (estate->allowSpace)
    {
        skipWhitespace(state);
    }
}

static void skipExpressionWhitespaceAndNewline(ParseState *state,
                                               ExpressionState *estate)
{
    if (estate->allowSpace)
    {
        skipWhitespaceAndNewline(state);
    }
}

static uint skipStatement(ParseState *state)
{
    uint indent;
    do
    {
        while (*state->current != '\n')
        {
            state->current++;
        }
        indent = readNewline(state);
    }
    while (indent > state->statementIndent);
    return indent;
}

static boolean peekIndent(const ParseState *state)
{
    return *state->current == ' ';
}

static boolean peekComment(const ParseState *state)
{
    return *state->current == '#';
}

static boolean isKeyword(vref identifier)
{
    return identifier <= maxKeyword;
}

static boolean peekIdentifier(const ParseState *state)
{
    return isInitialIdentifierCharacter(*state->current);
}

static vref readIdentifier(ParseState *state)
{
    const byte *begin = state->current;

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

static boolean peekReadKeywordElse(ParseState *state)
{
    if (state->current[0] == 'e' &&
        state->current[1] == 'l' &&
        state->current[2] == 's' &&
        state->current[3] == 'e' &&
        !isIdentifierCharacter(state->current[4]))
    {
        state->current += 4;
        return true;
    }
    return false;
}

static vref readVariableName(ParseState *state)
{
    vref identifier = peekReadIdentifier(state);
    if (!identifier || isKeyword(identifier))
    {
        error(state, "Expected variable name");
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
    statementError(state, "Expected keyword %s", HeapGetString(keyword));
    return false;
}

static boolean isDigit(byte b)
{
    return b >= '0' && b <= '9';
}

static boolean peekNumber(const ParseState *state)
{
    return isDigit(*state->current);
}

static boolean peekString(const ParseState *state)
{
    return *state->current == '"';
}

static vref readString(ParseState *state)
{
    bytevector string;
    boolean copied = false;
    const byte *begin;
    vref s;

    assert(peekString(state));
    begin = ++state->current;
    for (;;)
    {
        assert(!eof(state)); /* TODO: error handling */
        switch (*state->current)
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
            switch (*state->current)
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
                error(state, "Invalid escape sequence");
                break;
            }
            state->current++;
            begin = state->current;
            break;

        case '\r':
        case '\n':
            error(state, "Newline in string literal");
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
    begin = state->current;
    while (isFilenameCharacter(*state->current))
    {
        assert(!eof(state)); /* TODO: error handling */
        assert(!peekNewline(state)); /* TODO: error handling */
        state->current++;
    }
    if (begin == state->current)
    {
        error(state, "Expected filename");
        return 0;
    }
    return StringPoolAdd2((const char*)begin, getOffset(state, begin));
}

static boolean readOperator(ParseState *state, byte op)
{
    if (*state->current == op)
    {
        state->current++;
        return true;
    }
    return false;
}

static boolean peekOperator(ParseState *state, byte op)
{
    return *state->current == op;
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
        error(state, "Expected operator '%c'. Got '%c'", op, *state->current);
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
        value = value * 10 + *state->current - '0';
        assert(value >= 0);
        state->current++;
    }
    while (isDigit(*state->current));

    if (isIdentifierCharacter(*state->current))
    {
        error(state, "Invalid character in number literal");
        return false;
    }

    parsedConstant(estate, VALUE_NUMBER, HeapBoxInteger(value));
    return true;
}


static uint createList(ParseState *state, const uint *values, uint length)
{
    uint index;
    if (!length)
    {
        return variableFromConstant(state, HeapEmptyList);
    }
    index = createVariable(state);
    writeOp(state->bytecode, OP_LIST, length);
    BVAddData(state->bytecode, (const byte*)values, length * sizeof(*values));
    BVAddUint(state->bytecode, index);
    return index;
}

static void convertConstantsToValues(ParseState *state, uint *values, size_t count)
{
    while (count--)
    {
        uint index = variableFromConstant(state, (vref)*values);
        *values++ = index;
    }
}

static void finishAndStoreValueAt(ParseState *state, const ExpressionState *estate, uint index)
{
    uint index2;
    switch (estate->expressionType)
    {
    case EXPRESSION_CONSTANT:
        storeConstant(state, estate->constant, index);
        return;

    case EXPRESSION_STORED:
        if (estate->index != index)
        {
            writeOp(state->bytecode, OP_COPY, estate->index);
            BVAddUint(state->bytecode, index);
        }
        return;

    case EXPRESSION_MANY:
        assert(estate->valueCount == 1);
    case EXPRESSION_MISSING_STORE:
        BVAddUint(state->bytecode, index);
        return;

    case EXPRESSION_VARIABLE:
        index2 = variableIndex(state, estate->valueIdentifier);
        if (index != index2)
        {
            writeOp(state->bytecode, OP_COPY, index2);
            BVAddUint(state->bytecode, index);
        }
        return;

    case EXPRESSION_FIELD:
        writeOp(state->bytecode, OP_LOAD_FIELD, index);
        BVAddUint(state->bytecode, FieldIndexGetIndex(estate->field));
        return;
    }
    assert(false);
}

static uint finishRValue(ParseState *state, const ExpressionState *estate)
{
    uint index;

    switch (estate->expressionType)
    {
    case EXPRESSION_CONSTANT:
        return variableFromConstant(state, estate->constant);

    case EXPRESSION_STORED:
        return estate->index;

    case EXPRESSION_MANY:
        assert(estate->valueCount == 1);
    case EXPRESSION_MISSING_STORE:
        index = createVariable(state);
        BVAddUint(state->bytecode, index);
        return index;

    case EXPRESSION_VARIABLE:
        return variableIndex(state, estate->valueIdentifier);

    case EXPRESSION_FIELD:
        index = createVariable(state);
        writeOp(state->bytecode, OP_LOAD_FIELD, index);
        BVAddUint(state->bytecode, FieldIndexGetIndex(estate->field));
        return index;
    }
    assert(false);
    return 0;
}

static int parseRValue(ParseState *state, boolean constant)
{
    ExpressionState estate;

    estate.identifier = 0;
    if (!parseExpression(state, &estate, 1, constant))
    {
        return -1;
    }
    return (int)finishRValue(state, &estate);
}

static boolean parseAndStoreValueAt(ParseState *state, uint index)
{
    ExpressionState estate;

    estate.identifier = 0;
    if (!parseExpression(state, &estate, 1, false))
    {
        return false;
    }
    finishAndStoreValueAt(state, &estate, index);
    return true;
}

static boolean finishLValue(ParseState *state, const ExpressionState *lvalue,
                            const ExpressionState *rvalue)
{
    uint index;
    switch (lvalue->expressionType)
    {
    case EXPRESSION_CONSTANT:
    case EXPRESSION_STORED:
    case EXPRESSION_MISSING_STORE:
    case EXPRESSION_MANY:
        statementError(state, "Invalid target for assignment");
        return false;

    case EXPRESSION_VARIABLE:
        finishAndStoreValueAt(state, rvalue, variableIndex(state, lvalue->valueIdentifier));
        return true;

    case EXPRESSION_FIELD:
        index = finishRValue(state, rvalue);
        writeOp(state->bytecode, OP_STORE_FIELD, index);
        BVAddUint(state->bytecode, FieldIndexGetIndex(lvalue->field));
        return true;
    }
    assert(false);
    return false;
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
            statementError(state, "Unknown function '%s.%s'",
                           HeapGetString(NamespaceGetName(ns)),
                           HeapGetString(name));
        }
    }
    else
    {
        function = NamespaceLookupFunction(state->ns, name);
        if (!function)
        {
            statementError(state, "Unknown function '%s'",
                           HeapGetString(name));
        }
    }
    return function;
}

static void finishUnnamedArguments(ParseState *state, uint parameterCount,
                                   uint argumentCount, uint varargIndex)
{
    if (argumentCount > varargIndex)
    {
        uint length = argumentCount - varargIndex;
        uint list = createList(state, IVGetPointer(&temp, IVSize(&temp) - length), length);
        IVSetSize(&temp, IVSize(&temp) - length + 1);
        IVSet(&temp, IVSize(&temp) - 1, list);
        argumentCount = varargIndex + 1;
    }
    while (argumentCount++ < parameterCount)
    {
        IVAdd(&temp, (uint)-1);
    }
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
    uint i;
    size_t oldTempSize = IVSize(&temp);

    assert(!estate->identifier);
    function = lookupFunction(state, ns, name);
    if (!function)
    {
        goto error;
    }
    parameterCount = FunctionIndexGetParameterCount(function);
    requiredArgumentCount = FunctionIndexGetRequiredArgumentCount(function);
    parameterInfo = FunctionIndexGetParameterInfo(function);
    varargIndex = FunctionIndexHasVararg(function) ?
        FunctionIndexGetVarargIndex(function) : UINT_MAX;

    estate->expressionType = EXPRESSION_MANY;
    estate->valueType = VALUE_UNKNOWN;
    estate->function = function;

    for (;;)
    {
        int value;
        if (!skipWhitespaceAndNewline(state))
        {
            goto error;
        }
        if (readOperator(state, ')'))
        {
            finishUnnamedArguments(state, parameterCount, argumentCount, varargIndex);
            break;
        }
        if (!skipWhitespaceAndNewline(state))
        {
            goto error;
        }
        estateArgument.identifier = peekReadIdentifier(state);
        if (estateArgument.identifier && readOperator(state, ':'))
        {
            finishUnnamedArguments(state, parameterCount, argumentCount, varargIndex);
            for (;;)
            {
                uint index;
                for (index = 0;; index++)
                {
                    if (index == parameterCount)
                    {
                        errorOnLine(state, line, "No parameter named '%s'",
                                    HeapGetString(estateArgument.identifier));
                        goto error;
                    }
                    if (parameterInfo[index].name == estateArgument.identifier)
                    {
                        break;
                    }
                }
                if ((int)IVGet(&temp, oldTempSize + index) != -1)
                {
                    errorOnLine(state, line, "Multiple values for parameter '%s'",
                                HeapGetString(estateArgument.identifier));
                    goto error;
                }
                estateArgument.identifier = 0;
                value = parseRValue(state, false);
                if (value < 0 || !skipWhitespaceAndNewline(state))
                {
                    goto error;
                }
                IVSet(&temp, oldTempSize + index, (uint)value);
                estateArgument.identifier = peekReadIdentifier(state);
                if (!estateArgument.identifier)
                {
                    break;
                }
                if (!readExpectedOperator(state, ':'))
                {
                    goto error;
                }
            }
            if (!readExpectedOperator(state, ')'))
            {
                goto error;
            }
            break;
        }
        if (!parseExpression(state, &estateArgument, 1, false))
        {
            goto error;
        }
        IVAdd(&temp, finishRValue(state, &estateArgument));
        argumentCount++;
    }

    for (i = 0; i < parameterCount; i++)
    {
        if ((int)IVGet(&temp, oldTempSize + i) < 0)
        {
            if (i != varargIndex && i < requiredArgumentCount)
            {
                errorOnLine(state, line, "No value for parameter '%s' given",
                            HeapGetString(parameterInfo[i].name));
                goto error;
            }
            IVSet(&temp, oldTempSize + i,
                  variableFromConstant(state, parameterInfo[i].value));
        }
    }

    writeOp(state->bytecode, OP_INVOKE, uintFromRef(function));
    BVAddUint(state->bytecode, parameterCount);
    BVAddData(state->bytecode, (const byte*)IVGetPointer(&temp, oldTempSize),
              parameterCount * sizeof(int));
    BVAddUint(state->bytecode, estate->valueCount);
    IVSetSize(&temp, oldTempSize);
    return true;

error:
    IVSetSize(&temp, oldTempSize);
    return false;
}

static boolean parseNativeInvocationRest(ParseState *state,
                                         ExpressionState *estate,
                                         vref name)
{
    nativefunctionref function = NativeFindFunction(name);
    uint argumentCount;
    uint i;
    size_t oldTempSize = IVSize(&temp);

    if (!function)
    {
        statementError(state, "Unknown native function '%s'",
                       HeapGetString(name));
        return false;
    }
    if (estate->valueCount != NativeGetReturnValueCount(function))
    {
        statementError(state, "Native function returns %d values, but %d are handled",
                       NativeGetReturnValueCount(function), estate->valueCount);
        return false;
    }
    argumentCount = NativeGetParameterCount(function);
    estate->expressionType = EXPRESSION_MANY;
    estate->valueType = VALUE_UNKNOWN;
    estate->nativeFunction = function;

    for (i = 0; i < argumentCount; i++)
    {
        int value = parseRValue(state, false);
        if (value < 0)
        {
            IVSetSize(&temp, oldTempSize);
            return false;
        }
        IVAdd(&temp, (uint)value);
        skipWhitespace(state);
    }
    argumentCount = (uint)(IVSize(&temp) - oldTempSize);
    writeOp(state->bytecode, OP_INVOKE_NATIVE, uintFromRef(function));
    BVAddData(state->bytecode, (const byte*)IVGetPointer(&temp, oldTempSize),
              argumentCount * sizeof(int));
    IVSetSize(&temp, oldTempSize);
    return readExpectedOperator(state, ')');
}

static boolean parseBinaryOperationRest(
    ParseState *state, ExpressionState *estate,
    boolean (*parseExpressionRest)(ParseState*, ExpressionState*),
    Instruction instruction, ValueType valueType)
{
    uint value = finishRValue(state, estate);
    uint value2;
    skipExpressionWhitespace(state, estate);
    if (!parseExpressionRest(state, estate))
    {
        return false;
    }
    value2 = finishRValue(state, estate);
    writeOp(state->bytecode, instruction, value);
    BVAddUint(state->bytecode, value2);
    estate->expressionType = EXPRESSION_MISSING_STORE;
    estate->valueType = valueType;
    skipExpressionWhitespace(state, estate);
    return true;
}

static boolean parseQuotedValue(ParseState *state, ExpressionState *estate)
{
    static const char terminators[] = " \n\r(){}[]";
    const byte *begin = state->current;

    assert(!estate->identifier);
    while (!eof(state) &&
           !memchr(terminators, *state->current, sizeof(terminators)))
    {
        state->current++;
    }
    if (state->current == begin)
    {
        error(state, "Invalid quoted value");
        return false;
    }
    parsedConstant(estate, VALUE_STRING,
                   StringPoolAdd2((const char*)begin, getOffset(state, begin)));
    return true;
}

static boolean parseQuotedListRest(ParseState *state, ExpressionState *estate)
{
    ExpressionState estate2;
    boolean constant = true;
    size_t oldTempSize = IVSize(&temp);

    assert(!estate->identifier);
    if (!skipWhitespaceAndNewline(state))
    {
        return false;
    }
    while (!readOperator(state, '}'))
    {
        estate2.identifier = 0;
        if (readOperator(state, '$'))
        {
            if (!parseUnquotedExpression(state, &estate2, estate->parseConstant))
            {
                goto error;
            }
        }
        else if (!parseQuotedValue(state, &estate2))
        {
            goto error;
        }
        if (constant)
        {
            if (estate2.expressionType == EXPRESSION_CONSTANT)
            {
                IVAdd(&temp, estate2.constant);
            }
            else
            {
                constant = false;
                convertConstantsToValues(state,
                                         IVGetWritePointer(&temp, oldTempSize),
                                         IVSize(&temp) - oldTempSize);
            }
        }
        if (!constant)
        {
            IVAdd(&temp, finishRValue(state, &estate2));
        }
        if (!skipWhitespaceAndNewline(state))
        {
            goto error;
        }
    }
    if (constant)
    {
        parsedConstant(estate, VALUE_LIST, HeapCreateArray(IVGetPointer(&temp, oldTempSize),
                                                           IVSize(&temp) - oldTempSize));
    }
    else
    {
        uint length = (uint)(IVSize(&temp) - oldTempSize);
        writeOp(state->bytecode, OP_LIST, length);
        BVAddData(state->bytecode, (const byte*)IVGetPointer(&temp, oldTempSize),
                  length * sizeof(uint));
        estate->expressionType = EXPRESSION_MISSING_STORE;
        estate->valueType = VALUE_LIST;
    }
    IVSetSize(&temp, oldTempSize);
    return true;

error:
    IVSetSize(&temp, oldTempSize);
    return false;
}

static boolean parseExpression12(ParseState *state, ExpressionState *estate)
{
    ExpressionState estate2;
    vref identifier = estate->identifier;
    vref string;
    namespaceref ns;

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
            if (identifier == keywordTrue)
            {
                parsedConstant(estate, VALUE_BOOLEAN, HeapTrue);
                return true;
            }
            else if (identifier == keywordFalse)
            {
                parsedConstant(estate, VALUE_BOOLEAN, HeapFalse);
                return true;
            }
            else if (identifier == keywordNull)
            {
                parsedConstant(estate, VALUE_BOOLEAN, 0);
                return true;
            }
            statementError(state, "Unexpected keyword '%s'",
                           HeapGetString(identifier));
            return false;
        }
        if (estate->parseConstant)
        {
            statementError(state, "Expected constant");
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
                statementError(state, "Unknown namespace '%s'",
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
                statementError(state, "Unknown field '%s.%s'",
                               HeapGetString(NamespaceGetName(ns)),
                               HeapGetString(identifier));
            }
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
        parsedConstant(estate, VALUE_STRING, string);
        return true;
    }
    if (readOperator(state, '('))
    {
        skipWhitespace(state);
        if (!parseExpression(state, estate, estate->valueCount, estate->parseConstant))
        {
            return false;
        }
        /* TODO: Prevent use as lvalue */
        return readExpectedOperator(state, ')');
    }
    if (readOperator(state, '{'))
    {
        boolean constant;
        size_t oldTempSize = IVSize(&temp);
        skipWhitespace(state);
        if (readOperator(state, '}'))
        {
            parsedConstant(estate, VALUE_LIST, HeapEmptyList);
            return true;
        }
        constant = true;
        do
        {
            estate2.identifier = 0;
            if (!parseExpression(state, &estate2, 1, false))
            {
                IVSetSize(&temp, oldTempSize);
                return false;
            }
            if (constant)
            {
                if (estate2.expressionType == EXPRESSION_CONSTANT)
                {
                    IVAdd(&temp, estate2.constant);
                }
                else
                {
                    constant = false;
                    convertConstantsToValues(
                        state,
                        IVGetWritePointer(&temp, oldTempSize),
                        IVSize(&temp) - oldTempSize);
                }
            }
            if (!constant)
            {
                IVAdd(&temp, finishRValue(state, &estate2));
            }
            skipWhitespace(state);
        }
        while (!readOperator(state, '}'));
        if (constant)
        {
            parsedConstant(estate, VALUE_LIST, HeapCreateArray(IVGetPointer(&temp, oldTempSize),
                                                               IVSize(&temp) - oldTempSize));
        }
        else
        {
            uint length = (uint)(IVSize(&temp) - oldTempSize);
            estate->expressionType = EXPRESSION_MISSING_STORE;
            estate->valueType = VALUE_LIST;
            writeOp(state->bytecode, OP_LIST, length);
            BVAddData(state->bytecode, (const byte*)IVGetPointer(&temp, oldTempSize),
                      length * sizeof(uint));
        }
        IVSetSize(&temp, oldTempSize);
        return true;
    }
    if (readOperator(state, '@'))
    {
        string = readFilename(state);
        if (!string)
        {
            return false;
        }
        if (!strchr(HeapGetString(string), '*'))
        {
            parsedConstant(estate, VALUE_FILE, HeapCreatePath(string));
            return true;
        }
        /* TODO: @{} syntax */
        writeOp(state->bytecode, OP_FILELIST, uintFromRef(string));
        estate->expressionType = EXPRESSION_MISSING_STORE;
        estate->valueType = VALUE_FILE;
        return true;
    }
    statementError(state, "Invalid expression");
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
            uint value = finishRValue(state, estate);
            int index;
            skipWhitespace(state);
            index = parseRValue(state, estate->parseConstant);
            if (index < 0)
            {
                return false;
            }
            skipWhitespace(state);
            if (!readExpectedOperator(state, ']'))
            {
                return false;
            }
            writeOp(state->bytecode, OP_INDEXED_ACCESS, value);
            BVAddInt(state->bytecode, index);
            estate->expressionType = EXPRESSION_MISSING_STORE;
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
    uint index;
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
            uint index2 = finishRValue(state, estate);
            writeOp(state->bytecode, OP_CONCAT_STRING, index);
            BVAddUint(state->bytecode, index2);
            estate->expressionType = EXPRESSION_MISSING_STORE;
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
            statementError(state, "Expected constant");
            return false;
        }
        index = finishRValue(state, estate);
        first = false;
    }
}

static boolean parseExpression9(ParseState *state, ExpressionState *estate)
{
    if (readOperator(state, '-'))
    {
        uint value;
        assert(!readOperator(state, '-')); /* TODO: -- operator */
        if (!parseExpression10(state, estate))
        {
            return false;
        }
        value = finishRValue(state, estate);
        writeOp(state->bytecode, OP_NEG, value);
        skipExpressionWhitespace(state, estate);
        estate->expressionType = EXPRESSION_MISSING_STORE;
        estate->valueType = VALUE_NUMBER;
        return true;
    }
    if (readOperator(state, '!'))
    {
        uint value;
        if (!parseExpression10(state, estate))
        {
            return false;
        }
        value = finishRValue(state, estate);
        writeOp(state->bytecode, OP_NOT, value);
        skipExpressionWhitespace(state, estate);
        estate->expressionType = EXPRESSION_MISSING_STORE;
        return true;
    }
    if (readOperator(state, '~'))
    {
        uint value;
        if (!parseExpression10(state, estate))
        {
            return false;
        }
        value = finishRValue(state, estate);
        writeOp(state->bytecode, OP_INV, value);
        skipExpressionWhitespace(state, estate);
        estate->expressionType = EXPRESSION_MISSING_STORE;
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
                 *state->current != ' '))
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
            uint index = createVariable(state);
            finishAndStoreValueAt(state, estate, index);
            skipExpressionWhitespaceAndNewline(state, estate);
            branch = writeForwardBranch(state, OP_BRANCH_FALSE, index);
            if (!parseExpression3(state, estate))
            {
                return false;
            }
            finishAndStoreValueAt(state, estate, index);
            finishJump(state, branch);
            estate->expressionType = EXPRESSION_STORED;
            estate->valueType = VALUE_UNKNOWN;
            estate->index = index;
            skipExpressionWhitespace(state, estate);
            continue;
        }
        if (readOperator2(state, '|', '|'))
        {
            uint index = createVariable(state);
            finishAndStoreValueAt(state, estate, index);
            skipExpressionWhitespaceAndNewline(state, estate);
            branch = writeForwardBranch(state, OP_BRANCH_TRUE, index);
            if (!parseExpression3(state, estate))
            {
                return false;
            }
            finishAndStoreValueAt(state, estate, index);
            finishJump(state, branch);
            estate->expressionType = EXPRESSION_STORED;
            estate->valueType = VALUE_UNKNOWN;
            estate->index = index;
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
        uint index = createVariable(state);
        assert(!parseConstant); /* TODO */
        skipExpressionWhitespace(state, estate);
        offset = writeForwardBranch(state, OP_BRANCH_FALSE, finishRValue(state, estate));
        if (!parseAndStoreValueAt(state, index) || !readExpectedOperator(state, ':'))
        {
            return false;
        }
        offset2 = writeForwardJump(state, OP_JUMP);
        setJumpOffset(state, offset, (int)(BVSize(state->bytecode) - offset));
        skipExpressionWhitespace(state, estate);
        if (!parseAndStoreValueAt(state, index))
        {
            return false;
        }
        setJumpOffset(state, offset2, (int)(BVSize(state->bytecode) - offset2));
        estate->expressionType = EXPRESSION_STORED;
        estate->valueType = VALUE_UNKNOWN;
        estate->index = index;
        return true;
    }
    return true;
}

static boolean parseExpression(ParseState *state, ExpressionState *estate,
                               uint valueCount, boolean constant)
{
    estate->valueCount = valueCount;
    estate->parseConstant = constant;
    estate->allowSpace = true;
    return parseExpressionRest(state, estate);
}

static boolean parseUnquotedExpression(ParseState *state,
                                       ExpressionState *estate,
                                       boolean constant)
{
    estate->valueCount = 1;
    estate->parseConstant = constant;
    estate->allowSpace = false;
    return parseExpressionRest(state, estate);
}

static boolean parseAssignmentExpressionRest(ParseState *state,
                                             ExpressionState *estate,
                                             Instruction instruction)
{
    ExpressionState estate2;
    uint value = finishRValue(state, estate);
    int value2;
    skipWhitespace(state);
    value2 = parseRValue(state, false);
    if (value2 < 0)
    {
        return false;
    }
    writeOp(state->bytecode, instruction, value);
    BVAddInt(state->bytecode, value2);
    estate2.expressionType = EXPRESSION_MISSING_STORE;
    return finishLValue(state, estate, &estate2);
}

static boolean parseExpressionStatement(ParseState *state, vref identifier)
{
    ExpressionState estate, rvalue;

    estate.identifier = identifier;
    if (!parseExpression(state, &estate, 0, false))
    {
        return false;
    }
    if (readOperator(state, '='))
    {
        skipWhitespace(state);
        rvalue.identifier = 0;
        return parseExpression(state, &rvalue, 1, false) && finishLValue(state, &estate, &rvalue);
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
    if (estate.expressionType == EXPRESSION_MANY)
    {
        assert(estate.valueCount == 0);
        return true;
    }
    if (peekIdentifier(state))
    {
        size_t oldTempSize = IVSize(&temp);
        size_t oldBTempSize = BVSize(&btemp);
        size_t p;
        const uint *pindex;
        uint returnValueCount;
        BVAddData(&btemp, (byte*)&estate, sizeof(estate));
        do
        {
            estate.identifier = 0;
            if (!parseExpression(state, &estate, 0, false))
            {
                goto error;
            }
            BVAddData(&btemp, (byte*)&estate, sizeof(estate));
            skipWhitespace(state);
        }
        while (peekIdentifier(state));
        if (!readExpectedOperator(state, '='))
        {
            goto error;
        }
        skipWhitespace(state);
        rvalue.identifier = 0;
        returnValueCount = (uint)((BVSize(&btemp) - oldBTempSize) / sizeof(estate));
        if (!parseExpression(state, &rvalue, returnValueCount, false))
        {
            goto error;
        }
        if (rvalue.expressionType != EXPRESSION_MANY)
        {
            statementError(state, "Expected function invocation");
            goto error;
        }
        assert(rvalue.valueCount == returnValueCount);
        for (p = oldBTempSize; p < BVSize(&btemp); p += sizeof(estate))
        {
            uint index;
            estate = *(const ExpressionState*)BVGetPointer(&btemp, p);
            index = estate.expressionType == EXPRESSION_VARIABLE ?
                variableIndex(state, estate.valueIdentifier) :
                createVariable(state);
            BVAddUint(state->bytecode, index);
            IVAdd(&temp, (uint)index);
        }
        for (p = oldBTempSize, pindex = IVGetPointer(&temp, oldTempSize);
             p < BVSize(&btemp);
             p += sizeof(estate), pindex++)
        {
            estate = *(const ExpressionState*)BVGetPointer(&btemp, p);
            rvalue.expressionType = EXPRESSION_STORED;
            rvalue.index = *pindex;
            if (!finishLValue(state, &estate, &rvalue))
            {
                goto error;
            }
        }
        IVSetSize(&temp, oldTempSize);
        BVSetSize(&btemp, oldBTempSize);
        return true;

error:
        IVSetSize(&temp, oldTempSize);
        BVSetSize(&btemp, oldBTempSize);
        return false;
    }
    statementError(state, "Not a statement");
    return false;
}

static boolean parseReturnRest(ParseState *state)
{
    size_t oldTempSize;
    int value;

    if (peekNewline(state))
    {
        writeOp(state->bytecode, OP_RETURN_VOID, 0);
        return true;
    }
    oldTempSize = IVSize(&temp);
    for (;;)
    {
        value = parseRValue(state, false);
        if (value < 0)
        {
            IVSetSize(&temp, oldTempSize);
            return false;
        }
        IVAdd(&temp, (uint)value);
        if (peekNewline(state))
        {
            uint count = (uint)(IVSize(&temp) - oldTempSize);
            writeOp(state->bytecode, OP_RETURN, count);
            BVAddData(state->bytecode, (const byte*)IVGetPointer(&temp, oldTempSize),
                      count * sizeof(int));
            IVSetSize(&temp, oldTempSize);
            return true;
        }
        skipWhitespace(state);
    }
}

static uint parseBlock(ParseState *state, uint indent)
{
    uint oldStatementIndent = state->statementIndent;
    vref identifier;

    if (indent < state->statementIndent)
    {
        error(state, "Expected increased indentation level");
        return indent;
    }

    state->statementIndent = indent;
    for (;;)
    {
        if (indent != state->statementIndent)
        {
            if (indent > state->statementIndent || (indent && !oldStatementIndent))
            {
                error(state, "Mismatched indentation level");
            }
            else
            {
                break;
            }
        }

        state->statementLine = state->line;

        identifier = peekReadIdentifier(state);
        if (identifier)
        {
            if (isKeyword(identifier))
            {
                if (identifier > maxStatementKeyword)
                {
                    statementError(state, "Not a statement");
                    goto statementError;
                }
                skipWhitespace(state);
                if (identifier == keywordIf)
                {
                    int conditionOffset;
                    int offset = 0;
                    int condition = parseRValue(state, false);
                    if (condition < 0)
                    {
                        /* TODO: Ignore else */
                        goto statementError;
                    }
                    conditionOffset = (int)writeForwardBranch(state,
                                                              OP_BRANCH_FALSE,
                                                              (uint)condition);

                    indent = parseBlock(state, readNewline(state));
                    if (indent != state->statementIndent)
                    {
                        setJumpOffset(state, (size_t)conditionOffset,
                                      (int)BVSize(state->bytecode) - conditionOffset);
                        continue;
                    }

                    if (!peekReadKeywordElse(state))
                    {
                        setJumpOffset(state, (size_t)conditionOffset,
                                      (int)BVSize(state->bytecode) - conditionOffset);
                        continue;
                    }

                    do
                    {
                        writeOp(state->bytecode, OP_JUMP, 0);
                        BVAddInt(state->bytecode, offset);
                        offset = (int)BVSize(state->bytecode);
                        setJumpOffset(state, (size_t)conditionOffset, offset - conditionOffset);
                        skipWhitespace(state);
                        identifier = peekReadIdentifier(state);
                        if (identifier == keywordIf)
                        {
                            skipWhitespace(state);
                            condition = parseRValue(state, false);
                            if (condition < 0)
                            {
                                goto statementError;
                            }
                            if (!peekNewline(state))
                            {
                                error(state, "Garbage after if statement");
                                goto statementError;
                            }
                            conditionOffset = (int)writeForwardBranch(state, OP_BRANCH_FALSE,
                                                                      (uint)condition);
                            indent = parseBlock(state, readNewline(state));
                        }
                        else
                        {
                            if (identifier || !peekNewline(state))
                            {
                                error(state, "Garbage after else");
                                goto statementError;
                            }
                            indent = parseBlock(state, readNewline(state));
                            break;
                        }
                    }
                    while (indent == state->statementIndent && peekReadKeywordElse(state));

                    while (offset)
                    {
                        offset = (int)setJumpOffset(state, (size_t)offset,
                                                    (int)BVSize(state->bytecode) - offset);
                    }
                    continue;
                }
                if (identifier == keywordElse)
                {
                    error(state, "else without matching if");
                    goto statementError;
                }
                else if (identifier == keywordFor)
                {
                    size_t loopTop, afterLoop;
                    int iterCollection;
                    uint iterIndex;
                    uint iterStep;
                    uint iterCondition;
                    identifier = readVariableName(state);
                    if (!identifier)
                    {
                        goto statementError;
                    }
                    skipWhitespace(state);
                    if (!readExpectedKeyword(state, keywordIn))
                    {
                        goto statementError;
                    }
                    skipWhitespace(state);
                    iterCollection = parseRValue(state, false);
                    if (iterCollection < 0)
                    {
                        goto statementError;
                    }
                    iterIndex = createVariable(state);
                    iterStep = createVariable(state);
                    storeConstant(state, HeapBoxInteger(-1), iterIndex);
                    storeConstant(state, HeapBoxInteger(1), iterStep);
                    loopTop = BVSize(state->bytecode);
                    writeOp(state->bytecode, OP_ADD, iterIndex);
                    BVAddUint(state->bytecode, iterStep);
                    BVAddUint(state->bytecode, iterIndex);
                    writeOp(state->bytecode, OP_ITER_GET, (uint)iterCollection);
                    BVAddUint(state->bytecode, iterIndex);
                    BVAddUint(state->bytecode, variableIndex(state, identifier));
                    iterCondition = createVariable(state);
                    BVAddUint(state->bytecode, iterCondition);
                    afterLoop = writeForwardBranch(state, OP_BRANCH_FALSE, iterCondition);
                    indent = parseBlock(state, readNewline(state));
                    writeBackwardJump(state, OP_JUMP, loopTop);
                    finishJump(state, afterLoop);
                    continue;
                }
                if (identifier == keywordReturn)
                {
                    if (!parseReturnRest(state))
                    {
                        goto statementError;
                    }
                }
                else if (identifier == keywordWhile)
                {
                    size_t afterLoop;
                    size_t loopTop = BVSize(state->bytecode);
                    int condition = parseRValue(state, false);
                    if (condition < 0)
                    {
                        goto statementError;
                    }
                    afterLoop = writeForwardBranch(state, OP_BRANCH_FALSE, (uint) condition);
                    indent = parseBlock(state, readNewline(state));
                    writeBackwardJump(state, OP_JUMP, loopTop);
                    finishJump(state, afterLoop);
                    continue;
                }
                else
                {
                    assert(false);
                    goto statementError;
                }
            }
            else
            {
                if (!parseExpressionStatement(state, identifier))
                {
                    goto statementError;
                }
            }
        }
        else
        {
            error(state, "Not a statement");
            goto statementError;
        }

        indent = readNewline(state);
        continue;

statementError:
        indent = skipStatement(state);
        continue;
    }
    state->statementIndent = oldStatementIndent;
    return indent;
}

static boolean parseFunctionDeclarationRest(ParseState *state, functionref function)
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
            error(state, "Garbage after target declaration");
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
                    error(state, "Expected parameter name or ')'");
                    return false;
                }
                skipWhitespace(state);
                value = HeapInvalid;
                if (readOperator(state, ':'))
                {
                    requireDefaultValues = true;
                    skipWhitespace(state);
                    estate.identifier = 0;
                    if (!parseExpression(state, &estate, 1, true))
                    {
                        return false;
                    }
                    assert(estate.expressionType == EXPRESSION_CONSTANT);
                    value = estate.constant;
                }
                else if (requireDefaultValues)
                {
                    error(state, "Default value for parameter '%s' required",
                          HeapGetString(parameterName));
                    return false;
                }
                else if (readOperator3(state, '.', '.', '.'))
                {
                    requireDefaultValues = true;
                    skipWhitespace(state);
                    vararg = true;
                    value = HeapEmptyList;
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
            error(state, "Garbage after function declaration");
            return false;
        }
    }
    FunctionIndexFinishParameters(function, state->line,
                                  (uint)getOffset(state, state->start));
    return true;
}

static void parseScript(ParseState *state)
{
    vref name;
    boolean allowIndent = false;

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
                                       (uint)getOffset(state, state->start)));
                skipEndOfLine(state);
                allowIndent = true;
            }
            else if (peekOperator(state, '('))
            {
                NamespaceAddFunction(state->ns, name, FunctionIndexAddFunction(
                                         state->ns, name,
                                         state->filename, state->line,
                                         (uint)getOffset(state, state->start)));
                skipEndOfLine(state);
                allowIndent = true;
            }
            else
            {
                skipWhitespace(state);
                if (!readOperator(state, '='))
                {
                    error(state, "Invalid declaration");
                    allowIndent = true;
                    skipEndOfLine(state);
                    continue;
                }
                skipWhitespace(state);
                NamespaceAddField(state->ns, name, FieldIndexAdd(
                                      state->ns,
                                      state->filename, state->line,
                                      (uint)getOffset(state, state->start)));
                skipEndOfLine(state);
                allowIndent = false;
            }
        }
        else if ((peekIndent(state) && allowIndent) ||
                 peekComment(state))
        {
            skipEndOfLine(state);
        }
        else if (!peekReadNewline(state))
        {
            error(state, "Unsupported character: '%c'", *state->current);
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

static void initParseState(ParseState *state, bytevector *bytecode,
                           namespaceref ns, vref filename, uint line, uint offset)
{
    size_t size;

    assert(filename);
    assert(line == 1 || line <= offset);
    FileOpen(&state->fh, HeapGetString(filename), VStringLength(filename));
    FileMMap(&state->fh, &state->start, &size);
    state->current = state->start + offset;
    state->limit = state->start + size;
    state->ns = ns;
    state->filename = filename;
    state->line = line;
    state->statementIndent = 0;
    state->bytecode = bytecode;
    state->error = false;
}

static void initParseStateLocals(ParseState *state)
{
    state->localsCount = 0;
    IntHashMapInit(&state->locals, 256);
}

static void disposeParseStateLocals(ParseState *state)
{
    IntHashMapDispose(&state->locals);
}

static void disposeParseState(ParseState *state)
{
    FileClose(&state->fh);
}

void ParseFile(vref filename, namespaceref ns)
{
    ParseState state;

    initParseState(&state, null, ns, filename, 1, 0);
    if (state.current != state.limit && state.limit[-1] != '\n')
    {
        /* TODO: Provide fallback */
        errorOnLine(&state,
                    UtilCountNewlines((const char*)state.start,
                                      (size_t)(state.limit - state.start)) + 1,
                    "File does not end with newline");
    }
    else
    {
        parseScript(&state);
    }
    disposeParseState(&state);
}

static void parseField(ParseState *state, fieldref field, bytevector *bytecode)
{
    size_t start = BVSize(bytecode);
    int value;

    assert(field);
    initParseState(state, bytecode, FieldIndexGetNamespace(field),
                   FieldIndexGetFilename(field), FieldIndexGetLine(field),
                   FieldIndexGetFileOffset(field));

    state->statementLine = state->line;
    value = parseRValue(state, true);
    if (value >= 0)
    {
        if (!peekNewline(state))
        {
            statementError(state, "Garbage after variable declaration");
        }
        else
        {
            writeOp(bytecode, OP_STORE_FIELD, (uint)value);
            BVAddUint(bytecode, FieldIndexGetIndex(field));
            /* TODO: Look for code before next field/function. */
            FieldIndexSetBytecodeOffset(field, start, BVSize(bytecode));
        }
    }
    disposeParseState(state);
}

static void parseFunctionDeclaration(ParseState *state, functionref function, bytevector *bytecode)
{
    assert(function);
    initParseState(state, bytecode,
                   FunctionIndexGetNamespace(function),
                   FunctionIndexGetFilename(function),
                   FunctionIndexGetLine(function),
                   FunctionIndexGetFileOffset(function));
    state->statementLine = state->line;
    if (!parseFunctionDeclarationRest(state, function))
    {
        FunctionIndexSetFailedDeclaration(function);
    }
    disposeParseState(state);
}

static void parseFunctionBody(functionref function, bytevector *bytecode)
{
    const ParameterInfo *parameterInfo;
    ParseState state;
    size_t start = BVSize(bytecode);
    size_t paramsOffset;
    size_t localsOffset;
    uint line;
    uint indent;
    uint parameterCount;
    uint i;

    assert(function);
    line = FunctionIndexGetLine(function);
    if (!line)
    {
        return;
    }
    writeOp(bytecode, OP_FUNCTION, 0);
    BVAddRef(bytecode, function);
    paramsOffset = BVSize(bytecode);
    BVAddUint(bytecode, 0);
    localsOffset = BVSize(bytecode);
    BVAddUint(bytecode, 0);
    initParseState(&state, bytecode,
                   FunctionIndexGetNamespace(function),
                   FunctionIndexGetFilename(function),
                   line,
                   FunctionIndexGetFileOffset(function));
    initParseStateLocals(&state);

    /* This doesn't just check for duplicates; it also allocates local variables. */
    parameterCount = FunctionIndexGetParameterCount(function);
    if (parameterCount)
    {
        parameterInfo = FunctionIndexGetParameterInfo(function);
        for (i = 0; i < parameterCount; i++, parameterInfo++)
        {
            if (variableIndex(&state, parameterInfo->name) != i)
            {
                disposeParseState(&state);
                error(&state, "Multiple uses of parameter name '%s'",
                      HeapGetString(parameterInfo->name));
                return;
            }
        }
    }

    state.statementIndent = 0;
    indent = readNewline(&state);
    if (!eof(&state))
    {
        parseBlock(&state, indent);
    }
    if (!state.error)
    {
        FunctionIndexSetLocals(function, &state.locals, state.localsCount);
        writeOp(bytecode, OP_RETURN_VOID, 0);
        FunctionIndexSetBytecodeOffset(function, start);
    }
    BVSetUint(bytecode, paramsOffset, FunctionIndexGetParameterCount(function));
    BVSetUint(bytecode, localsOffset, FunctionIndexGetLocalsCount(function));
    disposeParseStateLocals(&state);
    disposeParseState(&state);
}

void ParseFinish(bytevector *bytecode)
{
    ParseState state;
    fieldref field;
    functionref function;
    size_t localsOffset;

    IVInit(&temp, 1024);
    BVInit(&btemp, 1024);

    writeOp(bytecode, OP_FUNCTION, 0);
    BVAddRef(bytecode, 0);
    BVAddUint(bytecode, 0);
    localsOffset = BVSize(bytecode);
    BVAddUint(bytecode, 0);
    initParseStateLocals(&state);
    for (field = FieldIndexGetFirstField();
         field;
         field = FieldIndexGetNextField(field))
    {
        parseField(&state, field, bytecode);
    }
    for (function = FunctionIndexGetNextFunction(
             FunctionIndexGetFirstFunction());
         function;
         function = FunctionIndexGetNextFunction(function))
    {
        parseFunctionDeclaration(&state, function, bytecode);
    }
    BVSetUint(bytecode, localsOffset, state.localsCount);
    disposeParseStateLocals(&state);
    writeOp(bytecode, OP_RETURN_VOID, 0);

    for (function = FunctionIndexGetNextFunction(
             FunctionIndexGetFirstFunction());
         function;
         function = FunctionIndexGetNextFunction(function))
    {
        parseFunctionBody(function, bytecode);
    }

    IVDispose(&temp);
    BVDispose(&btemp);
}
