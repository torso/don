extern void FileIndexDispose(void);
extern nonnull fileref FileIndexOpen(const char *filename);
extern void FileIndexClose(fileref file);
extern pure const char *FileIndexGetName(fileref file);
extern pure const byte *FileIndexGetContents(fileref file);
extern pure size_t FileIndexGetSize(fileref file);
