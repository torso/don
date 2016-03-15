/* Disassembles one instruction at bytecode. base should point to the beginning of the bytecode. */
nonnull const int *BytecodeDisassembleInstruction(const int *bytecode, const int *base);

nonnull void BytecodeDisassemble(const int *bytecode, const int *bytecodeLimit);

nonnull int BytecodeLineNumber(const int *lineNumbers, int bytecodeOffset, const char **filename);
