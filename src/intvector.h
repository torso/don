#define INTVECTOR_H

#define VECTOR_NAME intvector
#define VECTOR_TYPE int
#define VECTOR_FUNC(name) IV##name
#include "vector.h"
#undef VECTOR_NAME
#undef VECTOR_TYPE
#undef VECTOR_FUNC

#define IVAddUint(v, value) IVAdd(v, (int)(value))
#define IVAddRef(v, value) IVAdd(v, intFromRef(value))
#define IVGetRef(v, index) refFromInt(IVGet((v), (index)))
#define IVSetUint(v, index, value) IVSet((v), (index), (int)(value))
#define IVSetRef(v, index, value) IVSet((v), (index), intFromRef(value))
#define IVPeekRef(v) refFromInt(IVPeek(v))
#define IVPopRef(v) refFromInt(IVPop(v))
