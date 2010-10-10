#define INTVECTOR_H

struct intvector
{
    uint *data;
    size_t size;
    size_t allocatedSize;
};

extern nonnull ErrorCode IntVectorInit(intvector *v);
extern nonnull void IntVectorDispose(intvector *v);

extern nonnull pure size_t IntVectorSize(const intvector *v);
extern nonnull ErrorCode IntVectorSetSize(intvector *v, size_t size);
extern nonnull ErrorCode IntVectorGrowZero(intvector *v, size_t size);

extern nonnull pure const uint *IntVectorGetPointer(const intvector *v,
                                                    size_t index);

extern nonnull void IntVectorCopy(
    const intvector *src, size_t srcOffset,
    intvector *dst, size_t dstOffset, size_t size);
extern nonnull void IntVectorMove(intvector *v, size_t src, size_t dst,
                                  size_t size);

extern nonnull ErrorCode IntVectorAppend(
    const intvector *src, size_t srcOffset,
    intvector *dst, size_t size);
extern nonnull ErrorCode IntVectorAppendAll(const intvector *src,
                                            intvector *dst);

extern nonnull ErrorCode IntVectorAdd(intvector *v, uint value);
extern nonnull ErrorCode IntVectorAddRef(intvector *v, ref_t value);

extern nonnull pure uint IntVectorGet(const intvector *v, size_t index);
extern nonnull ref_t IntVectorGetRef(const intvector *v, size_t index);

extern nonnull void IntVectorSet(intvector *v, size_t index, uint value);
extern nonnull void IntVectorSetRef(intvector *v, size_t index, ref_t value);

extern nonnull pure uint IntVectorPeek(const intvector *v);
extern nonnull ref_t IntVectorPeekRef(const intvector *v);
extern nonnull uint IntVectorPop(intvector *v);
extern nonnull ref_t IntVectorPopRef(intvector *v);
