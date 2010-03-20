#ifndef STRINGPOOL_H
#error stringpool.h not included
#endif
#define FILEINDEX_H

typedef uint fileref;

extern void FileIndexDispose(void);
extern nonnull fileref FileIndexAdd(const char *filename);
extern pure stringref FileIndexGetName(fileref file);
extern pure const byte *FileIndexGetContents(fileref file);
extern pure uint FileIndexGetSize(fileref file);
