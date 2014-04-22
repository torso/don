static void vectorCheck(const VECTOR_NAME *v)
{
    assert(v);
    assert(v->data);
    assert(v->allocatedSize);
    assert(v->allocatedSize >= v->size);
}

static void vectorCheckIndex(const VECTOR_NAME *v, size_t index)
{
    vectorCheck(v);
    assert(index < v->size);
}

static void vectorCheckRange(const VECTOR_NAME *v, size_t index, size_t size)
{
    vectorCheck(v);
    assert(index < v->size || (index == v->size && !size));
    assert(VECTOR_FUNC(Size)(v) >= index + size);
}

static VECTOR_TYPE *vectorGrow(VECTOR_NAME *v, size_t size)
{
    size_t oldSize = v->size;
    size_t newSize;

    vectorCheck(v);
    size += v->size;
    if (unlikely(size > v->allocatedSize))
    {
        newSize = v->allocatedSize;
        do
        {
            newSize *= 2;
            assert(newSize >= v->allocatedSize); /* TODO: Handle error. */
        }
        while (size > newSize);
        v->data = (VECTOR_TYPE*)realloc(v->data, newSize * sizeof(VECTOR_TYPE));
        v->allocatedSize = newSize;
    }
    v->size = size;
    return &v->data[oldSize];
}


VECTOR_NAME *VECTOR_FUNC(Create)(size_t reservedSize)
{
    VECTOR_NAME *v = (VECTOR_NAME*)malloc(sizeof(VECTOR_NAME));
    VECTOR_FUNC(Init)(v, reservedSize);
    return v;
}

void VECTOR_FUNC(Init)(VECTOR_NAME *v, size_t reservedSize)
{
    reservedSize = max(reservedSize, 4);
    v->data = (VECTOR_TYPE*)malloc(reservedSize * sizeof(VECTOR_TYPE));
    v->size = 0;
    v->allocatedSize = reservedSize;
}

void VECTOR_FUNC(Dispose)(VECTOR_NAME *v)
{
    free(v->data);
    v->data = null;
}

VECTOR_TYPE *VECTOR_FUNC(DisposeContainer)(VECTOR_NAME *v)
{
    VECTOR_TYPE *data = v->data;
    v->data = null;
    return data;
}


void VECTOR_FUNC(SetSize)(VECTOR_NAME *v, size_t size)
{
    if (size < v->allocatedSize)
    {
        v->size = size;
    }
    else
    {
        vectorGrow(v, size - v->size);
    }
}

void VECTOR_FUNC(Grow)(VECTOR_NAME *v, size_t size)
{
    vectorGrow(v, size);
}

void VECTOR_FUNC(GrowZero)(VECTOR_NAME *v, size_t size)
{
    memset(vectorGrow(v, size), 0, size * sizeof(VECTOR_TYPE));
}

static void VECTOR_FUNC(ReserveAppendSize)(VECTOR_NAME *v, size_t size)
{
    size_t oldSize = v->size;
    vectorGrow(v, size);
    v->size = oldSize;
}

void VECTOR_FUNC(ReserveSize)(VECTOR_NAME *v, size_t size)
{
    if (size > v->allocatedSize)
    {
        VECTOR_FUNC(ReserveAppendSize)(v, size - v->size);
    }
}

size_t VECTOR_FUNC(GetReservedAppendSize)(const VECTOR_NAME *v)
{
    vectorCheck(v);
    return v->allocatedSize - v->size;
}


const VECTOR_TYPE *VECTOR_FUNC(GetPointer)(const VECTOR_NAME *v, size_t index)
{
    if (index)
    {
        vectorCheckIndex(v, index - 1);
    }
    else
    {
        vectorCheck(v);
    }
    return &v->data[index];
}

VECTOR_TYPE *VECTOR_FUNC(GetWritePointer)(VECTOR_NAME *v, size_t index)
{
    if (index)
    {
        vectorCheckIndex(v, index - 1);
    }
    else
    {
        vectorCheck(v);
    }
    return &v->data[index];
}

