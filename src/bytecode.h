/* Disassembles one instruction at bytecode. base should point to the beginning of the bytecode. */
extern nonnull const int *BytecodeDisassembleInstruction(const int *bytecode, const int *base);

extern nonnull void BytecodeDisassemble(const int *bytecode, const int *bytecodeLimit);

extern nonnull int BytecodeLineNumber(const int *lineNumbers, size_t bytecodeOffset,
                                      const char **filename);
