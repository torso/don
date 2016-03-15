#define INTVECTOR_H

#define VECTOR_NAME intvector
#define VECTOR_TYPE int
#define VECTOR_FUNC(name) IV##name
#include "vector.h"
#undef VECTOR_NAME
#undef VECTOR_TYPE
#undef VECTOR_FUNC

#define IVAddUint(v, value) IVAdd(v, (int)(value))
#define IVSetUint(v, index, value) IVSet((v), (index), (int)(value))

nonnull void IVAppendString(intvector *v, const char *string, size_t length);
