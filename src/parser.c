#include <memory.h>
#include <stdarg.h>
#include <stdio.h>
#include "common.h"
#include "bytevector.h"
#include "file.h"
#include "heap.h"
#include "intvector.h"
#include "namespace.h"
#include "native.h"
#include "parser.h"
#include "stringpool.h"

typedef struct
{
    const byte *current;
    const byte *start;
    const byte *limit;
    ParsedProgram *program;
    namespaceref ns;
    vref filename;
    uint line;
    uint statementLine;
    uint statementIndent;
    uint jumpCount;
    int jumpTargetCount;

    int unnamedVariableCount;

    intvector *bytecode;
    intvector *constants;
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
    int variable;
    vref valueIdentifier;
    vref constant;
    nativefunctionref nativeFunction;
    vref ns;
    int valueCount;
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
                               int valueCount, boolean constant);
static boolean parseUnquotedExpression(ParseState *state,
                                       ExpressionState *estate,
                                       boolean constant);


static int encodeOp(Instruction op, int param)
{
    assert(((param << 8) >> 8) == param);
    return (int)op | (param << 8);
}

static void writeOp(ParseState *state, Instruction op, int param)
{
    IVAdd(state->bytecode, encodeOp(op, param));
}

static void writeOp2(ParseState *state, Instruction op, int param1, int param2)
{
    int *restrict write = IVGetAppendPointer(state->bytecode, 2);
    *write++ = encodeOp(op, param1);
    *write++ = param2;
}

static void writeOp3(ParseState *state, Instruction op, int param1, int param2, int param3)
{
    int *restrict write = IVGetAppendPointer(state->bytecode, 3);
    *write++ = encodeOp(op, param1);
    *write++ = param2;
    *write++ = param3;
}


static size_t getOffset(const ParseState *state, const byte *begin)
{
    return (size_t)(state->current - begin);
}

static boolean eof(const ParseState *state)
{
    return state->current == state->limit;
}


static attrprintf(2, 3) void error(ParseState *state, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    writeOp(state, OP_ERROR, intFromRef(HeapCreateStringFormatted(format, args)));
    va_end(args);
}


static int createVariable(ParseState *state)
{
    assert(state->unnamedVariableCount < INT_MAX); /* TODO */
    return (INT_MIN >> 8) + state->unnamedVariableCount++;
}


static void writeJump(ParseState *state, int target)
{
    state->jumpCount++;
    writeOp(state, OP_JUMP_INDEXED, target);
}

static void writeBranch(ParseState *state, int target, Instruction instruction, int variable)
{
    state->jumpCount++;
    writeOp2(state, instruction, target, variable);
}

static int createJumpTarget(ParseState *state)
{
    return state->jumpTargetCount++;
}

static void placeJumpTargetHere(ParseState *state, int target)
{
    writeOp(state, OP_JUMPTARGET, target);
}

static int createJumpTargetHere(ParseState *state)
{
    int target = state->jumpTargetCount++;
    writeOp(state, OP_JUMPTARGET, target);
    return target;
}


static void storeConstant(ParseState *state, vref value, int variable)
{
    if (!value)
    {
        writeOp(state, OP_NULL, variable);
    }
    else if (value == HeapTrue)
    {
        writeOp(state, OP_TRUE, variable);
    }
    else if (value == HeapFalse)
    {
        writeOp(state, OP_FALSE, variable);
    }
    else if (value == HeapEmptyList)
    {
        writeOp(state, OP_EMPTY_LIST, variable);
    }
    else
    {
        writeOp2(state, OP_STORE_CONSTANT, variable, intFromRef(value));
    }
}

static int variableFromConstant(ParseState *state, vref value)
{
    IVAddRef(state->constants, value);
    return -(int)IVSize(state->constants);
}


static void parsedConstant(ExpressionState *estate, ValueType valueType, vref constant)
{
    estate->expressionType = EXPRESSION_CONSTANT;
    estate->valueType = valueType;
    estate->constant = constant;
}


static boolean isInitialIdentifierCharacter(byte c)
{
    static boolean characters[] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,1,
        0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    };
    return characters[c];
    /* return (c >= 'a' && c <= 'z') || */
    /*     (c >= 'A' && c <= 'Z') || */
    /*     c == '_'; */
}

