#define BYTEVECTOR_H

typedef struct
{
    uint size;
    byte* data;
} bytevector;

extern nonnull void ByteVectorInit(bytevector* v);
extern nonnull void ByteVectorFree(bytevector* v);
extern nonnull uint ByteVectorSize(const bytevector* v);
extern nonnull void ByteVectorSetSize(bytevector* v, uint size);
extern nonnull boolean ByteVectorAdd(bytevector* v, byte value);
extern nonnull boolean ByteVectorAddInt(bytevector* v, int value);
#define ByteVectorAddPackInt ByteVectorAddPackUint
extern nonnull boolean ByteVectorAddPackUint(bytevector* v, uint value);
extern nonnull byte ByteVectorGet(const bytevector* v, uint index);
extern nonnull int ByteVectorGetInt(const bytevector* v, uint index);
extern nonnull uint ByteVectorGetUint(const bytevector* v, uint index);
extern nonnull int ByteVectorGetPackInt(const bytevector* v, uint index);
extern nonnull uint ByteVectorGetPackUint(const bytevector* v, uint index);
#define ByteVectorGetPackIntSize ByteVectorGetPackUintSize
extern nonnull uint ByteVectorGetPackUintSize(const bytevector* v,
                                                   uint index);
extern nonnull const byte* ByteVectorGetPointer(const bytevector* v,
                                                uint index);
extern nonnull void ByteVectorSet(bytevector* v, uint index, byte value);
extern nonnull void ByteVectorSetInt(bytevector* v, uint index, int value);
extern nonnull void ByteVectorFill(bytevector* v, uint index, uint size,
                                   byte value);
