#include "common.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include "bytevector.h"
#include "fail.h"
#include "file.h"
#include "heap.h"
#include "intvector.h"
#include "namespace.h"
#include "native.h"
#include "parser.h"
#include "stringpool.h"

static const bool DEBUG_PARSER = false;

typedef struct
{
    const byte *current;
    const byte *start;
    const byte *limit;
    ParsedProgram *program;
    namespaceref ns;
    int line;
    int lineBeforeSkip;
    uint jumpCount;
    int jumpTargetCount;
    int unnamedVariableCount;
    bool isTarget;
    bool structuralError;

    intvector *bytecode;
    intvector *constants;
} ParseState;

typedef enum
{
    EXPRESSION_CONSTANT,
    EXPRESSION_STORED,
    EXPRESSION_VARIABLE,
    EXPRESSION_FIELD,
    EXPRESSION_MANY
} ExpressionType;

typedef enum
{
    NO_ERROR = 0,
    ERROR_MISSING_RIGHTPAREN,
    ERROR_INVALID_EXPRESSION
} Error;

typedef struct
{
    vref identifier;
    ExpressionType expressionType;
    int variable;
    vref valueIdentifier;
    vref constant;
    vref ns;
    int valueCount;
    bool parseConstant;
    bool eatNewlines;
    bool sideEffects;
} ExpressionState;

static vref keywordElse;
static vref keywordFalse;
static vref keywordFor;
static vref keywordFn;
static vref keywordIf;
static vref keywordIn;
static vref keywordList;
static vref keywordNull;
static vref keywordReturn;
static vref keywordTarget;
static vref keywordTrue;
static vref keywordWhile;
static vref identifierNative;

static vref maxStatementKeyword;
static vref maxKeyword;

static intvector temp;
static bytevector btemp;


static bool parseExpression(ParseState *state, ExpressionState *estate,
                            int valueCount, bool constant, bool eatNewlines);
static int parseRValue(ParseState *state, bool constant, bool eatNewlines);
static void parseBlock(ParseState *state);


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

static void writeOpFromTemp(ParseState *state, Instruction op, size_t oldTempSize)
{
    uint count = (uint)(IVSize(&temp) - oldTempSize);
    int *restrict write = IVGetAppendPointer(state->bytecode, 1 + count);
    const int *restrict read = IVGetPointer(&temp, oldTempSize);
    *write++ = encodeOp(op, (int)count);
    while (count--)
    {
        *write++ = *read++;
    }
    IVSetSize(&temp, oldTempSize);
}


static size_t getOffset(const ParseState *state, const byte *begin)
{
    return (size_t)(state->current - begin);
}

static bool eof(const ParseState *state)
{
    return state->current == state->limit;
}


static attrprintf(2, 3) void error(ParseState *state, const char *format, ...)
{
    va_list args;
    if (DEBUG_PARSER)
    {
        va_start(args, format);
        fprintf(stderr, "%d:", state->line);
        fprintf(stderr, format, args);
        fputs("\n", stderr);
        va_end(args);
    }
    va_start(args, format);
    writeOp(state, OP_ERROR, intFromRef(HeapCreateStringFormatted(format, args)));
    va_end(args);
}