VECTOR_TYPE *VECTOR_FUNC(GetAppendPointer)(VECTOR_NAME *v, size_t size)
{
    size_t oldSize = v->size;
    vectorGrow(v, size);
    return &v->data[oldSize];
}


void VECTOR_FUNC(Copy)(const VECTOR_NAME *src, size_t srcOffset,
                       VECTOR_NAME *dst, size_t dstOffset, size_t size)
{
    vectorCheckRange(src, srcOffset, size);
    vectorCheckRange(dst, dstOffset, size);
    memmove(&dst->data[dstOffset], &src->data[srcOffset], size * sizeof(VECTOR_TYPE));
}

void VECTOR_FUNC(Move)(VECTOR_NAME *v, size_t src, size_t dst, size_t size)
{
    vectorCheckRange(v, src, size);
    vectorCheckRange(v, dst, size);
    memmove(&v->data[dst], &v->data[src], size * sizeof(VECTOR_TYPE));
}

void VECTOR_FUNC(Zero)(VECTOR_NAME *v, size_t offset, size_t size)
{
    vectorCheckRange(v, offset, size);
    memset(&v->data[offset], 0, size * sizeof(VECTOR_TYPE));
}


void VECTOR_FUNC(Append)(const VECTOR_NAME *src, size_t srcOffset,
                         VECTOR_NAME *dst, size_t size)
{
    VECTOR_TYPE *p;
    vectorCheckRange(src, srcOffset, size);
    p = vectorGrow(dst, size);
    memcpy(p, &src->data[srcOffset], size * sizeof(VECTOR_TYPE));
}

void VECTOR_FUNC(AppendAll)(const VECTOR_NAME *src, VECTOR_NAME *dst)
{
    vectorCheck(src);
    VECTOR_FUNC(Append)(src, 0, dst, src->size);
}

void VECTOR_FUNC(RemoveRange)(VECTOR_NAME *v, size_t offset, size_t size)
{
    VECTOR_FUNC(Move)(v, offset + size, offset, v->size - offset - size);
    VECTOR_FUNC(SetSize)(v, v->size - size);
}


void VECTOR_FUNC(Add)(VECTOR_NAME *v, VECTOR_TYPE value)
{
    VECTOR_TYPE *p = vectorGrow(v, 1);
    *p = value;
}

void VECTOR_FUNC(AddData)(VECTOR_NAME *v, const VECTOR_TYPE *data, size_t size)
{
    VECTOR_TYPE *p = vectorGrow(v, size);
    memcpy(p, data, size * sizeof(VECTOR_TYPE));
}

VECTOR_TYPE VECTOR_FUNC(Get)(const VECTOR_NAME *v, size_t index)
{
    vectorCheckIndex(v, index);
    return v->data[index];
}

void VECTOR_FUNC(Set)(VECTOR_NAME *v, size_t index, VECTOR_TYPE value)
{
    vectorCheckIndex(v, index);
    v->data[index] = value;
}

void VECTOR_FUNC(Insert)(VECTOR_NAME *v, size_t index, VECTOR_TYPE value)
{
    VECTOR_TYPE *p = vectorGrow(v, 1);
    VECTOR_TYPE *insert = (VECTOR_TYPE*)VECTOR_FUNC(GetPointer)(v, index);
    memmove(insert + sizeof(value), insert, (size_t)(p - insert));
    *insert = value;
}

VECTOR_TYPE VECTOR_FUNC(Peek)(const VECTOR_NAME *v)
{
    vectorCheckIndex(v, 0);
    return v->data[v->size - 1];
}

VECTOR_TYPE VECTOR_FUNC(Pop)(VECTOR_NAME *v)
{
    vectorCheckIndex(v, 0);
    v->size--;
    return v->data[v->size];
}
