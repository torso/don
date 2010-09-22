#define BYTEVECTOR_H

struct bytevector
{
    uint size;
    byte *data;
};

extern nonnull ErrorCode ByteVectorInit(bytevector *v);
extern nonnull void ByteVectorDispose(bytevector *v);
extern nonnull pure uint ByteVectorSize(const bytevector *v);
extern nonnull ErrorCode ByteVectorSetSize(bytevector *v, uint size);
extern nonnull void ByteVectorCopy(
    const bytevector *restrict src, uint srcOffset,
    bytevector *restrict dst, uint dstOffset, uint length);
extern nonnull ErrorCode ByteVectorAppend(
    const bytevector *restrict src, uint srcOffset,
    bytevector *restrict dst, uint length);
extern nonnull ErrorCode ByteVectorAppendAll(const bytevector *restrict src,
                                             bytevector *restrict dst);
extern nonnull void ByteVectorMove(bytevector *v, uint src, uint dst,
                                   uint length);
extern nonnull ErrorCode ByteVectorAdd(bytevector *v, byte value);
extern nonnull ErrorCode ByteVectorAddInt(bytevector *v, int value);
extern nonnull ErrorCode ByteVectorAddUint(bytevector *v, uint value);
extern nonnull ErrorCode ByteVectorAddUint16(bytevector *v, uint16 value);
extern nonnull ErrorCode ByteVectorAddPackInt(bytevector *v, int value);
extern nonnull ErrorCode ByteVectorAddPackUint(bytevector *v, uint value);
extern nonnull ErrorCode ByteVectorAddUnpackedInt(bytevector *v, int value);
extern nonnull ErrorCode ByteVectorAddUnpackedUint(bytevector *v, uint value);
extern nonnull ErrorCode ByteVectorAddData(bytevector *v,
                                           byte *value, uint size);
extern nonnull pure byte ByteVectorGet(const bytevector *v, uint index);
extern nonnull byte ByteVectorRead(const bytevector *v, uint *index);
#define ByteVectorGetInt(v, index) ((int)ByteVectorGetUint(v, index))
extern nonnull pure uint ByteVectorGetUint(const bytevector *v, uint index);
extern nonnull pure uint16 ByteVectorGetUint16(const bytevector *v, uint index);
#define ByteVectorReadInt(v, index) ((int)ByteVectorReadUint(v, index))
extern nonnull uint ByteVectorReadUint(const bytevector *v, uint *index);
extern nonnull uint16 ByteVectorReadUint16(const bytevector *v, uint *index);
extern nonnull pure int ByteVectorGetPackInt(const bytevector *v, uint index);
extern nonnull pure uint ByteVectorGetPackUint(const bytevector *v, uint index);
#define ByteVectorReadPackInt(v, index) ((int)ByteVectorReadPackUint(v, index))
extern nonnull uint ByteVectorReadPackUint(const bytevector *v, uint *index);
#define ByteVectorSkipPackInt(v, index) (ByteVectorSkipPackUint(v, index))
extern nonnull void ByteVectorSkipPackUint(const bytevector *v, uint *index);
#define ByteVectorGetPackIntSize ByteVectorGetPackUintSize
extern nonnull pure uint ByteVectorGetPackUintSize(const bytevector *v, uint index);
extern nonnull pure const byte *ByteVectorGetPointer(const bytevector *v,
                                                     uint index);
extern nonnull pure byte ByteVectorPeek(const bytevector *v);
extern nonnull byte ByteVectorPop(bytevector *v);
extern nonnull void ByteVectorPopData(bytevector *v, byte *value, uint size);
extern nonnull void ByteVectorSet(bytevector *v, uint index, byte value);
extern nonnull void ByteVectorWrite(bytevector *v, uint *index, byte value);
extern nonnull void ByteVectorSetInt(bytevector *v, uint index, int value);
extern nonnull void ByteVectorSetUint(bytevector *v, uint index, uint value);
extern nonnull void ByteVectorSetPackInt(bytevector *v, uint index, int value);
extern nonnull void ByteVectorSetPackUint(bytevector *v, uint index, uint value);
extern nonnull void ByteVectorWriteInt(bytevector *v, uint *index, int value);
extern nonnull void ByteVectorWriteUint(bytevector *v, uint *index, uint value);
extern nonnull void ByteVectorWritePackInt(bytevector *v, uint *index,
                                           int value);
extern nonnull void ByteVectorWritePackUint(bytevector *v, uint *index,
                                            uint value);
extern nonnull void ByteVectorFill(bytevector *v, uint index, uint length,
                                   byte value);
