extern void FileIndexDispose(void);
extern nonnull fileref FileIndexAdd(const char *filename);
extern pure stringref FileIndexGetName(fileref file);
extern pure const byte *FileIndexGetContents(fileref file);
extern pure size_t FileIndexGetSize(fileref file);
