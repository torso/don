#define BYTEVECTOR_H

typedef struct
{
    uint size;
    byte* data;
} bytevector;

extern nonnull void ByteVectorInit(bytevector* v);
extern nonnull void ByteVectorFree(bytevector* v);
extern nonnull pure uint ByteVectorSize(const bytevector* v);
extern nonnull void ByteVectorSetSize(bytevector* v, uint size);
extern nonnull boolean ByteVectorAdd(bytevector* v, byte value);
extern nonnull boolean ByteVectorAddInt(bytevector* v, int value);
extern nonnull boolean ByteVectorAddPackUint(bytevector* v, uint value);
extern nonnull pure byte ByteVectorGet(const bytevector* v, uint index);
extern nonnull pure int ByteVectorGetPackUint(const bytevector* v, uint index);
extern nonnull pure uint ByteVectorGetPackUintSize(const bytevector* v,
                                                   uint index);
extern nonnull pure const byte* ByteVectorGetPointer(const bytevector* v,
                                                     uint index);
extern nonnull void ByteVectorSet(bytevector* v, uint index, byte value);
extern nonnull void ByteVectorSetInt(bytevector* v, uint index, int value);
extern nonnull void ByteVectorFill(bytevector* v, uint index, uint size,
                                   byte value);