static boolean isIdentifierCharacter(byte c)
{
    static boolean characters[] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,
        0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,1,
        0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    };
    return characters[c];
    /* return (c >= 'a' && c <= 'z') || */
    /*     (c >= 'A' && c <= 'Z') || */
    /*     (c >= '0' && c <= '9') || */
    /*     c == '_'; */
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
            error(state, "Expected linebreak");
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
    error(state, "Expected keyword '%s'", HeapGetString(keyword));
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


static void convertConstantsToValues(ParseState *state, int *values, size_t count)
{
    while (count--)
    {
        int index = variableFromConstant(state, refFromUint((uint)*values));
        *values++ = index;
    }
}

static void finishAndStoreValueAt(ParseState *state, const ExpressionState *estate, int variable)
{
    int variable2;
    switch (estate->expressionType)
    {
    case EXPRESSION_CONSTANT:
        storeConstant(state, estate->constant, variable);
        return;

    case EXPRESSION_STORED:
        if (estate->variable != variable)
        {
            writeOp2(state, OP_COPY, estate->variable, variable);
        }
        return;

    case EXPRESSION_MANY:
        assert(estate->valueCount == 1);
    case EXPRESSION_MISSING_STORE:
        IVAdd(state->bytecode, variable);
        return;

    case EXPRESSION_VARIABLE:
        variable2 = intFromRef(estate->valueIdentifier);
        if (variable != variable2)
        {
            writeOp2(state, OP_COPY, variable2, variable);
        }
        return;

    case EXPRESSION_FIELD:
        writeOp3(state, OP_LOAD_FIELD, estate->variable, intFromRef(estate->ns), variable);
        return;
    }
    unreachable;
}

static int finishRValue(ParseState *state, const ExpressionState *estate)
{
    int variable;

    switch (estate->expressionType)
    {
    case EXPRESSION_CONSTANT:
        return variableFromConstant(state, estate->constant);

    case EXPRESSION_STORED:
        return estate->variable;

    case EXPRESSION_MANY:
        assert(estate->valueCount == 1);
    case EXPRESSION_MISSING_STORE:
        variable = createVariable(state);
        IVAdd(state->bytecode, variable);
        return variable;

    case EXPRESSION_VARIABLE:
        return intFromRef(estate->valueIdentifier);

    case EXPRESSION_FIELD:
        variable = createVariable(state);
        writeOp3(state, OP_LOAD_FIELD, estate->variable, intFromRef(estate->ns), variable);
        return variable;
    }
    unreachable;
}

static int parseRValue(ParseState *state, boolean constant)
{
    ExpressionState estate;

    estate.identifier = 0;
    if (!parseExpression(state, &estate, 1, constant))
    {
        return 0;
    }
    return finishRValue(state, &estate);
}

static boolean parseAndStoreValueAt(ParseState *state, int variable)
{
    ExpressionState estate;

    estate.identifier = 0;
    if (!parseExpression(state, &estate, 1, false))
    {
        return false;
    }
    finishAndStoreValueAt(state, &estate, variable);
    return true;
}

static boolean finishLValue(ParseState *state, const ExpressionState *lvalue,
                            const ExpressionState *rvalue)
{
    /* int variable; */
    switch (lvalue->expressionType)
    {
    case EXPRESSION_CONSTANT:
    case EXPRESSION_STORED:
    case EXPRESSION_MISSING_STORE:
    case EXPRESSION_MANY:
        error(state, "Invalid target for assignment");
        return false;

    case EXPRESSION_VARIABLE:
        finishAndStoreValueAt(state, rvalue, intFromRef(lvalue->valueIdentifier));
        return true;

    case EXPRESSION_FIELD:
        writeOp3(state, OP_STORE_FIELD, lvalue->variable,
                 intFromRef(lvalue->ns), finishRValue(state, rvalue));
        return true;
    }
    unreachable;
}

