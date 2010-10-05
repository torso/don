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

extern ErrorCode FunctionIndexBeginFunction(stringref name);
extern ErrorCode FunctionIndexAddParameter(stringref name, boolean required);
extern functionref FunctionIndexFinishFunction(fileref file, uint line,
                                               uint fileOffset);

extern pure functionref FunctionIndexGetFirstFunction(void);
extern pure functionref FunctionIndexGetNextFunction(functionref function);

extern pure functionref FunctionIndexGetFunctionFromBytecode(uint bytecodeOffset);
extern pure stringref FunctionIndexGetName(functionref function);
extern pure fileref FunctionIndexGetFile(functionref function);
extern pure uint FunctionIndexGetLine(functionref function);
extern pure uint FunctionIndexGetFileOffset(functionref function);
extern pure boolean FunctionIndexIsTarget(functionref function);
extern pure uint FunctionIndexGetBytecodeOffset(functionref function);
extern void FunctionIndexSetBytecodeOffset(functionref function, uint offset);
extern pure uint FunctionIndexGetParameterCount(functionref function);
extern pure const stringref *FunctionIndexGetParameterNames(functionref function);
extern pure uint FunctionIndexGetMinimumArgumentCount(functionref function);
extern pure uint FunctionIndexGetLocalsCount(functionref function);
extern pure stringref FunctionIndexGetLocalName(functionref function,
                                                uint16 local);
extern nonnull ErrorCode FunctionIndexSetLocals(functionref function,
                                                const inthashmap *locals,
                                                uint count);
