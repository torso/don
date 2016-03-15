#define BYTEVECTOR_H

#define VECTOR_NAME bytevector
#define VECTOR_TYPE byte
#define VECTOR_FUNC(name) BV##name
#include "vector.h"
#undef VECTOR_NAME
#undef VECTOR_TYPE
#undef VECTOR_FUNC

nonnull void BVFill(bytevector *v, size_t index, size_t size, byte value);

nonnull void BVAddInt(bytevector *v, int value);
nonnull void BVAddUint(bytevector *v, uint value);
nonnull void BVAddSize(bytevector *v, size_t value);
nonnull void BVAddAll(bytevector *v, const bytevector *src);
nonnull void BVAddString(bytevector *v, const char *string);
nonnull void BVInsertData(bytevector *v, size_t offset, const byte *data, size_t size);
nonnull void BVAppendString(bytevector *v, const char *data);

#define BVGetInt(v, index) ((int)BVGetUint(v, index))
nonnull uint BVGetUint(const bytevector *v, size_t index);
nonnull void BVGetData(const bytevector *v, size_t index, byte *dst, size_t size);

nonnull void BVSetInt(bytevector *v, size_t index, int value);
nonnull void BVSetUint(bytevector *v, size_t index, uint value);
nonnull void BVSetSizeAt(bytevector *v, size_t index, size_t value);

nonnull byte BVRead(const bytevector *v, size_t *index);
#define BVReadInt(v, index) ((int)BVReadUint(v, index))
nonnull uint BVReadUint(const bytevector *v, size_t *index);
nonnull size_t BVReadSize(const bytevector *v, size_t *index);

nonnull void BVWrite(bytevector *v, size_t *index, byte value);
nonnull void BVWriteInt(bytevector *v, size_t *index, int value);
nonnull void BVWriteUint(bytevector *v, size_t *index, uint value);

nonnull void BVPopData(bytevector *v, byte *value, size_t size);
