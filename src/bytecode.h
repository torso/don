extern nonnull uint BytecodeReadUint(const byte **bytecode);
extern nonnull int16 BytecodeReadInt16(const byte **bytecode);
extern nonnull uint16 BytecodeReadUint16(const byte **bytecode);
extern nonnull ref_t BytecodeReadRef(const byte **bytecode);
#define BytecodeReadInt (int)BytecodeReadUint

/*
  Disassembles one instruction at bytecode. base should point to the beginning of the bytecode. */
extern nonnull const byte *BytecodeDisassembleInstruction(const byte *bytecode, const byte *base);

extern nonnull void BytecodeDisassemble(const byte *bytecode, const byte *bytecodeLimit);
