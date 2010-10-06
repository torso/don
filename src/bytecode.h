extern nonnull uint BytecodeReadUint(const byte **bytecode);
extern nonnull uint16 BytecodeReadUint16(const byte **bytecode);
#define BytecodeReadInt (int)BytecodeReadUint

/*
  Disassembles one instruction at bytecode. base should point to the beginning
  of the current function.
*/
extern nonnull const byte *BytecodeDisassembleInstruction(const byte *bytecode,
                                                          const byte *base);

/*
  Disassembles an entire function. The end of the function is automatically
  detected through reachability.
*/
extern nonnull void BytecodeDisassembleFunction(const byte *bytecode,
                                                const byte *bytecodeLimit);
