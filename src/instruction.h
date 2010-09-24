#define INSTRUCTION_H

typedef enum
{
    OP_NULL,
    OP_TRUE,
    OP_FALSE,
    OP_INTEGER,
    OP_STRING,

    OP_LOAD,
    OP_STORE,

    OP_EQUALS,
    OP_NOT_EQUALS,
    OP_LESS_EQUALS,
    OP_GREATER_EQUALS,
    OP_LESS,
    OP_GREATER,
    OP_ADD,
    OP_SUB,
    OP_CONCAT,

    OP_JUMP,
    OP_BRANCH_FALSE,
    OP_RETURN,
    OP_RETURN_VOID,
    OP_INVOKE,
    OP_INVOKE_NATIVE
} Instruction;
