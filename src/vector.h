struct VECTOR_NAME
{
    VECTOR_TYPE *data;
    size_t size;
    size_t allocatedSize;
};

nonnull VECTOR_NAME *VECTOR_FUNC(Create)(size_t reservedSize);
nonnull void VECTOR_FUNC(Init)(VECTOR_NAME *v, size_t reservedSize);
nonnull void VECTOR_FUNC(Dispose)(VECTOR_NAME *v);

/**
 * Marks the vector as non-usable (but does not free it) and returns the
 * contents. The caller will be responsible for freeing both the container and
 * the returned data.
 */
nonnull VECTOR_TYPE *VECTOR_FUNC(DisposeContainer)(VECTOR_NAME *v);

static nonnull pure unused size_t VECTOR_FUNC(Size)(const VECTOR_NAME *v)
{
    return v->size;
}

nonnull void VECTOR_FUNC(SetSize)(VECTOR_NAME *v, size_t size);
nonnull void VECTOR_FUNC(Grow)(VECTOR_NAME *v, size_t size);
nonnull void VECTOR_FUNC(GrowValue)(VECTOR_NAME *v, VECTOR_TYPE value, size_t size);
nonnull void VECTOR_FUNC(GrowZero)(VECTOR_NAME *v, size_t size);
nonnull void VECTOR_FUNC(ReserveSize)(VECTOR_NAME *v, size_t size);
nonnull size_t VECTOR_FUNC(GetReservedAppendSize)(const VECTOR_NAME *v);

nonnull pure const VECTOR_TYPE *VECTOR_FUNC(GetPointer)(
    const VECTOR_NAME *v, size_t index);
nonnull pure VECTOR_TYPE *VECTOR_FUNC(GetWritePointer)(
    VECTOR_NAME *v, size_t index);
nonnull VECTOR_TYPE *VECTOR_FUNC(GetAppendPointer)(VECTOR_NAME *v, size_t size);

nonnull void VECTOR_FUNC(Copy)(const VECTOR_NAME *src, size_t srcOffset,
                                      VECTOR_NAME *dst, size_t dstOffset,
                                      size_t size);
nonnull void VECTOR_FUNC(Move)(VECTOR_NAME *v,
                                      size_t src, size_t dst, size_t size);
nonnull void VECTOR_FUNC(Zero)(VECTOR_NAME *v,
                                      size_t offset, size_t size);
nonnull void VECTOR_FUNC(Append)(const VECTOR_NAME *src,
                                        size_t srcOffset, VECTOR_NAME *dst,
                                        size_t size);
nonnull void VECTOR_FUNC(AppendAll)(const VECTOR_NAME *src,
                                           VECTOR_NAME *dst);
nonnull void VECTOR_FUNC(RemoveRange)(VECTOR_NAME *v,
                                             size_t offset, size_t size);

nonnull void VECTOR_FUNC(Add)(VECTOR_NAME *v, VECTOR_TYPE value);
nonnull void VECTOR_FUNC(AddData)(VECTOR_NAME *v,
                                         const VECTOR_TYPE *values,
                                         size_t size);

nonnull VECTOR_TYPE VECTOR_FUNC(Get)(const VECTOR_NAME *v, size_t index);
nonnull void VECTOR_FUNC(Set)(VECTOR_NAME *v,
                                     size_t index, VECTOR_TYPE value);
nonnull void VECTOR_FUNC(Insert)(VECTOR_NAME *v, size_t index,
                                        VECTOR_TYPE value);

nonnull VECTOR_TYPE VECTOR_FUNC(Peek)(const VECTOR_NAME *v);
nonnull VECTOR_TYPE VECTOR_FUNC(Pop)(VECTOR_NAME *v);
