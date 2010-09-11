#define INTVECTOR_H

struct intvector
{
    uint size;
    uint *data;
};

extern nonnull ErrorCode IntVectorInit(intvector *v);
extern nonnull ErrorCode IntVectorInitCopy(intvector *restrict v,
                                           const intvector *restrict data);
extern nonnull void IntVectorDispose(intvector *v);
extern nonnull pure uint IntVectorSize(const intvector *v);
extern nonnull ErrorCode IntVectorSetSize(intvector *v, uint size);
extern nonnull ErrorCode IntVectorGrowZero(intvector *v, uint size);
extern nonnull void IntVectorCopy(
    const intvector *restrict src, uint srcOffset,
    intvector *restrict dst, uint dstOffset, uint length);
extern nonnull ErrorCode IntVectorAppend(
    const intvector *restrict src, uint srcOffset,
    intvector *restrict dst, uint length);
extern nonnull ErrorCode IntVectorAppendAll(const intvector *restrict src,
                                            intvector *restrict dst);
extern nonnull void IntVectorMove(intvector *v, uint src, uint dst,
                                  uint length);
extern nonnull ErrorCode IntVectorAdd(intvector *v, uint value);
extern nonnull ErrorCode IntVectorAdd4(intvector *v, uint value1, uint value2,
                                       uint value3, uint value4);
extern nonnull pure uint IntVectorGet(const intvector *v, uint index);
extern nonnull pure const uint *IntVectorGetPointer(const intvector *v,
                                                    uint index);
extern nonnull pure uint IntVectorPeek(const intvector *v);
extern nonnull uint IntVectorPop(intvector *v);
extern nonnull void IntVectorSet(intvector *v, uint index, uint value);
