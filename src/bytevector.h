#define BYTEVECTOR_H

struct bytevector
{
    size_t size;
    byte *data;
};

extern nonnull ErrorCode ByteVectorInit(bytevector *v);
extern nonnull void ByteVectorDispose(bytevector *v);
extern nonnull byte *ByteVectorDisposeContainer(bytevector *v);
extern nonnull pure size_t ByteVectorSize(const bytevector *v);
extern nonnull ErrorCode ByteVectorSetSize(bytevector *v, size_t size);
extern nonnull ErrorCode ByteVectorGrowZero(bytevector *v, size_t size);
extern nonnull void ByteVectorCopy(
    const bytevector *restrict src, size_t srcOffset,
    bytevector *restrict dst, size_t dstOffset, size_t size);
extern nonnull ErrorCode ByteVectorAppend(
    const bytevector *restrict src, size_t srcOffset,
    bytevector *restrict dst, size_t size);
extern nonnull ErrorCode ByteVectorAppendAll(const bytevector *restrict src,
                                             bytevector *restrict dst);
extern nonnull void ByteVectorMove(bytevector *v, size_t src, size_t dst,
                                   size_t size);
extern nonnull ErrorCode ByteVectorAdd(bytevector *v, byte value);
extern nonnull ErrorCode ByteVectorAddInt(bytevector *v, int value);
extern nonnull ErrorCode ByteVectorAddUint(bytevector *v, uint value);
extern nonnull ErrorCode ByteVectorAddUint16(bytevector *v, uint16 value);
extern nonnull ErrorCode ByteVectorAddPackInt(bytevector *v, int value);
extern nonnull ErrorCode ByteVectorAddPackUint(bytevector *v, uint value);
extern nonnull ErrorCode ByteVectorAddUnpackedInt(bytevector *v, int value);
extern nonnull ErrorCode ByteVectorAddUnpackedUint(bytevector *v, uint value);
extern nonnull ErrorCode ByteVectorAddData(bytevector *v,
                                           const byte *value, size_t size);
extern nonnull pure byte ByteVectorGet(const bytevector *v, size_t index);
extern nonnull byte ByteVectorRead(const bytevector *v, size_t *index);
#define ByteVectorGetInt(v, index) ((int)ByteVectorGetUint(v, index))
extern nonnull pure uint ByteVectorGetUint(const bytevector *v, size_t index);
extern nonnull pure uint16 ByteVectorGetUint16(const bytevector *v, size_t index);
#define ByteVectorReadInt(v, index) ((int)ByteVectorReadUint(v, index))
extern nonnull uint ByteVectorReadUint(const bytevector *v, size_t *index);
extern nonnull uint16 ByteVectorReadUint16(const bytevector *v, size_t *index);
extern nonnull pure int ByteVectorGetPackInt(const bytevector *v, size_t index);
extern nonnull pure uint ByteVectorGetPackUint(const bytevector *v, size_t index);
#define ByteVectorReadPackInt(v, index) ((int)ByteVectorReadPackUint(v, index))
extern nonnull uint ByteVectorReadPackUint(const bytevector *v, size_t *index);
#define ByteVectorSkipPackInt(v, index) (ByteVectorSkipPackUint(v, index))
extern nonnull void ByteVectorSkipPackUint(const bytevector *v, size_t *index);
#define ByteVectorGetPackIntSize ByteVectorGetPackUintSize
extern nonnull pure uint ByteVectorGetPackUintSize(const bytevector *v,
                                                   size_t index);
extern nonnull pure const byte *ByteVectorGetPointer(const bytevector *v,
                                                     size_t index);
extern nonnull pure byte ByteVectorPeek(const bytevector *v);
extern nonnull byte ByteVectorPop(bytevector *v);
extern nonnull void ByteVectorPopData(bytevector *v, byte *value, size_t size);
extern nonnull void ByteVectorSet(bytevector *v, size_t index, byte value);
extern nonnull void ByteVectorWrite(bytevector *v, size_t *index, byte value);
extern nonnull void ByteVectorSetInt(bytevector *v, size_t index, int value);
extern nonnull void ByteVectorSetUint(bytevector *v, size_t index, uint value);
extern nonnull void ByteVectorSetPackInt(bytevector *v, size_t index, int value);
extern nonnull void ByteVectorSetPackUint(bytevector *v, size_t index, uint value);
extern nonnull void ByteVectorWriteInt(bytevector *v, size_t *index, int value);
extern nonnull void ByteVectorWriteUint(bytevector *v, size_t *index, uint value);
extern nonnull void ByteVectorWritePackInt(bytevector *v, size_t *index,
                                           int value);
extern nonnull void ByteVectorWritePackUint(bytevector *v, size_t *index,
                                            uint value);
extern nonnull void ByteVectorFill(bytevector *v, size_t index, size_t size,
                                   byte value);
