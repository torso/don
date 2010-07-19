#ifndef STRINGPOOL_H
#error stringpool.h not included
#endif
#ifndef FILEINDEX_H
#error fileindex.h not included
#endif

/*
  This class holds information about targets (including functions).

  The file, line number and file offset is used when parsing the function.

  The argument names are used when parsing other functions. The function
  signature is parsed eagerly, and the body lazily.

  The bytecode offset is used when running a target. While parsing, the offset
  will be the offset in the intermediate bytecode format.


  To add a new target, call TargetIndexBeginTarget. Then call
  TargetIndexAddArgument as many times as needed. When done, call
  TargetIndexFinishTarget.


  Before looking up functions, the index needs to be built. After this has been
  done, no more functions can be added.
*/

#define TARGETINDEX_H

typedef uint targetref;

extern void TargetIndexInit(void);
extern void TargetIndexDispose(void);
extern boolean TargetIndexBuildIndex(void);
extern targetref TargetIndexGetFirstTarget(void);
extern targetref TargetIndexGetNextTarget(targetref target);

extern boolean TargetIndexBeginTarget(stringref name, fileref file, uint line,
                                      uint fileOffset);
extern boolean TargetIndexAddParameter(stringref name, boolean required);
extern void TargetIndexFinishTarget(void);
extern void TargetIndexMarkForParsing(targetref target);
extern targetref TargetIndexPopUnparsedTarget(void);

extern pure uint TargetIndexGetTargetCount(void);
extern pure targetref TargetIndexGet(stringref name);
extern pure stringref TargetIndexGetName(targetref target);
extern pure fileref TargetIndexGetFile(targetref target);
extern pure uint TargetIndexGetLine(targetref target);
extern pure uint TargetIndexGetFileOffset(targetref target);
extern pure uint TargetIndexGetBytecodeOffset(targetref target);
extern void TargetIndexSetBytecodeOffset(targetref target, uint offset);
extern pure uint TargetIndexGetParameterCount(targetref target);
extern pure const stringref *TargetIndexGetParameterNames(targetref target);
extern pure uint TargetIndexGetMinimumArgumentCount(targetref target);