static attrprintf(3, 4) void errorOnLine(ParseState *state, int line, const char *format, ...)
{
    va_list args;
    if (DEBUG_PARSER)
    {
        va_start(args, format);
        fprintf(stderr, "%d:", line);
        fprintf(stderr, format, args);
        fputs("\n", stderr);
        va_end(args);
    }
    writeOp(state, OP_LINE, line);
    va_start(args, format);
    writeOp(state, OP_ERROR, intFromRef(HeapCreateStringFormatted(format, args)));
    va_end(args);
    writeOp(state, OP_LINE, state->line);
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


static void parsedConstant(ExpressionState *estate, vref constant)
{
    estate->expressionType = EXPRESSION_CONSTANT;
    estate->constant = constant;
}


static bool isInitialIdentifierCharacter(byte c)
{
    static bool characters[] = {
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

static bool isIdentifierCharacter(byte c)
{
    static bool characters[] = {
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

static bool isFilenameCharacter(byte c)
{
    return (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        memchr("/.*-+~_=!@#$%^&", c, 15);
}


static bool readOperator(ParseState *state, byte op)
{
    if (*state->current == op)
    {
        state->current++;
        return true;
    }
    return false;
}

static bool peekOperator(ParseState *state, byte op)
{
    return *state->current == op;
}

static bool reverseIfOperator(ParseState *state, byte op)
{
    if (peekOperator(state, op))
    {
        state->current--;
        return true;
    }
    return false;
}

static bool readOperator2(ParseState *state, byte op1, byte op2)
{
    if (state->current[0] == op1 && state->current[1] == op2)
    {
        state->current += 2;
        return true;
    }
    return false;
}

static bool peekOperator2(ParseState *state, byte op1, byte op2)
{
    if (state->current[0] == op1 && state->current[1] == op2)
    {
        return true;
    }
    return false;
}

static bool readOperator3(ParseState *state, byte op1, byte op2, byte op3)
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


static bool peekNewline(ParseState *state)
{
    return *state->current == '\n' || *state->current == '#';
}

static bool peekReadNewline(ParseState *state)
{
    if (*state->current == '\n')
    {
        state->current++;
        state->line++;
        writeOp(state, OP_LINE, state->line);
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
        if (unlikely(*state->current != '#'))
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
            if (state->current == state->limit)
            {
                return 0;
            }
            state->current++;
            state->line++;
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
            writeOp(state, OP_LINE, state->line);
            return (uint)getOffset(state, lineBegin);
        }
    }
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
    while (*state->current++ != '\n');
    state->line++;
    writeOp(state, OP_LINE, state->line);
}

static bool skipBlockWhitespace(ParseState *state)
{
    skipWhitespace(state);
    if (peekNewline(state))
    {
        int line = state->line;
        readNewline(state);
        if (unlikely(eof(state)))
        {
            if (!state->structuralError)
            {
                errorOnLine(state, line + 1, "Expected operator '}'");
            }
            return true;
        }
        return readOperator(state, '}');
    }
    return readOperator(state, '}');
}

static void skipWhitespaceAndNewline(ParseState *state)
{
    state->lineBeforeSkip = state->line;
    skipWhitespace(state);
    if (unlikely(peekNewline(state)))
    {
        readNewline(state);
    }
}

static void skipExpressionWhitespace(ParseState *state, ExpressionState *estate)
{
    if (estate->eatNewlines)
    {
        skipWhitespaceAndNewline(state);
    }
    else
    {
        skipWhitespace(state);
    }
}

static bool unreadNewline(ParseState *state)
{
    const byte *p = state->current;
    for (;;)
    {
        p--;
        if (*p == '\n')
        {
            state->current = p;
            state->line--;
            return true;
        }
        if (*p != ' ')
        {
            return false;
        }
    }
}

static const byte *skipDoubleQuotedString(const ParseState *state)
{
    const byte *p = state->current;
    assert(*p == '"');
    for (;;)
    {
        p++;
        if (*p == '"' || *p == '\n')
        {
            return p;
        }
        if (*p == '\\' && (p[1] == '"' || p[1] == '\\'))
        {
            p++;
        }
    }
}

static const byte *skipSingleQuotedString(const ParseState *state)
{
    const byte *p = state->current;
    assert(*p == '\'');
    for (;;)
    {
        p++;
        if (*p == '\'' || *p == '\n')
        {
            return p;
        }
    }
}

static bool skipToComma(ParseState *state, char expectedTerminator)
{
    for (;;)
    {
        byte c = *state->current++;
        if (c == ',')
        {
            skipWhitespaceAndNewline(state);
            return true;
        }
        if (c == expectedTerminator || (c == '\n' && (eof(state) || *state->current != ' ')))
        {
            return false;
        }
        if (c == '"')
        {
            state->current--;
            state->current = skipDoubleQuotedString(state);
            if (*state->current != '"')
            {
                state->structuralError = true;
                return false;
            }
        }
        if (c == '\'')
        {
            state->current--;
            state->current = skipSingleQuotedString(state);
            if (*state->current != '\'')
            {
                state->structuralError = true;
                return false;
            }
        }
    }
}

static bool peekComment(const ParseState *state)
{
    return *state->current == '#';
}

static bool isKeyword(vref identifier)
{
    return identifier <= maxKeyword;
}

static bool peekIdentifier(const ParseState *state)
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

static bool peekReadKeywordElse(ParseState *state)
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
    if (likely(identifier && !isKeyword(identifier)))
    {
        return identifier;
    }
    error(state, "Expected variable name");
    return 0;
}

static bool readExpectedKeyword(ParseState *state, vref keyword)
{
    vref identifier = peekReadIdentifier(state);
    if (likely(identifier == keyword))
    {
        return true;
    }
    error(state, "Expected keyword '%s'", HeapGetString(keyword));
    return false;
}

static bool isDigit(byte b)
{
    return b >= '0' && b <= '9';
}

static bool peekNumber(const ParseState *state)
{
    return isDigit(*state->current);
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
    if (unlikely(begin == state->current))
    {
        error(state, "Expected filename");
        return 0;
    }
    return HeapCreateString((const char*)begin, getOffset(state, begin));
}


/* TODO: Parse big numbers */
/* TODO: Parse non-decimal numbers */
/* TODO: Parse non-integer numbers */
static void parseNumber(ParseState *state, ExpressionState *estate)
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

    parsedConstant(estate, HeapBoxInteger(value));
}

static bool readExpectedOperator(ParseState *state, byte op)
{
    if (likely(readOperator(state, op)))
    {
        return true;
    }
    writeOp(state, OP_LINE, state->lineBeforeSkip);
    if (peekIdentifier(state))
    {
        ParseState stateCopy = *state;
        vref identifier = readIdentifier(&stateCopy);
        error(state, "Expected operator '%c'. Got '%s'", op, HeapGetString(identifier));
    }
    else if (peekNumber(state))
    {
        ParseState stateCopy = *state;
        ExpressionState estate;
        parseNumber(&stateCopy, &estate);
        error(state, "Expected operator '%c'. Got '%d'", op, HeapUnboxInteger(estate.constant));
    }
    else if (*state->current == '"')
    {
        const byte *p = skipDoubleQuotedString(state);
        if (*p == '"')
        {
            size_t oldBTempSize = BVSize(&btemp);
            BVAddData(&btemp, state->current, (size_t)(p - state->current + 1));
            BVAdd(&btemp, 0);
            error(state, "Expected operator '%c'. Got %s", op,
                  BVGetPointer(&btemp, oldBTempSize));
            BVSetSize(&btemp, oldBTempSize);
            return false;
        }
        else
        {
            error(state, "Expected operator '%c'. Got string", op);
        }
    }
    else if (*state->current == '\'')
    {
        const byte *p = skipSingleQuotedString(state);
        if (*p == '\'')
        {
            size_t oldBTempSize = BVSize(&btemp);
            BVAddData(&btemp, state->current, (size_t)(p - state->current + 1));
            BVAdd(&btemp, 0);
            error(state, "Expected operator '%c'. Got %s", op,
                  BVGetPointer(&btemp, oldBTempSize));
            BVSetSize(&btemp, oldBTempSize);
            return false;
        }
        else
        {
            error(state, "Expected operator '%c'. Got string", op);
        }
    }
    else
    {
        /* TODO: Nicer message for @ */
        error(state, "Expected operator '%c'. Got '%c'", op, *state->current);
    }
    writeOp(state, OP_LINE, state->line);
    return false;
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

static int parseRValue(ParseState *state, bool constant, bool eatNewlines)
{
    ExpressionState estate;

    estate.identifier = 0;
    if (unlikely(!parseExpression(state, &estate, 1, constant, eatNewlines)))
    {
        return 0;
    }
    return finishRValue(state, &estate);
}

static bool parseAndStoreValueAt(ParseState *state, int variable, bool eatNewlines)
{
    ExpressionState estate;

    estate.identifier = 0;
    if (unlikely(!parseExpression(state, &estate, 1, false, eatNewlines)))
    {
        return false;
    }
    finishAndStoreValueAt(state, &estate, variable);
    return true;
}

static bool finishLValue(ParseState *state, const ExpressionState *lvalue,
                         const ExpressionState *rvalue)
{
    switch (lvalue->expressionType)
    {
    case EXPRESSION_CONSTANT:
    case EXPRESSION_STORED:
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

static int parseDollarExpressionRest(ParseState *state)
{
    assert(*state->current == '$');
    state->current++;
    if (peekIdentifier(state))
    {
        IVAddRef(&temp, readVariableName(state));
        return NO_ERROR;
    }
    if (likely(*state->current == '('))
    {
        int value;
        state->current++;
        skipWhitespaceAndNewline(state);
        if (unlikely(peekOperator(state, ')')) ||
            unlikely(peekOperator(state, ']')) ||
            unlikely(peekOperator(state, '}')))
        {
            errorOnLine(state, state->lineBeforeSkip, "Expected expression inside '$()'");
            return ERROR_MISSING_RIGHTPAREN;
        }
        value = parseRValue(state, false, true);
        if (unlikely(!value))
        {
            return ERROR_INVALID_EXPRESSION;
        }
        if (unlikely(!readExpectedOperator(state, ')')))
        {
            return ERROR_MISSING_RIGHTPAREN;
        }
        IVAdd(&temp, value);
        return NO_ERROR;
    }
    error(state, "Expected variable or '(' after '$'. Got '%c'", *state->current);
    return ERROR_INVALID_EXPRESSION;
}

static bool parseDoubleQuotedString(ParseState *state, ExpressionState *estate)
{
    size_t oldTempSize = IVSize(&temp);
    size_t oldBTempSize = BVSize(&btemp);
    const byte *begin = ++state->current;
    const byte *end;
    const byte *terminatorBegin = null;
    size_t terminatorLength = 0;

    assert(begin[-1] == '"');
    if (peekOperator2(state, '"', '"'))
    {
        state->current += 2;
        skipWhitespace(state);
        terminatorBegin = state->current;
        if (unlikely(!isIdentifierCharacter(*state->current)))
        {
            error(state, "Expected terminator after '<<' operator");
            state->structuralError = true;
            return false;
        }
        while (isIdentifierCharacter(*++state->current));
        terminatorLength = (size_t)(state->current - terminatorBegin);
        skipWhitespace(state);
        if (unlikely(!peekReadNewline(state)))
        {
            error(state, "Expected newline to start multiline string literal");
            state->structuralError = true;
            return false;
        }
        begin = state->current;
    }
    for (;;)
    {
        switch (*state->current)
        {
        case '"':
        {
            if (terminatorBegin)
            {
                state->current++;
                break;
            }
            end = state->current++;
            goto finishString;
        }

        case '$':
        {
            size_t length;
            BVAddData(&btemp, begin, getOffset(state, begin));
            length = BVSize(&btemp) - oldBTempSize;
            if (length)
            {
                IVAdd(&temp, variableFromConstant(
                          state,
                          HeapCreateString((const char*)BVGetPointer(&btemp, oldBTempSize), length)));
                BVSetSize(&btemp, oldBTempSize);
            }
            switch (expect(parseDollarExpressionRest(state), NO_ERROR))
            {
            case NO_ERROR:
            case ERROR_INVALID_EXPRESSION:
                break;

            case ERROR_MISSING_RIGHTPAREN:
                goto error;
                break;
            default:
                unreachable;
            }
            begin = state->current;
            break;
        }

        case '\\':
            BVAddData(&btemp, begin, getOffset(state, begin));
            state->current++;
            switch (*state->current++)
            {
            case '\\':
            case '\'':
            case '"':
            case '$':
                begin = state->current - 1;
                continue;
            case '0': BVAdd(&btemp, '\0'); break;
            case 'f': BVAdd(&btemp, '\f'); break;
            case 'n': BVAdd(&btemp, '\n'); break;
            case 'r': BVAdd(&btemp, '\r'); break;
            case 't': BVAdd(&btemp, '\t'); break;
            case 'v': BVAdd(&btemp, '\v'); break;

            default:
                error(state, "Invalid escape sequence");
                continue;
            }
            begin = state->current;
            break;

        case '\r':
        case '\n':
            /* TODO: Unify line endings in resulting string */
            if (unlikely(!terminatorBegin))
            {
                error(state, "Newline in string literal");
                goto error;
            }
            if (unlikely(eof(state)))
            {
                error(state, "Unterminated multiline string literal");
                goto error;
            }
            state->current++;
            state->line++;
            writeOp(state, OP_LINE, state->line);
            if (!strncmp((const char*)terminatorBegin,
                         (const char*)state->current, terminatorLength))
            {
                end = state->current;
                state->current += terminatorLength;
                goto finishString;
            }
            break;

        default:
            state->current++;
            break;
        }
    }

finishString:
    BVAddData(&btemp, begin, (size_t)(end - begin));
    {
        vref s = HeapCreateString((const char*)BVGetPointer(&btemp, oldBTempSize),
                                  BVSize(&btemp) - oldBTempSize);
        BVSetSize(&btemp, oldBTempSize);
        if (IVSize(&temp) == oldTempSize)
        {
            parsedConstant(estate, s);
        }
        else if (unlikely(estate->parseConstant))
        {
            error(state, "Expected constant");
            parsedConstant(estate, HeapEmptyString);
            IVSetSize(&temp, oldTempSize);
        }
        else
        {
            int variable = createVariable(state);
            if (s != HeapEmptyString)
            {
                IVAdd(&temp, variableFromConstant(state, s));
            }
            writeOpFromTemp(state, OP_CONCAT_STRING, oldTempSize);
            IVAdd(state->bytecode, variable);
            estate->variable = variable;
            estate->expressionType = EXPRESSION_STORED;
        }
    }
    return true;

error:
    IVSetSize(&temp, oldTempSize);
    BVSetSize(&btemp, oldBTempSize);
    return false;
}

static bool parseSingleQuotedString(ParseState *state, ExpressionState *estate)
{
    const byte *begin = ++state->current;

    assert(begin[-1] == '\'');
    while (*state->current != '\'')
    {
        if (unlikely(*state->current == '\n' || *state->current == '\r'))
        {
            error(state, "Newline in string literal");
            return false;
        }
        state->current++;
    }
    parsedConstant(estate, HeapCreateString((const char*)begin, (size_t)(state->current - begin)));
    state->current++;
    return true;
}

static bool parseListRest(ParseState *state, ExpressionState *estate)
{
    ExpressionState estate2;
    bool constant;
    size_t oldTempSize = IVSize(&temp);
    skipWhitespaceAndNewline(state);
    if (readOperator(state, ')'))
    {
        parsedConstant(estate, HeapEmptyList);
        return true;
    }
    constant = true;
    for (;;)
    {
        estate2.identifier = 0;
        if (unlikely(!parseExpression(state, &estate2, 1, estate->parseConstant, true)))
        {
            IVSetSize(&temp, oldTempSize);
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

        skipWhitespaceAndNewline(state);
        if (readOperator(state, ','))
        {
            skipWhitespaceAndNewline(state);
            continue;
        }
        if (readOperator(state, ')'))
        {
            break;
        }
        readExpectedOperator(state, ')');
error:
        if (!skipToComma(state, ')'))
        {
            break;
        }
    }
    if (constant)
    {
        parsedConstant(estate, VCreateArrayFromVectorSegment(
                           &temp, oldTempSize, IVSize(&temp) - oldTempSize));
    }
    else
    {
        uint length = (uint)(IVSize(&temp) - oldTempSize);
        int *restrict write = IVGetAppendPointer(state->bytecode, 2 + length);
        *write++ = encodeOp(OP_LIST, (int)length);
        memcpy(write, IVGetPointer(&temp, oldTempSize), length * sizeof(*write));
        write[length] = estate->variable = createVariable(state);
        estate->expressionType = EXPRESSION_STORED;
    }
    IVSetSize(&temp, oldTempSize);
    return true;
}

static void finishBracketListItem(ParseState *state, const byte *begin, const byte *end,
                                  bool constant, size_t concatCount, size_t oldBTempSize)
{
    size_t length;
    vref s;
    BVAddData(&btemp, begin, (size_t)(end - begin));
    length = BVSize(&btemp) - oldBTempSize;
    if (length)
    {
        concatCount++;
        s = HeapCreateString((const char*)BVGetPointer(&btemp, oldBTempSize), length);
        BVSetSize(&btemp, oldBTempSize);
        if (constant)
        {
            IVAdd(&temp, intFromRef(s));
        }
        else
        {
            IVAdd(&temp, variableFromConstant(state, s));
        }
    }
    if (concatCount > 1)
    {
        int variable = createVariable(state);
        assert(!constant);
        writeOpFromTemp(state, OP_CONCAT_STRING, IVSize(&temp) - concatCount);
        IVAdd(state->bytecode, variable);
        IVAdd(&temp, variable);
    }
}

static bool parseBracketedListRest(ParseState *state, ExpressionState *estate)
{
    bool constant = true;
    size_t oldTempSize = IVSize(&temp);
    size_t oldBTempSize = BVSize(&btemp);
    size_t concatCount;
    int lineStart = state->line;

    skipWhitespaceAndNewline(state);
    for (;;)
    {
        const byte *begin;
itemParsed:
        if (readOperator(state, ']'))
        {
            break;
        }

        begin = state->current;
        if (peekNumber(state))
        {
            parseNumber(state, estate);
            if (*state->current == ' ' || *state->current == ']' ||
                *state->current == '\n' || *state->current == '\r')
            {
                IVAdd(&temp, constant ? intFromRef(estate->constant) :
                      variableFromConstant(state, estate->constant));
                skipWhitespaceAndNewline(state);
                continue;
            }
            state->current = begin;
        }

        concatCount = 0;
        for (;;)
        {
            switch (*state->current)
            {
            case ']':
                finishBracketListItem(state, begin, state->current, constant,
                                      concatCount, oldBTempSize);
                state->current++;
                goto done;

            case '\r':
            case '\n':
                if (unlikely(eof(state)))
                {
                    error(state,
                          "End of file reached while parsing '[]' expression. Started on line %d",
                          lineStart);
                    state->structuralError = true;
                    goto error;
                }
                /* fallthrough */
            case ' ':
                finishBracketListItem(state, begin, state->current, constant,
                                      concatCount, oldBTempSize);
                skipWhitespaceAndNewline(state);
                goto itemParsed;

            case '$':
            {
                if (constant)
                {
                    convertConstantsToValues(
                        state,
                        (int*)IVGetWritePointer(&temp, oldTempSize),
                        IVSize(&temp) - oldTempSize);
                }
                constant = false;
                if (begin != state->current)
                {
                    finishBracketListItem(state, begin, state->current, false,
                                          0, oldBTempSize);
                    concatCount++;
                }

                switch (expect(parseDollarExpressionRest(state), NO_ERROR))
                {
                case NO_ERROR:
                    concatCount++;
                    break;

                case ERROR_MISSING_RIGHTPAREN:
                    if (*state->current == '}')
                    {
                        if (unreadNewline(state))
                        {
                            goto error;
                        }
                    }
                    goto error;
                case ERROR_INVALID_EXPRESSION:
                    break;
                default:
                    unreachable;
                }
                begin = state->current;
                break;
            }

            case '\'':
            case '"':
                error(state, "TODO: quotes in bracketed list");
                state->current++;
                break;

            case '[':
                /* Unnecessary restriction, but helps editors match brackets. */
                error(state, "'[' must be escaped as '\\[' in bracketed list");
                state->current++;
                break;

            case '\\':
                BVAddData(&btemp, begin, (size_t)(state->current - begin));
                state->current++;
                switch (*state->current)
                {
                case '\\':
                case '\'':
                case '$':
                case '"':
                case '[':
                case ']':
                case ' ':
                    begin = state->current++;
                    continue;

                case '0': BVAdd(&btemp, '\0'); break;
                case 'f': BVAdd(&btemp, '\f'); break;
                case 'n': BVAdd(&btemp, '\n'); break;
                case 'r': BVAdd(&btemp, '\r'); break;
                case 't': BVAdd(&btemp, '\t'); break;
                case 'v': BVAdd(&btemp, '\v'); break;

                case '\n':
                    error(state, "Newline in escape sequence");
                    continue;

                default:
                    state->current++;
                    error(state, "Invalid escape sequence");
                    continue;
                }
                state->current++;
                begin = state->current;
                break;

            default:
                state->current++;
                break;
            }
        }
    }

done:
    if (constant)
    {
        parsedConstant(estate, VCreateArrayFromVectorSegment(
                           &temp, oldTempSize, IVSize(&temp) - oldTempSize));
    }
    else
    {
        uint length = (uint)(IVSize(&temp) - oldTempSize);
        int *restrict write = IVGetAppendPointer(state->bytecode, 2 + length);
        *write++ = encodeOp(OP_LIST, (int)length);
        memcpy(write, IVGetPointer(&temp, oldTempSize), length * sizeof(*write));
        write[length] = estate->variable = createVariable(state);
        estate->expressionType = EXPRESSION_STORED;
    }
    IVSetSize(&temp, oldTempSize);
    return true;

error:
    return false;
}

static bool parseInvocationRest(ParseState *state, ExpressionState *estate, vref ns, vref name)
{
    ExpressionState estateArgument;
    size_t oldTempSize = IVSize(&temp);
    int *restrict write;
    uint size;

    assert(!estate->identifier);
    estate->expressionType = EXPRESSION_MANY;
    estate->sideEffects = true;
    skipWhitespaceAndNewline(state);
    if (!readOperator(state, ')'))
    {
        do
        {
            int value;
            skipWhitespaceAndNewline(state);
            estateArgument.identifier = peekReadIdentifier(state);
            skipWhitespaceAndNewline(state);
            if (estateArgument.identifier && readOperator(state, ':'))
            {
                for (;;)
                {
                    IVAddRef(&temp, estateArgument.identifier);
                    estateArgument.identifier = 0;
                    skipWhitespaceAndNewline(state);
                    value = parseRValue(state, false, true);
                    if (unlikely(!value))
                    {
                        goto error;
                    }
                    IVAdd(&temp, value);
                    skipWhitespaceAndNewline(state);
                    if (!readOperator(state, ','))
                    {
                        break;
                    }
                    skipWhitespaceAndNewline(state);
                    estateArgument.identifier = peekReadIdentifier(state);
                    if (!estateArgument.identifier)
                    {
                        error(state, "Expected parameter name"); /* TODO: Nicer error message */
                        goto error;
                    }
                    skipWhitespaceAndNewline(state);
                    if (unlikely(!readExpectedOperator(state, ':')))
                    {
                        goto error;
                    }
                }
                break;
            }
            if (unlikely(!parseExpression(state, &estateArgument, 1, false, true)))
            {
                goto error;
            }
            IVAdd(&temp, 0);
            IVAdd(&temp, finishRValue(state, &estateArgument));
            skipWhitespaceAndNewline(state);
        }
        while (readOperator(state, ','));
        if (unlikely(!readExpectedOperator(state, ')')))
        {
            goto error;
        }
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

static bool parseNativeInvocationRest(ParseState *state, ExpressionState *estate, vref name)
{
    nativefunctionref function = NativeFindFunction(name);
    uint argumentCount;
    uint i;
    size_t oldTempSize = IVSize(&temp);
    int *restrict write;
    const int *restrict read;

    if (unlikely(!function))
    {
        error(state, "Unknown native function '%s'",
              HeapGetString(name));
        return false;
    }
    if (unlikely((uint)estate->valueCount != (NativeGetReturnValueCount(function) ? 1 : 0)))
    {
        error(state, "Native function returns %d values, but %d are handled",
              NativeGetReturnValueCount(function), estate->valueCount);
        return false;
    }
    argumentCount = NativeGetParameterCount(function);

    skipWhitespaceAndNewline(state);
    for (i = 0; i < argumentCount; i++)
    {
        int value = parseRValue(state, false, true);
        if (unlikely(!value))
        {
            IVSetSize(&temp, oldTempSize);
            return false;
        }
        IVAdd(&temp, value);
        if (i != argumentCount - 1)
        {
            if (unlikely(!readExpectedOperator(state, ',')))
            {
                IVSetSize(&temp, oldTempSize);
                return false;
            }
        }
        skipWhitespaceAndNewline(state);
    }
    argumentCount = (uint)(IVSize(&temp) - oldTempSize);
    write = IVGetAppendPointer(state->bytecode, 2 + argumentCount);
    *write++ = encodeOp(OP_INVOKE_NATIVE, intFromRef(function));
    read = IVGetPointer(&temp, oldTempSize);
    while (argumentCount--)
    {
        *write++ = *read++;
    }
    estate->expressionType = EXPRESSION_STORED;
    *write++ = estate->variable = createVariable(state);
    estate->sideEffects = true;
    IVSetSize(&temp, oldTempSize);
    return readExpectedOperator(state, ')');
}

static bool parseBinaryOperationRest(
    ParseState *state, ExpressionState *estate,
    bool (*parseExpressionRest)(ParseState*, ExpressionState*),
    Instruction instruction)
{
    int value = finishRValue(state, estate);
    int value2;
    int variable;
    skipWhitespaceAndNewline(state);
    if (unlikely(!parseExpressionRest(state, estate)))
    {
        return false;
    }
    value2 = finishRValue(state, estate);
    skipExpressionWhitespace(state, estate);
    variable = createVariable(state);
    writeOp3(state, instruction, value, value2, variable);
    estate->variable = variable;
    estate->expressionType = EXPRESSION_STORED;
    return true;
}

static bool parseExpression11(ParseState *state, ExpressionState *estate)
{
    vref identifier = estate->identifier;
    vref string;
    vref ns;

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
                parsedConstant(estate, HeapTrue);
                return true;
            }
            if (identifier == keywordFalse)
            {
                parsedConstant(estate, HeapFalse);
                return true;
            }
            if (identifier == keywordList)
            {
                skipWhitespaceAndNewline(state);
                if (unlikely(!readExpectedOperator(state, '(')))
                {
                    return false;
                }
                return parseListRest(state, estate);
            }
            if (likely(identifier == keywordNull))
            {
                parsedConstant(estate, 0);
                return true;
            }
            error(state, "Unexpected keyword '%s'",
                  HeapGetString(identifier));
            return false;
        }
        if (unlikely(estate->parseConstant))
        {
            error(state, "Expected constant");
            return false;
        }
        if (!peekOperator2(state, '.', '.') && readOperator(state, '.'))
        {
            if (state->ns == NAMESPACE_DON && identifier == identifierNative)
            {
                identifier = readVariableName(state);
                if (unlikely(!identifier || !readExpectedOperator(state, '(')))
                {
                    return false;
                }
                return parseNativeInvocationRest(state, estate, identifier);
            }
            ns = identifier;
            identifier = readVariableName(state);
            if (unlikely(!identifier))
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
    if (peekNumber(state))
    {
        parseNumber(state, estate);
        return true;
    }
    if (*state->current == '"')
    {
        return parseDoubleQuotedString(state, estate);
    }
    if (*state->current == '\'')
    {
        return parseSingleQuotedString(state, estate);
    }
    if (readOperator(state, '('))
    {
        bool oldEatNewlines = estate->eatNewlines;
        skipWhitespace(state);
        if (unlikely(!parseExpression(state, estate, estate->valueCount,
                                      estate->parseConstant, true)))
        {
            return false;
        }
        /* TODO: Prevent use as lvalue */
        estate->eatNewlines = oldEatNewlines;
        estate->sideEffects = false;
        return readExpectedOperator(state, ')');
    }
    if (readOperator(state, '['))
    {
        return parseBracketedListRest(state, estate);
    }
    if (likely(readOperator(state, '@')))
    {
        int variable;
        string = readFilename(state);
        if (unlikely(!string))
        {
            return false;
        }
        if (!strchr(HeapGetString(string), '*'))
        {
            parsedConstant(estate, HeapCreatePath(string));
            return true;
        }
        /* TODO: @{} syntax */
        variable = createVariable(state);
        writeOp2(state, OP_FILELIST, intFromRef(string), variable);
        estate->variable = variable;
        estate->expressionType = EXPRESSION_STORED;
        return true;
    }
    error(state, "Invalid expression");
    return false;
}

static bool parseExpression10(ParseState *state, ExpressionState *estate)
{
    if (unlikely(!parseExpression11(state, estate)))
    {
        return false;
    }
    for (;;)
    {
        skipExpressionWhitespace(state, estate);
        if (readOperator(state, '['))
        {
            int value = finishRValue(state, estate);
            int index;
            int variable;
            skipWhitespaceAndNewline(state);
            index = parseRValue(state, estate->parseConstant, true);
            if (unlikely(!index))
            {
                return false;
            }
            skipWhitespaceAndNewline(state);
            if (unlikely(!readExpectedOperator(state, ']')))
            {
                return false;
            }
            variable = createVariable(state);
            writeOp3(state, OP_INDEXED_ACCESS, value, index, variable);
            estate->variable = variable;
            estate->expressionType = EXPRESSION_STORED;
            continue;
        }
        if (!peekOperator2(state, '.', '.') && readOperator(state, '.'))
        {
            assert(false); /* TODO: handle namespace */
        }
        break;
    }
    return true;
}

static bool parseExpression9(ParseState *state, ExpressionState *estate)
{
    int variable;
    int value;
    if (readOperator(state, '-'))
    {
        assert(!readOperator(state, '-')); /* TODO: -- operator */
        if (unlikely(!parseExpression10(state, estate)))
        {
            return false;
        }
        value = finishRValue(state, estate);
        skipExpressionWhitespace(state, estate);
        variable = createVariable(state);
        writeOp2(state, OP_NEG, value, variable);
        estate->variable = variable;
        estate->expressionType = EXPRESSION_STORED;
        return true;
    }
    if (readOperator(state, '!'))
    {
        if (unlikely(!parseExpression10(state, estate)))
        {
            return false;
        }
        value = finishRValue(state, estate);
        skipExpressionWhitespace(state, estate);
        variable = createVariable(state);
        writeOp2(state, OP_NOT, value, variable);
        estate->variable = variable;
        estate->expressionType = EXPRESSION_STORED;
        return true;
    }
    if (readOperator(state, '~'))
    {
        if (unlikely(!parseExpression10(state, estate)))
        {
            return false;
        }
        value = finishRValue(state, estate);
        skipExpressionWhitespace(state, estate);
        variable = createVariable(state);
        writeOp2(state, OP_INV, value, variable);
        estate->variable = variable;
        estate->expressionType = EXPRESSION_STORED;
        return true;
    }
    return parseExpression10(state, estate);
}

static bool parseExpression8(ParseState *state, ExpressionState *estate)
{
    if (unlikely(!parseExpression9(state, estate)))
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
            if (unlikely(!parseBinaryOperationRest(
                             state, estate, parseExpression9, OP_MUL)))
            {
                return false;
            }
            skipExpressionWhitespace(state, estate);
            continue;
        }
        if (readOperator(state, '/'))
        {
            if (reverseIfOperator(state, '='))
            {
                return true;
            }
            if (unlikely(!parseBinaryOperationRest(
                             state, estate, parseExpression9, OP_DIV)))
            {
                return false;
            }
            skipExpressionWhitespace(state, estate);
            continue;
        }
        if (readOperator(state, '%'))
        {
            if (reverseIfOperator(state, '='))
            {
                return true;
            }
            if (unlikely(!parseBinaryOperationRest(
                             state, estate, parseExpression9, OP_REM)))
            {
                return false;
            }
            skipExpressionWhitespace(state, estate);
            continue;
        }
        break;
    }
    return true;
}

static bool parseExpression7(ParseState *state, ExpressionState *estate)
{
    if (unlikely(!parseExpression8(state, estate)))
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
            if (unlikely(!parseBinaryOperationRest(
                             state, estate, parseExpression8, OP_ADD)))
            {
                return false;
            }
            continue;
        }
        else if (readOperator(state, '-'))
        {
            if (peekOperator(state, '='))
            {
                state->current--;
                return true;
            }
            assert(!readOperator(state, '-')); /* TODO: -- operator */
            if (unlikely(!parseBinaryOperationRest(
                             state, estate, parseExpression8, OP_SUB)))
            {
                return false;
            }
            continue;
        }
        break;
    }
    return true;
}

static bool parseExpression6(ParseState *state, ExpressionState *estate)
{
    if (unlikely(!parseExpression7(state, estate)))
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

static bool parseExpression5(ParseState *state, ExpressionState *estate)
{
    if (unlikely(!parseExpression6(state, estate)))
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

static bool parseExpression4(ParseState *state, ExpressionState *estate)
{
    if (unlikely(!parseExpression5(state, estate)))
    {
        return false;
    }
    for (;;)
    {
        if (readOperator2(state, '.', '.'))
        {
            if (unlikely(!parseBinaryOperationRest(
                             state, estate, parseExpression5, OP_RANGE)))
            {
                return false;
            }
            continue;
        }
        if (readOperator2(state, ':', ':'))
        {
            if (unlikely(!parseBinaryOperationRest(
                             state, estate, parseExpression5, OP_CONCAT_LIST)))
            {
                return false;
            }
            continue;
        }
        break;
    }
    return true;
}

static bool parseExpression3(ParseState *state, ExpressionState *estate)
{
    if (unlikely(!parseExpression4(state, estate)))
    {
        return false;
    }
    for (;;)
    {
        if (readOperator2(state, '=', '='))
        {
            if (unlikely(!parseBinaryOperationRest(state, estate, parseExpression4, OP_EQUALS)))
            {
                return false;
            }
            continue;
        }
        if (readOperator2(state, '!', '='))
        {
            if (unlikely(!parseBinaryOperationRest(state, estate, parseExpression4, OP_NOT_EQUALS)))
            {
                return false;
            }
            continue;
        }
        if (readOperator2(state, '<', '='))
        {
            if (unlikely(!parseBinaryOperationRest(state, estate, parseExpression4,
                                                   OP_LESS_EQUALS)))
            {
                return false;
            }
            continue;
        }
        if (readOperator2(state, '>', '='))
        {
            if (unlikely(!parseBinaryOperationRest(state, estate, parseExpression4,
                                                   OP_GREATER_EQUALS)))
            {
                return false;
            }
            continue;
        }
        if (readOperator(state, '<'))
        {
            if (unlikely(!parseBinaryOperationRest(state, estate, parseExpression4, OP_LESS)))
            {
                return false;
            }
            continue;
        }
        if (readOperator(state, '>'))
        {
            if (unlikely(!parseBinaryOperationRest(state, estate, parseExpression4, OP_GREATER)))
            {
                return false;
            }
            continue;
        }
        break;
    }
    return true;
}

static bool parseExpression2(ParseState *state, ExpressionState *estate)
{
    int target;

    if (unlikely(!parseExpression3(state, estate)))
    {
        return false;
    }
    for (;;)
    {
        if (readOperator2(state, '&', '&'))
        {
            int variable = createVariable(state);
            finishAndStoreValueAt(state, estate, variable);
            skipWhitespaceAndNewline(state);
            target = createJumpTarget(state);
            writeBranch(state, target, OP_BRANCH_FALSE_INDEXED, variable);
            if (unlikely(!parseExpression3(state, estate)))
            {
                return false;
            }
            finishAndStoreValueAt(state, estate, variable);
            placeJumpTargetHere(state, target);
            estate->expressionType = EXPRESSION_STORED;
            estate->variable = variable;
            skipExpressionWhitespace(state, estate);
            continue;
        }
        if (readOperator2(state, '|', '|'))
        {
            int variable = createVariable(state);
            finishAndStoreValueAt(state, estate, variable);
            skipWhitespaceAndNewline(state);
            target = createJumpTarget(state);
            writeBranch(state, target, OP_BRANCH_TRUE_INDEXED, variable);
            if (unlikely(!parseExpression3(state, estate)))
            {
                return false;
            }
            finishAndStoreValueAt(state, estate, variable);
            placeJumpTargetHere(state, target);
            estate->expressionType = EXPRESSION_STORED;
            estate->variable = variable;
            skipExpressionWhitespace(state, estate);
            continue;
        }
        break;
    }
    return true;
}

static bool parseExpressionRest(ParseState *state, ExpressionState *estate)
{
    const bool parseConstant = estate->parseConstant;

    if (unlikely(!parseExpression2(state, estate)))
    {
        return false;
    }
    if (readOperator(state, '?'))
    {
        int target1 = createJumpTarget(state);
        int target2 = createJumpTarget(state);
        int variable = createVariable(state);
        assert(!parseConstant); /* TODO */
        skipWhitespaceAndNewline(state);
        writeBranch(state, target1, OP_BRANCH_FALSE_INDEXED, finishRValue(state, estate));
        if (unlikely(!parseAndStoreValueAt(state, variable, true)) ||
            unlikely(!readExpectedOperator(state, ':')))
        {
            return false;
        }
        writeJump(state, target2);
        placeJumpTargetHere(state, target1);
        skipWhitespaceAndNewline(state);
        if (unlikely(!parseAndStoreValueAt(state, variable, false)))
        {
            return false;
        }
        placeJumpTargetHere(state, target2);
        skipExpressionWhitespace(state, estate);
        estate->expressionType = EXPRESSION_STORED;
        estate->variable = variable;
        return true;
    }
    return true;
}

static bool parseExpression(ParseState *state, ExpressionState *estate,
                            int valueCount, bool constant, bool eatNewlines)
{
    estate->valueCount = valueCount;
    estate->parseConstant = constant;
    estate->eatNewlines = eatNewlines;
    estate->sideEffects = false;
    return parseExpressionRest(state, estate);
}

static bool parseAssignmentExpressionRest(ParseState *state, ExpressionState *estate,
                                          Instruction instruction)
{
    ExpressionState estate2;
    int value = finishRValue(state, estate);
    int value2;
    int variable;
    skipWhitespaceAndNewline(state);
    value2 = parseRValue(state, false, false);
    if (unlikely(!value2))
    {
        return false;
    }
    variable = createVariable(state);
    writeOp3(state, instruction, value, value2, variable);
    estate2.variable = variable;
    estate2.expressionType = EXPRESSION_STORED;
    return finishLValue(state, estate, &estate2);
}

static bool parseExpressionStatement(ParseState *state, vref identifier)
{
    ExpressionState estate, rvalue;

    estate.identifier = identifier;
    if (unlikely(!parseExpression(state, &estate, 0, false, false)))
    {
        return false;
    }
    if (readOperator(state, '='))
    {
        skipWhitespaceAndNewline(state);
        rvalue.identifier = 0;
        return parseExpression(state, &rvalue, 1, false, false) &&
            finishLValue(state, &estate, &rvalue);
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
    if (estate.sideEffects)
    {
        assert(estate.valueCount == 0);
        return true;
    }
    if (likely(peekIdentifier(state)))
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
            if (unlikely(!parseExpression(state, &estate, 0, false, false)))
            {
                goto error;
            }
            BVAddData(&btemp, (byte*)&estate, sizeof(estate));
            skipWhitespace(state);
        }
        while (peekIdentifier(state));
        if (unlikely(!readExpectedOperator(state, '=')))
        {
            goto error;
        }
        skipWhitespace(state);
        rvalue.identifier = 0;
        returnValueCount = (int)((BVSize(&btemp) - oldBTempSize) / sizeof(estate));
        if (unlikely(!parseExpression(state, &rvalue, returnValueCount, false, false)))
        {
            goto error;
        }
        if (unlikely(rvalue.expressionType != EXPRESSION_MANY))
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
            if (unlikely(!finishLValue(state, &estate, &rvalue)))
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

static bool parseReturnRest(ParseState *state)
{
    size_t oldTempSize;
    int value;

    if (peekNewline(state))
    {
        writeOp(state, OP_RETURN_VOID, 0);
        return true;
    }
    if (unlikely(state->isTarget))
    {
        error(state, "Targets can't return values");
    }
    oldTempSize = IVSize(&temp);
    for (;;)
    {
        value = parseRValue(state, false, false);
        if (unlikely(!value))
        {
            IVSetSize(&temp, oldTempSize);
            return false;
        }
        IVAdd(&temp, value);
        if (peekNewline(state))
        {
            writeOpFromTemp(state, OP_RETURN, oldTempSize);
            return true;
        }
        skipWhitespace(state);
    }
}

static void parseBlock(ParseState *state)
{
    vref identifier;

    skipWhitespace(state);
    if (peekNewline(state))
    {
        readNewline(state);
    }
    if (unlikely(!readOperator(state, '{')))
    {
        state->structuralError = true;
        error(state, "Expected operator '{'");
        return;
    }

    for (;;)
    {
        if (skipBlockWhitespace(state))
        {
            return;
        }

        identifier = peekReadIdentifier(state);
        if (likely(identifier))
        {
            if (isKeyword(identifier))
            {
                if (unlikely(identifier > maxStatementKeyword))
                {
                    error(state, "Not a statement");
                    goto statementError;
                }
                skipWhitespace(state);
                if (identifier == keywordIf)
                {
                    int conditionTarget = createJumpTarget(state);
                    int afterIfTarget;
                    int condition = parseRValue(state, false, false);
                    if (unlikely(!condition))
                    {
                        state->structuralError = true;
                        goto statementError;
                    }
                    writeBranch(state, conditionTarget, OP_BRANCH_FALSE_INDEXED, condition);

                    parseBlock(state);
                    if (skipBlockWhitespace(state))
                    {
                        placeJumpTargetHere(state, conditionTarget);
                        return;
                    }
                    if (!peekReadKeywordElse(state))
                    {
                        placeJumpTargetHere(state, conditionTarget);
                        continue;
                    }

                    afterIfTarget = createJumpTarget(state);
                    for (;;)
                    {
                        writeJump(state, afterIfTarget);
                        placeJumpTargetHere(state, conditionTarget);
                        if (skipBlockWhitespace(state))
                        {
                            error(state, "Expected block after else");
                            return;
                        }
                        identifier = peekReadIdentifier(state);
                        if (identifier != keywordIf)
                        {
                            if (unlikely(identifier))
                            {
                                error(state, "Garbage after else");
                                goto statementError;
                            }
                            parseBlock(state);
                            break;
                        }
                        skipWhitespace(state);
                        condition = parseRValue(state, false, false);
                        if (unlikely(!condition))
                        {
                            goto statementError;
                        }
                        conditionTarget = createJumpTarget(state);
                        writeBranch(state, conditionTarget, OP_BRANCH_FALSE_INDEXED, condition);
                        parseBlock(state);
                        if (skipBlockWhitespace(state))
                        {
                            return;
                        }
                        if (!peekReadKeywordElse(state))
                        {
                            placeJumpTargetHere(state, conditionTarget);
                            break;
                        }
                    }
                    placeJumpTargetHere(state, afterIfTarget);
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
                    if (unlikely(!identifier))
                    {
                        goto statementError;
                    }
                    skipWhitespace(state);
                    if (unlikely(!readExpectedKeyword(state, keywordIn)))
                    {
                        goto statementError;
                    }
                    skipWhitespace(state);
                    iterCollection = parseRValue(state, false, false);
                    if (unlikely(!iterCollection))
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
                    parseBlock(state);
                    writeJump(state, loopTop);
                    placeJumpTargetHere(state, afterLoop);
                }
                else if (identifier == keywordReturn)
                {
                    if (unlikely(!parseReturnRest(state)))
                    {
                        goto statementError;
                    }
                }
                else if (identifier == keywordWhile)
                {
                    int loopTop = createJumpTargetHere(state);
                    int afterLoop = createJumpTarget(state);
                    int condition = parseRValue(state, false, false);
                    if (unlikely(!condition))
                    {
                        goto statementError;
                    }
                    writeBranch(state, afterLoop, OP_BRANCH_FALSE_INDEXED, condition);
                    parseBlock(state);
                    writeJump(state, loopTop);
                    placeJumpTargetHere(state, afterLoop);
                }
                else if (unlikely(identifier == keywordElse))
                {
                    if (!state->structuralError)
                    {
                        error(state, "else without matching if");
                    }
                    goto statementError;
                }
                else
                {
                    unreachable;
                }
            }
            else
            {
                if (unlikely(!parseExpressionStatement(state, identifier)))
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
        continue;

statementError:
        while (*state->current != '\n')
        {
            state->current++;
        }
    }
}

static void parseFunctionBody(ParseState *state)
{
    state->jumpCount = 0;
    state->jumpTargetCount = 0;
    state->unnamedVariableCount = 0;
    state->structuralError = false;
    parseBlock(state);
    if (!eof(state))
    {
        skipWhitespace(state);
        if (!peekNewline(state))
        {
            uint indent;
            if (!state->structuralError)
            {
                error(state, "Garbage after function body");
            }
            do
            {
                while (*state->current != '\n')
                {
                    state->current++;
                }
                indent = readNewline(state);
            }
            while (indent);
        }
    }
    writeOp(state, OP_RETURN_VOID, 0);
    state->program->maxJumpCount = max(state->program->maxJumpCount, state->jumpCount);
    state->program->maxJumpTargetCount = max(state->program->maxJumpTargetCount,
                                             (uint)state->jumpTargetCount);
}

static bool parseFunctionDeclarationRest(ParseState *state, vref functionName)
{
    ExpressionState estate;
    bool requireDefaultValues = false;
    int varargIndex = INT_MAX;
    size_t oldTempSize = IVSize(&temp);

    if (!readOperator(state, ')'))
    {
        for (;;)
        {
            int value = INT_MAX;
            vref parameterName = peekReadIdentifier(state);
            if (unlikely(!parameterName || isKeyword(parameterName)))
            {
                error(state, "Expected parameter name or ')'");
                goto error;
            }
            skipWhitespace(state);
            if (readOperator(state, ':'))
            {
                requireDefaultValues = true;
                skipWhitespace(state);
                estate.identifier = 0;
                if (unlikely(!parseExpression(state, &estate, 1, true, true)))
                {
                    goto error;
                }
                assert(estate.expressionType == EXPRESSION_CONSTANT);
                value = variableFromConstant(state, estate.constant);
            }
            else if (unlikely(requireDefaultValues))
            {
                error(state, "Default value for parameter '%s' required",
                      HeapGetString(parameterName));
                goto error;
            }
            else if (readOperator3(state, '.', '.', '.'))
            {
                assert(varargIndex == INT_MAX); /* TODO: error message */
                varargIndex = (int)(IVSize(&temp) - oldTempSize) / 2;
                requireDefaultValues = true;
                skipWhitespace(state);
                value = variableFromConstant(state, HeapEmptyList);
            }
            IVAdd(&temp, intFromRef(parameterName));
            IVAdd(&temp, value);

            skipWhitespaceAndNewline(state);
            if (readOperator(state, ','))
            {
                skipWhitespaceAndNewline(state);
                continue;
            }
            if (readOperator(state, ')'))
            {
                break;
            }
            readExpectedOperator(state, ')');
    error:
            if (!skipToComma(state, ')'))
            {
                break;
            }
        }
    }
    IVAdd(&state->program->functions, (int)IVSize(state->bytecode));
    {
        int parameterCount = (int)(IVSize(&temp) - oldTempSize) / 2;
        int *write = IVGetAppendPointer(state->bytecode, 3 + (size_t)parameterCount * 2);
        const int *read = IVGetPointer(&temp, oldTempSize);
        *write++ = encodeOp(OP_FUNCTION_UNLINKED, intFromRef(functionName));
        *write++ = parameterCount;
        *write++ = varargIndex;
        while (parameterCount--)
        {
            *write++ = *read++;
            *write++ = *read++;
        }
        IVSetSize(&temp, oldTempSize);
    }
    return true;
}

void ParserAddKeywords(void)
{
    keywordElse = StringPoolAdd("else");
    keywordFor = StringPoolAdd("for");
    keywordIf = StringPoolAdd("if");
    keywordReturn = StringPoolAdd("return");
    keywordWhile = StringPoolAdd("while");
    maxStatementKeyword = keywordWhile;

    keywordFalse = StringPoolAdd("false");
    keywordFn = StringPoolAdd("fn");
    keywordIn = StringPoolAdd("in");
    keywordList = StringPoolAdd("list");
    keywordNull = StringPoolAdd("null");
    keywordTarget = StringPoolAdd("target");
    keywordTrue = StringPoolAdd("true");
    maxKeyword = keywordTrue;

    identifierNative = StringPoolAdd("native");
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

void ParseFile(ParsedProgram *program, const char *filename, size_t filenameLength, namespaceref ns)
{
    ParseState state;
    File file;
    size_t size;
    byte *buffer;

    assert(filename);
    assert(!filename[filenameLength]);
    state.ns = ns;
    state.line = 1;
    state.bytecode = &program->bytecode;
    state.program = program;
    state.constants = &program->constants;

    FileOpen(&file, filename, filenameLength);
    size = FileSize(&file);
    if (unlikely(size >= SSIZE_MAX))
    {
        Fail("File too big: %s\n", filename);
    }
    buffer = (byte*)malloc(size + 1);
    FileRead(&file, buffer, size);
    FileClose(&file);
    /* Make sure the file ends with a newline. That way there is only a need to
       look for end-of-file on newlines. */
    buffer[size] = '\n';

    state.start = buffer;
    state.current = state.start;
    state.limit = state.start + size;

    writeOp(&state, OP_FILE, intFromRef(ns));
    IVAppendString(state.bytecode, filename, filenameLength);

    while (!eof(&state))
    {
        if (peekIdentifier(&state))
        {
            vref identifier = readIdentifier(&state);
            skipWhitespace(&state);
            if (identifier == keywordFn)
            {
                vref name = peekReadIdentifier(&state);
                if (unlikely(!name))
                {
                    error(&state, "Expected function name after 'fn' keyword");
                    /* TODO: skip parsing function body, but continue parsing after it */
                    goto error;
                }
                if (unlikely(NamespaceAddFunction(ns, name, (int)IVSize(&program->functions)) >= 0))
                {
                    /* TODO: blacklist function */
                    error(&state, "Multiple functions or targets with name '%s'",
                          HeapGetString(name));
                }
                if (unlikely(!readOperator(&state, '(')))
                {
                    IVAdd(&program->functions, 0); /* TODO: This will probably cause a crash when linking */
                    error(&state, "Expected operator '(' after function name");
                    /* TODO: skip parsing function body, but continue parsing after it */
                    goto error;
                }
                if (unlikely(!parseFunctionDeclarationRest(&state, name)))
                {
                    /* TODO: skip parsing function body, but continue parsing after it */
                    goto error;
                }
                state.isTarget = false;
                parseFunctionBody(&state);
            }
            else if (identifier == keywordTarget)
            {
                vref name = peekReadIdentifier(&state);
                if (unlikely(NamespaceAddTarget(ns, name, (int)IVSize(&program->functions)) >= 0))
                {
                    /* TODO: blacklist function */
                    error(&state, "Multiple functions or targets with name '%s'",
                          HeapGetString(name));
                }
                IVAdd(&program->functions, (int)IVSize(state.bytecode));
                writeOp3(&state, OP_FUNCTION_UNLINKED, intFromRef(name), 0, 0);
                state.isTarget = true;
                parseFunctionBody(&state);
            }
            else
            {
                ExpressionState estate;
                skipWhitespace(&state);
                if (unlikely(!readOperator(&state, '=')))
                {
                    error(&state, "Invalid declaration");
                    /* TODO: skip declaration */
                    goto error;
                }
                skipWhitespace(&state);
                estate.identifier = 0;
                if (parseExpression(&state, &estate, 1, true, false))
                {
                    assert(estate.expressionType == EXPRESSION_CONSTANT);
                    if (unlikely(!peekNewline(&state)))
                    {
                        error(&state, "Garbage after variable declaration");
                    }
                    else
                    {
                        if (unlikely(NamespaceAddField(
                                         ns, identifier, (int)IVSize(&program->fields)) >= 0))
                        {
                            error(&state, "Multiple fields with name '%s'",
                                  HeapGetString(identifier));
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
        else if (unlikely(!peekReadNewline(&state)))
        {
            error(&state, "Unsupported character: '%c'", *state.current);
            skipEndOfLine(&state);
        }
    }

error:
    free(buffer);
}
