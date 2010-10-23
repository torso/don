#define BYTEVECTOR_H

struct bytevector
{
    byte *data;
    size_t size;
    size_t allocatedSize;
};

extern nonnull bytevector *ByteVectorCreate(size_t reserveSize);
extern nonnull void ByteVectorInit(bytevector *v, size_t reserveSize);
extern nonnull void ByteVectorDispose(bytevector *v);
extern nonnull byte *ByteVectorDisposeContainer(bytevector *v);

extern nonnull pure size_t ByteVectorSize(const bytevector *v);
extern nonnull void ByteVectorSetSize(bytevector *v, size_t size);
extern nonnull void ByteVectorGrow(bytevector *v, size_t size);
extern nonnull void ByteVectorGrowZero(bytevector *v, size_t size);
extern nonnull void ByteVectorReserveSize(bytevector *v, size_t size);
extern nonnull void ByteVectorReserveAppendSize(bytevector *v, size_t size);
extern nonnull size_t ByteVectorGetReservedAppendSize(const bytevector *v);

extern nonnull pure byte *ByteVectorGetPointer(const bytevector *v,
                                               size_t index);
extern nonnull byte *ByteVectorGetAppendPointer(bytevector *v);

extern nonnull void ByteVectorCopy(const bytevector *src, size_t srcOffset,
                                   bytevector *dst, size_t dstOffset,
                                   size_t size);
extern nonnull void ByteVectorMove(bytevector *v, size_t src, size_t dst,
                                   size_t size);
extern nonnull void ByteVectorFill(bytevector *v, size_t index, size_t size,
                                   byte value);

extern nonnull void ByteVectorAppend(const bytevector *src,
                                     size_t srcOffset,
                                     bytevector *dst,
                                     size_t size);
extern nonnull void ByteVectorAppendAll(const bytevector *src,
                                        bytevector *dst);

extern nonnull void ByteVectorAdd(bytevector *v, byte value);
extern nonnull void ByteVectorAddInt(bytevector *v, int value);
extern nonnull void ByteVectorAddUint(bytevector *v, uint value);
extern nonnull void ByteVectorAddUint16(bytevector *v, uint16 value);
extern nonnull void ByteVectorAddRef(bytevector *v, ref_t value);
extern nonnull void ByteVectorAddData(bytevector *v,
                                      const byte *data, size_t size);
extern nonnull void ByteVectorInsertData(bytevector *v, size_t offset,
                                         const byte *data, size_t size);

extern nonnull byte ByteVectorGet(const bytevector *v, size_t index);
#define ByteVectorGetInt(v, index) ((int)ByteVectorGetUint(v, index))
extern nonnull uint ByteVectorGetUint(const bytevector *v, size_t index);
extern nonnull uint16 ByteVectorGetUint16(const bytevector *v, size_t index);

extern nonnull void ByteVectorSet(bytevector *v, size_t index, byte value);
extern nonnull void ByteVectorSetInt(bytevector *v, size_t index, int value);
extern nonnull void ByteVectorSetUint(bytevector *v, size_t index, uint value);

extern nonnull byte ByteVectorRead(const bytevector *v, size_t *index);
#define ByteVectorReadInt(v, index) ((int)ByteVectorReadUint(v, index))
extern nonnull uint ByteVectorReadUint(const bytevector *v, size_t *index);
extern nonnull uint16 ByteVectorReadUint16(const bytevector *v, size_t *index);

extern nonnull void ByteVectorWrite(bytevector *v, size_t *index, byte value);
extern nonnull void ByteVectorWriteInt(bytevector *v, size_t *index, int value);
extern nonnull void ByteVectorWriteUint(bytevector *v, size_t *index, uint value);

extern nonnull byte ByteVectorPeek(const bytevector *v);
extern nonnull byte ByteVectorPop(bytevector *v);
extern nonnull void ByteVectorPopData(bytevector *v, byte *value, size_t size);
