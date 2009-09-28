#ifndef STRINGPOOL_H
#error stringpool.h not included
#endif
#ifndef FILEINDEX_H
#error fileindex.h not included
#endif

#define TARGETINDEX_H

typedef uint targetref;

extern void TargetIndexInit(void);
extern void TargetIndexFree(void);
extern targetref TargetIndexAdd(stringref name, fileref file, int line,
                                int offset);
extern pure stringref TargetIndexGetName(targetref target);
extern pure fileref TargetIndexGetFile(targetref target);
extern pure uint TargetIndexGetLine(targetref target);
extern pure uint TargetIndexGetOffset(targetref target);