static boolean parseInvocationRest(ParseState *state, ExpressionState *estate,
                                   vref ns, vref name)
{
    ExpressionState estateArgument;
    size_t oldTempSize = IVSize(&temp);
    int *restrict write;
    uint size;

    assert(!estate->identifier);
    estate->expressionType = EXPRESSION_MANY;
    estate->valueType = VALUE_UNKNOWN;

    for (;;)
    {
        int value;
        if (!skipWhitespaceAndNewline(state))
        {
            goto error;
        }
        if (readOperator(state, ')'))
        {
            break;
        }
        if (!skipWhitespaceAndNewline(state))
        {
            goto error;
        }
        estateArgument.identifier = peekReadIdentifier(state);
        if (estateArgument.identifier && readOperator(state, ':'))
        {
            for (;;)
            {
                IVAddRef(&temp, estateArgument.identifier);
                estateArgument.identifier = 0;
                value = parseRValue(state, false);
                if (!value || !skipWhitespaceAndNewline(state))
                {
                    goto error;
                }
                IVAdd(&temp, value);
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
        IVAdd(&temp, 0);
        IVAdd(&temp, finishRValue(state, &estateArgument));
    }

    state->program->invocationCount++;
    size = (uint)(IVSize(&temp) - oldTempSize);
    write = IVGetAppendPointer(state->bytecode, 4 + size);
    *write++ = encodeOp(OP_INVOKE_UNLINKED, intFromRef(name));
    *write++ = intFromRef(ns);
    *write++ = (int)size / 2;
    *write++ = estate->valueCount;
    memcpy(write, IVGetPointer(&temp, oldTempSize), size * sizeof(*write));
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
    int *restrict write;
    const int *restrict read;

    if (!function)
    {
        error(state, "Unknown native function '%s'",
              HeapGetString(name));
        return false;
    }
    if ((uint)estate->valueCount != NativeGetReturnValueCount(function))
    {
        error(state, "Native function returns %d values, but %d are handled",
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
        if (!value)
        {
            IVSetSize(&temp, oldTempSize);
            return false;
        }
        IVAdd(&temp, value);
        skipWhitespace(state);
    }
    argumentCount = (uint)(IVSize(&temp) - oldTempSize);
    write = IVGetAppendPointer(state->bytecode, 1 + argumentCount);
    *write++ = encodeOp(OP_INVOKE_NATIVE, intFromRef(function));
    read = IVGetPointer(&temp, oldTempSize);
    while (argumentCount--)
    {
        *write++ = *read++;
    }
    IVSetSize(&temp, oldTempSize);
    return readExpectedOperator(state, ')');
}

static boolean parseBinaryOperationRest(
    ParseState *state, ExpressionState *estate,
    boolean (*parseExpressionRest)(ParseState*, ExpressionState*),
    Instruction instruction, ValueType valueType)
{
    int value = finishRValue(state, estate);
    int value2;
    skipExpressionWhitespace(state, estate);
    if (!parseExpressionRest(state, estate))
    {
        return false;
    }
    value2 = finishRValue(state, estate);
    writeOp2(state, instruction, value, value2);
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
                IVAddRef(&temp, estate2.constant);
            }
            else
            {
                constant = false;
                convertConstantsToValues(state,
                                         (int*)IVGetWritePointer(&temp, oldTempSize),
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
        parsedConstant(estate, VALUE_LIST, HeapCreateArrayFromVectorSegment(
                           &temp, oldTempSize, IVSize(&temp) - oldTempSize));
    }
    else
    {
        uint length = (uint)(IVSize(&temp) - oldTempSize);
        int *restrict write = IVGetAppendPointer(state->bytecode, 1 + length);
        *write++ = encodeOp(OP_LIST, (int)length);
        memcpy(write, IVGetPointer(&temp, oldTempSize), length * sizeof(*write));
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
    vref ns;

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
            error(state, "Unexpected keyword '%s'",
                  HeapGetString(identifier));
            return false;
        }
        if (estate->parseConstant)
        {
            error(state, "Expected constant");
            return false;
        }
        if (!peekOperator2(state, '.', '.') &&
            readOperator(state, '.'))
        {
            if (state->ns == NAMESPACE_DON && identifier == StringPoolAdd("native"))
            {
                identifier = readVariableName(state);
                if (!identifier || !readExpectedOperator(state, '('))
                {
                    return false;
                }
                return parseNativeInvocationRest(state, estate, identifier);
            }
            ns = identifier;
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
            estate->ns = ns;
            estate->variable = intFromRef(identifier);
            return true;
        }
        if (readOperator(state, '('))
        {
            return parseInvocationRest(state, estate, 0, identifier);
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
                    IVAddRef(&temp, estate2.constant);
                }
                else
                {
                    constant = false;
                    convertConstantsToValues(
                        state,
                        (int*)IVGetWritePointer(&temp, oldTempSize),
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
            parsedConstant(estate, VALUE_LIST, HeapCreateArrayFromVectorSegment(
                               &temp, oldTempSize, IVSize(&temp) - oldTempSize));
        }
        else
        {
            uint length = (uint)(IVSize(&temp) - oldTempSize);
            int *restrict write = IVGetAppendPointer(state->bytecode, 1 + length);
            estate->expressionType = EXPRESSION_MISSING_STORE;
            estate->valueType = VALUE_LIST;
            *write++ = encodeOp(OP_LIST, (int)length);
            memcpy(write, IVGetPointer(&temp, oldTempSize), length * sizeof(*write));
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
        writeOp(state, OP_FILELIST, intFromRef(string));
        estate->expressionType = EXPRESSION_MISSING_STORE;
        estate->valueType = VALUE_FILE;
        return true;
    }
    error(state, "Invalid expression");
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
            int value = finishRValue(state, estate);
            int index;
            skipWhitespace(state);
            index = parseRValue(state, estate->parseConstant);
            if (!index)
            {
                return false;
            }
            skipWhitespace(state);
            if (!readExpectedOperator(state, ']'))
            {
                return false;
            }
            writeOp2(state, OP_INDEXED_ACCESS, value, index);
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
    int variable;
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
            int variable2 = finishRValue(state, estate);
            writeOp2(state, OP_CONCAT_STRING, variable, variable2);
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
            error(state, "Expected constant");
            return false;
        }
        variable = finishRValue(state, estate);
        first = false;
    }
}

static boolean parseExpression9(ParseState *state, ExpressionState *estate)
{
    if (readOperator(state, '-'))
    {
        int value;
        assert(!readOperator(state, '-')); /* TODO: -- operator */
        if (!parseExpression10(state, estate))
        {
            return false;
        }
        value = finishRValue(state, estate);
        writeOp(state, OP_NEG, value);
        skipExpressionWhitespace(state, estate);
        estate->expressionType = EXPRESSION_MISSING_STORE;
        estate->valueType = VALUE_NUMBER;
        return true;
    }
    if (readOperator(state, '!'))
    {
        int value;
        if (!parseExpression10(state, estate))
        {
            return false;
        }
        value = finishRValue(state, estate);
        writeOp(state, OP_NOT, value);
        skipExpressionWhitespace(state, estate);
        estate->expressionType = EXPRESSION_MISSING_STORE;
        return true;
    }
    if (readOperator(state, '~'))
    {
        int value;
        if (!parseExpression10(state, estate))
        {
            return false;
        }
        value = finishRValue(state, estate);
        writeOp(state, OP_INV, value);
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
    int target;

    if (!parseExpression3(state, estate))
    {
        return false;
    }
    for (;;)
    {
        if (readOperator2(state, '&', '&'))
        {
            int variable = createVariable(state);
            finishAndStoreValueAt(state, estate, variable);
            skipExpressionWhitespaceAndNewline(state, estate);
            target = createJumpTarget(state);
            writeBranch(state, target, OP_BRANCH_FALSE_INDEXED, variable);
            if (!parseExpression3(state, estate))
            {
                return false;
            }
            finishAndStoreValueAt(state, estate, variable);
            placeJumpTargetHere(state, target);
            estate->expressionType = EXPRESSION_STORED;
            estate->valueType = VALUE_UNKNOWN;
            estate->variable = variable;
            skipExpressionWhitespace(state, estate);
            continue;
        }
        if (readOperator2(state, '|', '|'))
        {
            int variable = createVariable(state);
            finishAndStoreValueAt(state, estate, variable);
            skipExpressionWhitespaceAndNewline(state, estate);
            target = createJumpTarget(state);
            writeBranch(state, target, OP_BRANCH_TRUE_INDEXED, variable);
            if (!parseExpression3(state, estate))
            {
                return false;
            }
            finishAndStoreValueAt(state, estate, variable);
            placeJumpTargetHere(state, target);
            estate->expressionType = EXPRESSION_STORED;
            estate->valueType = VALUE_UNKNOWN;
            estate->variable = variable;
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
        int target1 = createJumpTarget(state);
        int target2 = createJumpTarget(state);
        int variable = createVariable(state);
        assert(!parseConstant); /* TODO */
        skipExpressionWhitespace(state, estate);
        writeBranch(state, target1, OP_BRANCH_FALSE_INDEXED, finishRValue(state, estate));
        if (!parseAndStoreValueAt(state, variable) || !readExpectedOperator(state, ':'))
        {
            return false;
        }
        writeJump(state, target2);
        placeJumpTargetHere(state, target1);
        skipExpressionWhitespace(state, estate);
        if (!parseAndStoreValueAt(state, variable))
        {
            return false;
        }
        placeJumpTargetHere(state, target2);
        estate->expressionType = EXPRESSION_STORED;
        estate->valueType = VALUE_UNKNOWN;
        estate->variable = variable;
        return true;
    }
    return true;
}

static boolean parseExpression(ParseState *state, ExpressionState *estate,
                               int valueCount, boolean constant)
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
    int value = finishRValue(state, estate);
    int value2;
    skipWhitespace(state);
    value2 = parseRValue(state, false);
    if (!value2)
    {
        return false;
    }
    writeOp2(state, instruction, value, value2);
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
        const int *pindex;
        int returnValueCount;
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
        returnValueCount = (int)((BVSize(&btemp) - oldBTempSize) / sizeof(estate));
        if (!parseExpression(state, &rvalue, returnValueCount, false))
        {
            goto error;
        }
        if (rvalue.expressionType != EXPRESSION_MANY)
        {
            error(state, "Expected function invocation");
            goto error;
        }
        assert(rvalue.valueCount == returnValueCount);
        for (p = oldBTempSize; p < BVSize(&btemp); p += sizeof(estate))
        {
            int variable;
            estate = *(const ExpressionState*)BVGetPointer(&btemp, p);
            variable = estate.expressionType == EXPRESSION_VARIABLE ?
                intFromRef(estate.valueIdentifier) :
                createVariable(state);
            IVAdd(state->bytecode, variable);
            IVAdd(&temp, variable);
        }
        for (p = oldBTempSize, pindex = (const int*)IVGetPointer(&temp, oldTempSize);
             p < BVSize(&btemp);
             p += sizeof(estate), pindex++)
        {
            estate = *(const ExpressionState*)BVGetPointer(&btemp, p);
            rvalue.expressionType = EXPRESSION_STORED;
            rvalue.variable = *pindex;
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
    error(state, "Not a statement");
    return false;
}

static boolean parseReturnRest(ParseState *state)
{
    size_t oldTempSize;
    int value;

    if (peekNewline(state))
    {
        writeOp(state, OP_RETURN_VOID, 0);
        return true;
    }
    oldTempSize = IVSize(&temp);
    for (;;)
    {
        value = parseRValue(state, false);
        if (!value)
        {
            IVSetSize(&temp, oldTempSize);
            return false;
        }
        IVAdd(&temp, value);
        if (peekNewline(state))
        {
            uint count = (uint)(IVSize(&temp) - oldTempSize);
            int *restrict write = IVGetAppendPointer(state->bytecode, 1 + count);
            const int *restrict read = IVGetPointer(&temp, oldTempSize);
            *write++ = encodeOp(OP_RETURN, (int)count);
            while (count--)
            {
                *write++ = *read++;
            }
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
        writeOp(state, OP_LINE, (int)state->line);
        error(state, "Expected increased indentation level");
        return indent;
    }

    state->statementIndent = indent;
    for (;;)
    {
        writeOp(state, OP_LINE, (int)state->line);
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
                    error(state, "Not a statement");
                    goto statementError;
                }
                skipWhitespace(state);
                if (identifier == keywordIf)
                {
                    int conditionTarget = createJumpTarget(state);
                    int afterIfTarget;
                    int condition = parseRValue(state, false);
                    if (!condition)
                    {
                        /* TODO: Ignore else */
                        goto statementError;
                    }
                    writeBranch(state, conditionTarget, OP_BRANCH_FALSE_INDEXED, condition);

                    indent = parseBlock(state, readNewline(state));
                    if (indent != state->statementIndent || !peekReadKeywordElse(state))
                    {
                        placeJumpTargetHere(state, conditionTarget);
                        continue;
                    }

                    afterIfTarget = createJumpTarget(state);
                    for (;;)
                    {
                        writeJump(state, afterIfTarget);
                        placeJumpTargetHere(state, conditionTarget);
                        skipWhitespace(state);
                        identifier = peekReadIdentifier(state);
                        if (identifier != keywordIf)
                        {
                            if (identifier || !peekNewline(state))
                            {
                                error(state, "Garbage after else");
                                goto statementError;
                            }
                            indent = parseBlock(state, readNewline(state));
                            break;
                        }
                        skipWhitespace(state);
                        condition = parseRValue(state, false);
                        if (!condition)
                        {
                            goto statementError;
                        }
                        if (!peekNewline(state))
                        {
                            error(state, "Garbage after if statement");
                            goto statementError;
                        }
                        conditionTarget = createJumpTarget(state);
                        writeBranch(state, conditionTarget, OP_BRANCH_FALSE_INDEXED, condition);
                        indent = parseBlock(state, readNewline(state));
                        if (indent != state->statementIndent || !peekReadKeywordElse(state))
                        {
                            placeJumpTargetHere(state, conditionTarget);
                            break;
                        }
                    }
                    placeJumpTargetHere(state, afterIfTarget);
                    continue;
                }
                if (identifier == keywordElse)
                {
                    error(state, "else without matching if");
                    goto statementError;
                }
                else if (identifier == keywordFor)
                {
                    int loopTop;
                    int afterLoop;
                    int iterCollection;
                    int iterIndex;
                    int iterStep;
                    int iterCondition;
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
                    if (!iterCollection)
                    {
                        goto statementError;
                    }
                    iterIndex = createVariable(state);
                    iterStep = createVariable(state);
                    storeConstant(state, HeapBoxInteger(-1), iterIndex);
                    storeConstant(state, HeapBoxInteger(1), iterStep);
                    loopTop = createJumpTargetHere(state);
                    afterLoop = createJumpTarget(state);

                    writeOp3(state, OP_ADD, iterIndex, iterStep, iterIndex);
                    writeOp3(state, OP_ITER_GET, iterCollection, iterIndex, intFromRef(identifier));
                    iterCondition = createVariable(state);
                    IVAdd(state->bytecode, iterCondition);
                    writeBranch(state, afterLoop, OP_BRANCH_FALSE_INDEXED, iterCondition);
                    indent = parseBlock(state, readNewline(state));
                    writeJump(state, loopTop);
                    placeJumpTargetHere(state, afterLoop);
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
                    int loopTop = createJumpTargetHere(state);
                    int afterLoop = createJumpTarget(state);
                    int condition = parseRValue(state, false);
                    if (!condition)
                    {
                        goto statementError;
                    }
                    writeBranch(state, afterLoop, OP_BRANCH_FALSE_INDEXED, condition);
                    indent = parseBlock(state, readNewline(state));
                    writeJump(state, loopTop);
                    placeJumpTargetHere(state, afterLoop);
                    continue;
                }
                else
                {
                    unreachable;
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

static void parseFunctionBody(ParseState *state)
{
    uint indent;

    state->statementIndent = 0;
    state->jumpCount = 0;
    state->jumpTargetCount = 0;
    state->unnamedVariableCount = 0;
    indent = readNewline(state);
    if (!eof(state))
    {
        parseBlock(state, indent);
    }
    writeOp(state, OP_RETURN_VOID, 0);
    state->program->maxJumpCount = max(state->program->maxJumpCount, state->jumpCount);
    state->program->maxJumpTargetCount = max(state->program->maxJumpTargetCount,
                                             (uint)state->jumpTargetCount);
}

static boolean parseFunctionDeclarationRest(ParseState *state, vref functionName)
{
    ExpressionState estate;
    boolean requireDefaultValues = false;
    size_t paramsOffset;
    int parameterCount = 0;
    size_t varargOffset;
    int varargIndex = INT_MAX;

    paramsOffset = IVSize(state->bytecode) + 1;
    varargOffset = paramsOffset + 1;
    writeOp3(state, OP_FUNCTION_UNLINKED, intFromRef(functionName), 0, 0);

    if (!readOperator(state, ')'))
    {
        for (;;)
        {
            int value = INT_MAX;
            vref parameterName = peekReadIdentifier(state);
            if (!parameterName || isKeyword(parameterName))
            {
                error(state, "Expected parameter name or ')'");
                return false;
            }
            skipWhitespace(state);
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
                value = variableFromConstant(state, estate.constant);
            }
            else if (requireDefaultValues)
            {
                error(state, "Default value for parameter '%s' required",
                      HeapGetString(parameterName));
                return false;
            }
            else if (readOperator3(state, '.', '.', '.'))
            {
                assert(varargIndex == INT_MAX); /* TODO: error message */
                varargIndex = parameterCount;
                requireDefaultValues = true;
                skipWhitespace(state);
                value = variableFromConstant(state, HeapEmptyList);
            }
            IVAdd(state->bytecode, intFromRef(parameterName));
            IVAdd(state->bytecode, value);
            parameterCount++;
            if (readOperator(state, ')'))
            {
                break;
            }
            skipWhitespace(state);
        }
    }
    IVSet(state->bytecode, paramsOffset, parameterCount);
    IVSet(state->bytecode, varargOffset, varargIndex);
    if (!peekNewline(state))
    {
        error(state, "Garbage after function declaration");
        return false;
    }
    return true;
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

void ParseInit(ParsedProgram *program)
{
    IVInit(&program->bytecode, 16384);
    IVInit(&program->functions, 32);
    IVInit(&program->constants, 1024);
    IVInit(&program->fields, 32);
    program->invocationCount = 0;
    program->maxJumpCount = 0;
    program->maxJumpTargetCount = 0;

    IVInit(&temp, 1024);
    BVInit(&btemp, 1024);
}

void ParseDispose(void)
{
    IVDispose(&temp);
    BVDispose(&btemp);
}

void ParseFile(ParsedProgram *program, vref filename, namespaceref ns)
{
    ParseState state;
    File file;
    size_t size;
    vref name;
    byte *buffer;

    assert(filename);
    state.ns = ns;
    state.filename = filename;
    state.line = 1;
    state.statementIndent = 0;
    state.bytecode = &program->bytecode;
    state.program = program;
    state.constants = &program->constants;

    FileOpen(&file, HeapGetString(filename), VStringLength(filename));
    size = FileSize(&file);
    buffer = (byte*)malloc(size + 1);
    FileRead(&file, buffer, size);
    FileClose(&file);
    /* Make sure the file ends with a newline. That way there is only a need to
       look for end-of-file on newlines. */
    buffer[size++] = '\n';

    state.start = buffer;
    state.current = state.start;
    state.limit = state.start + size;

    writeOp2(&state, OP_FILE, intFromRef(filename), intFromRef(ns));

    while (!eof(&state))
    {
        if (peekIdentifier(&state))
        {
            writeOp(&state, OP_LINE, (int)state.line);
            state.statementLine = state.line;
            name = readIdentifier(&state);
            if (readOperator(&state, ':'))
            {
                int existingFunction = NamespaceAddTarget(
                    ns, name, (int)IVSize(&program->functions));
                IVAdd(&program->functions, (int)IVSize(state.bytecode));
                if (existingFunction >= 0)
                {
                    /* TODO: blacklist function */
                    error(&state, "Multiple functions or targets with name '%s'",
                          HeapGetString(name));
                }
                if (!peekNewline(&state))
                {
                    error(&state, "Garbage after target declaration");
                    /* TODO: skip parsing function body */
                    goto error;
                }
                writeOp3(&state, OP_FUNCTION_UNLINKED, intFromRef(name), 0, 0);
                parseFunctionBody(&state);
            }
            else if (readOperator(&state, '('))
            {
                int existingFunction = NamespaceAddFunction(
                    ns, name, (int)IVSize(&program->functions));
                IVAdd(&program->functions, (int)IVSize(state.bytecode));
                if (existingFunction >= 0)
                {
                    /* TODO: blacklist function */
                    error(&state, "Multiple functions or targets with name '%s'",
                          HeapGetString(name));
                }
                if (!parseFunctionDeclarationRest(&state, name))
                {
                    /* TODO: skip parsing function body, but continue parsing after it */
                    goto error;
                }
                parseFunctionBody(&state);
            }
            else
            {
                ExpressionState estate;
                skipWhitespace(&state);
                if (!readOperator(&state, '='))
                {
                    error(&state, "Invalid declaration");
                    /* TODO: skip declaration */
                    goto error;
                }
                skipWhitespace(&state);
                estate.identifier = 0;
                if (parseExpression(&state, &estate, 1, true))
                {
                    assert(estate.expressionType == EXPRESSION_CONSTANT);
                    if (!peekNewline(&state))
                    {
                        error(&state, "Garbage after variable declaration");
                    }
                    else
                    {
                        if (NamespaceAddField(ns, name, (int)IVSize(&program->fields)) >= 0)
                        {
                            error(&state, "Multiple fields with name '%s'", HeapGetString(name));
                        }
                        IVAdd(&program->fields, intFromRef(estate.constant));
                    }
                }
            }
        }
        else if (peekComment(&state))
        {
            skipEndOfLine(&state);
        }
        else if (!peekReadNewline(&state))
        {
            error(&state, "Unsupported character: '%c'", *state.current);
            skipEndOfLine(&state);
        }
    }

error:
    free(buffer);
}
