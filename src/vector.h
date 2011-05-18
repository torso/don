struct VECTOR_NAME
{
    VECTOR_TYPE *data;
    size_t size;
    size_t allocatedSize;
};

extern nonnull VECTOR_NAME *VECTOR_FUNC(Create)(size_t reservedSize);
extern nonnull void VECTOR_FUNC(Init)(VECTOR_NAME *v, size_t reservedSize);
extern nonnull void VECTOR_FUNC(Dispose)(VECTOR_NAME *v);

/**
 * Marks the vector as non-usable (but does not free it) and returns the
 * contents. The caller will be responsible for freeing both the container and
 * the returned data.
 */
extern nonnull VECTOR_TYPE *VECTOR_FUNC(DisposeContainer)(VECTOR_NAME *v);


extern nonnull pure size_t VECTOR_FUNC(Size)(const VECTOR_NAME *v);
extern nonnull void VECTOR_FUNC(SetSize)(VECTOR_NAME *v, size_t size);
extern nonnull void VECTOR_FUNC(Grow)(VECTOR_NAME *v, size_t size);
extern nonnull void VECTOR_FUNC(GrowZero)(VECTOR_NAME *v, size_t size);
extern nonnull void VECTOR_FUNC(ReserveSize)(VECTOR_NAME *v, size_t size);
extern nonnull void VECTOR_FUNC(ReserveAppendSize)(VECTOR_NAME *v, size_t size);
extern nonnull size_t VECTOR_FUNC(GetReservedAppendSize)(const VECTOR_NAME *v);

extern nonnull pure const VECTOR_TYPE *VECTOR_FUNC(GetPointer)(
    const VECTOR_NAME *v, size_t index);
extern nonnull VECTOR_TYPE *VECTOR_FUNC(GetAppendPointer)(VECTOR_NAME *v);

extern nonnull void VECTOR_FUNC(Copy)(const VECTOR_NAME *src, size_t srcOffset,
                                      VECTOR_NAME *dst, size_t dstOffset,
                                      size_t size);
extern nonnull void VECTOR_FUNC(Move)(VECTOR_NAME *v,
                                      size_t src, size_t dst, size_t size);
extern nonnull void VECTOR_FUNC(Zero)(VECTOR_NAME *v,
                                      size_t offset, size_t size);
extern nonnull void VECTOR_FUNC(Append)(const VECTOR_NAME *src, size_t srcOffset,
                                 VECTOR_NAME *dst, size_t size);
extern nonnull void VECTOR_FUNC(AppendAll)(const VECTOR_NAME *src,
                                           VECTOR_NAME *dst);
extern nonnull void VECTOR_FUNC(RemoveRange)(VECTOR_NAME *v,
                                             size_t offset, size_t size);

extern nonnull void VECTOR_FUNC(Add)(VECTOR_NAME *v, VECTOR_TYPE value);

extern nonnull VECTOR_TYPE VECTOR_FUNC(Get)(const VECTOR_NAME *v, size_t index);
extern nonnull void VECTOR_FUNC(Set)(VECTOR_NAME *v,
                                     size_t index, VECTOR_TYPE value);

extern nonnull VECTOR_TYPE VECTOR_FUNC(Peek)(const VECTOR_NAME *v);
extern nonnull VECTOR_TYPE VECTOR_FUNC(Pop)(VECTOR_NAME *v);
