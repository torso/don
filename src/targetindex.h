#ifndef STRINGPOOL_H
#error stringpool.h not included
#endif
#ifndef FILEINDEX_H
#error fileindex.h not included
#endif

#define TARGETINDEX_H

typedef int targetref;

extern void TargetIndexInit(void);
extern void TargetIndexFree(void);
extern targetref TargetIndexAdd(stringref name, fileref file, int line,
                                int offset);
extern pure targetref TargetIndexGet(stringref name);
extern pure stringref TargetIndexGetName(targetref target);
extern pure fileref TargetIndexGetFile(targetref target);
extern pure uint TargetIndexGetLine(targetref target);
extern pure uint TargetIndexGetOffset(targetref target);

extern pure uint TargetIndexGetParsedOffset(targetref target);
extern void TargetIndexSetParsedOffset(targetref target, uint offset);

extern pure uint TargetIndexGetBytecodeOffset(targetref target);
extern void TargetIndexSetBytecodeOffset(targetref target, uint offset);

extern void TargetIndexFinish(void);
extern void TargetIndexDisposeParsed(void);
