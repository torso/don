#define INTVECTOR_H

#define VECTOR_NAME intvector
#define VECTOR_TYPE uint
#define VECTOR_FUNC(name) IV##name
#include "vector.h"
#undef VECTOR_NAME
#undef VECTOR_TYPE
#undef VECTOR_FUNC

#define IVAddRef(v, value) IVAdd(v, uintFromRef(value))
#define IVGetRef(v, index) refFromUint(IVGet(v, index))
#define IVSetRef(v, index, value) IVSet(v, index, uintFromRef(value))
#define IVPeekRef(v) refFromUint(IVPeek(v))
#define IVPopRef(v) refFromUint(IVPop(v))
