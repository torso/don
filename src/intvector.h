#define INTVECTOR_H

struct intvector
{
    size_t size;
    uint *data;
};

extern nonnull ErrorCode IntVectorInit(intvector *v);
extern nonnull ErrorCode IntVectorInitCopy(intvector *restrict v,
                                           const intvector *restrict data);
extern nonnull void IntVectorDispose(intvector *v);
extern nonnull pure size_t IntVectorSize(const intvector *v);
extern nonnull ErrorCode IntVectorSetSize(intvector *v, size_t size);
extern nonnull ErrorCode IntVectorGrowZero(intvector *v, size_t size);
extern nonnull void IntVectorCopy(
    const intvector *restrict src, size_t srcOffset,
    intvector *restrict dst, size_t dstOffset, size_t size);
extern nonnull ErrorCode IntVectorAppend(
    const intvector *restrict src, size_t srcOffset,
    intvector *restrict dst, size_t size);
extern nonnull ErrorCode IntVectorAppendAll(const intvector *restrict src,
                                            intvector *restrict dst);
extern nonnull void IntVectorMove(intvector *v, size_t src, size_t dst,
                                  size_t size);
extern nonnull ErrorCode IntVectorAdd(intvector *v, uint value);
extern nonnull ErrorCode IntVectorAdd4(intvector *v, uint value1, uint value2,
                                       uint value3, uint value4);
extern nonnull pure uint IntVectorGet(const intvector *v, size_t index);
extern nonnull pure const uint *IntVectorGetPointer(const intvector *v,
                                                    size_t index);
extern nonnull pure uint IntVectorPeek(const intvector *v);
extern nonnull uint IntVectorPop(intvector *v);
extern nonnull void IntVectorSet(intvector *v, size_t index, uint value);
