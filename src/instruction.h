#define INSTRUCTION_H

typedef enum
{
    OP_NULL,
    OP_TRUE,
    OP_FALSE,
    OP_INTEGER,
    OP_STRING,
    OP_EMPTY_LIST,
    OP_LIST,
    OP_FILE,
    OP_FILESET,

    OP_POP,
    OP_DUP,
    OP_REORDER_STACK,
    OP_LOAD,
    OP_STORE,
    OP_LOAD_FIELD,
    OP_STORE_FIELD,

    OP_CAST_BOOLEAN,
    OP_NOT,
    OP_NEG,
    OP_INV,
    OP_ITER_INIT,
    OP_ITER_NEXT,

    OP_EQUALS,
    OP_NOT_EQUALS,
    OP_LESS_EQUALS,
    OP_GREATER_EQUALS,
    OP_LESS,
    OP_GREATER,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_REM,
    OP_CONCAT_LIST,
    OP_CONCAT_STRING,
    OP_INDEXED_ACCESS,
    OP_RANGE,

    OP_JUMP,
    OP_BRANCH_TRUE,
    OP_BRANCH_FALSE,
    OP_RETURN,
    OP_RETURN_VOID,
    OP_INVOKE,
    OP_INVOKE_NATIVE,

    OP_UNKNOWN_VALUE
} Instruction;
