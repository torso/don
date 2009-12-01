#ifndef FILEINDEX_H
#error fileindex.h not included
#endif
#ifndef TARGETINDEX_H
#error targetindex.h not included
#endif

extern void ParserAddKeywords(void);
extern boolean ParseFile(fileref file);
extern boolean ParseTarget(targetref target);
