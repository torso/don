#define BYTEVECTOR_H

#define VECTOR_NAME bytevector
#define VECTOR_TYPE byte
#define VECTOR_FUNC(name) BV##name
#include "vector.h"
#undef VECTOR_NAME
#undef VECTOR_TYPE
#undef VECTOR_FUNC

extern nonnull void BVFill(bytevector *v, size_t index, size_t size,
                           byte value);

extern nonnull void BVAddInt(bytevector *v, int value);
extern nonnull void BVAddUint(bytevector *v, uint value);
extern nonnull void BVAddUint16(bytevector *v, uint16 value);
extern nonnull void BVAddSize(bytevector *v, size_t value);
#define BVAddRef(v, value) BVAddUint(v, uintFromRef(value))
extern nonnull void BVAddAll(bytevector *v, const bytevector *src);
extern nonnull void BVAddData(bytevector *v,
                                      const byte *data, size_t size);
extern nonnull void BVInsertData(bytevector *v, size_t offset,
                                         const byte *data, size_t size);

#define BVGetInt(v, index) ((int)BVGetUint(v, index))
extern nonnull uint BVGetUint(const bytevector *v, size_t index);
extern nonnull uint16 BVGetUint16(const bytevector *v, size_t index);
extern nonnull void BVGetData(const bytevector *v, size_t index,
                                      byte *dst, size_t size);

extern nonnull void BVSetInt(bytevector *v, size_t index, int value);
extern nonnull void BVSetUint(bytevector *v, size_t index, uint value);
extern nonnull void BVSetSizeAt(bytevector *v, size_t index, size_t value);

extern nonnull byte BVRead(const bytevector *v, size_t *index);
#define BVReadInt(v, index) ((int)BVReadUint(v, index))
extern nonnull uint BVReadUint(const bytevector *v, size_t *index);
extern nonnull uint16 BVReadUint16(const bytevector *v, size_t *index);

extern nonnull void BVWrite(bytevector *v, size_t *index, byte value);
extern nonnull void BVWriteInt(bytevector *v, size_t *index, int value);
extern nonnull void BVWriteUint(bytevector *v, size_t *index, uint value);

extern nonnull void BVPopData(bytevector *v, byte *value, size_t size);
