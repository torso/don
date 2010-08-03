#ifndef BYTEVECTOR_H
#error bytevector.h not included
#endif
#ifndef FILEINDEX_H
#error fileindex.h not included
#endif
#ifndef TARGETINDEX_H
#error targetindex.h not included
#endif

extern void ParserAddKeywords(void);
extern ErrorCode ParseFile(fileref file);
extern nonnull ErrorCode ParseFunction(targetref target, bytevector *parsed);
