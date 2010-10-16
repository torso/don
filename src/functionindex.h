/*
  This class holds information about functions (including targets).

  The file, line number and file offset is used when parsing the function.

  The argument names are used when parsing other functions. The function
  signature is parsed eagerly, and the body lazily.

  The bytecode offset is used when running a function. While parsing, the offset
  will be the offset in the intermediate bytecode format.


  To add a new function, call FunctionIndexBeginFunction. Then call
  FunctionIndexAddArgument as many times as needed. When done, call
  FunctionIndexFinishFunction.


  Before looking up functions, the index needs to be built. After this has been
  done, no more functions can be added.
*/

extern ErrorCode FunctionIndexInit(void);
extern void FunctionIndexDispose(void);

extern functionref FunctionIndexAddFunction(stringref name, fileref file,
                                            uint line, uint fileOffset);
extern ErrorCode FunctionIndexAddParameter(functionref function, stringref name,
                                           fieldref value, boolean vararg);
extern void FunctionIndexFinishParameters(functionref function,
                                          uint line, uint fileOffset);

extern functionref FunctionIndexGetFirstFunction(void);
extern functionref FunctionIndexGetNextFunction(functionref function);

extern functionref FunctionIndexGetFunctionFromBytecode(uint bytecodeOffset);
extern stringref FunctionIndexGetName(functionref function);
extern fileref FunctionIndexGetFile(functionref function);
extern uint FunctionIndexGetLine(functionref function);
extern uint FunctionIndexGetFileOffset(functionref function);
extern boolean FunctionIndexIsTarget(functionref function);
extern uint FunctionIndexGetBytecodeOffset(functionref function);
extern void FunctionIndexSetBytecodeOffset(functionref function, uint offset);
extern uint FunctionIndexGetParameterCount(functionref function);
extern const ParameterInfo *FunctionIndexGetParameterInfo(functionref function);
extern boolean FunctionIndexHasVararg(functionref function);
extern uint FunctionIndexGetVarargIndex(functionref function);
extern uint FunctionIndexGetLocalsCount(functionref function);
extern stringref FunctionIndexGetLocalName(functionref function,
                                                uint16 local);
extern nonnull ErrorCode FunctionIndexSetLocals(functionref function,
                                                const inthashmap *locals,
                                                uint count);
