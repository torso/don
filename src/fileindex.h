#define FILEINDEX_H

typedef uint fileref;

extern void FileIndexInit(void);
extern nonnull fileref FileIndexAdd(const char* filename);
extern pure const byte* FileIndexGetContents(fileref file);
extern pure ulong FileIndexGetSize(fileref file);
