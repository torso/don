#define BYTEVECTOR_H

typedef struct
{
    uint size;
    byte *data;
} bytevector;

extern nonnull void ByteVectorInit(bytevector *v);
extern nonnull void ByteVectorFree(bytevector *v);
extern nonnull uint ByteVectorSize(const bytevector *v);
extern nonnull void ByteVectorSetSize(bytevector *v, uint size);
extern nonnull void ByteVectorCopy(
    const bytevector *restrict src, uint srcOffset,
    bytevector *restrict dst, uint dstOffset, uint length);
extern nonnull void ByteVectorAppend(
    const bytevector *restrict src, uint srcOffset,
    bytevector *restrict dst, uint length);
extern nonnull void ByteVectorAppendAll(const bytevector *restrict src,
                                        bytevector *restrict dst);
extern nonnull void ByteVectorMove(bytevector *v, uint src, uint dst,
                                   uint length);
extern nonnull boolean ByteVectorAdd(bytevector *v, byte value);
extern nonnull boolean ByteVectorAddInt(bytevector *v, int value);
extern nonnull boolean ByteVectorAddUint(bytevector *v, uint value);
extern nonnull boolean ByteVectorAddPackInt(bytevector *v, int value);
extern nonnull boolean ByteVectorAddPackUint(bytevector *v, uint value);
extern nonnull byte ByteVectorGet(const bytevector *v, uint index);
extern nonnull byte ByteVectorRead(const bytevector *v, uint *index);
#define ByteVectorGetInt(v, index) ((int)ByteVectorGetUint(v, index))
extern nonnull uint ByteVectorGetUint(const bytevector *v, uint index);
#define ByteVectorReadInt(v, index) ((int)ByteVectorReadUint(v, index))
extern nonnull uint ByteVectorReadUint(const bytevector *v, uint *index);
extern nonnull int ByteVectorGetPackInt(const bytevector *v, uint index);
extern nonnull uint ByteVectorGetPackUint(const bytevector *v, uint index);
#define ByteVectorReadPackInt(v, index) ((int)ByteVectorReadPackUint(v, index))
extern nonnull uint ByteVectorReadPackUint(const bytevector *v, uint *index);
#define ByteVectorGetPackIntSize ByteVectorGetPackUintSize
extern nonnull uint ByteVectorGetPackUintSize(const bytevector *v, uint index);
extern nonnull const byte *ByteVectorGetPointer(const bytevector *v,
                                                uint index);
extern nonnull void ByteVectorSet(bytevector *v, uint index, byte value);
extern nonnull void ByteVectorWrite(bytevector *v, uint *index, byte value);
extern nonnull void ByteVectorSetInt(bytevector *v, uint index, int value);
extern nonnull void ByteVectorSetUint(bytevector *v, uint index, uint value);
extern nonnull void ByteVectorSetPackUint(bytevector *v, uint index, uint value);
extern nonnull void ByteVectorWriteInt(bytevector *v, uint *index, int value);
extern nonnull void ByteVectorWriteUint(bytevector *v, uint *index, uint value);
extern nonnull void ByteVectorWritePackInt(bytevector *v, uint *index,
                                           int value);
extern nonnull void ByteVectorWritePackUint(bytevector *v, uint *index,
                                            uint value);
extern nonnull void ByteVectorFill(bytevector *v, uint index, uint length,
                                   byte value);
