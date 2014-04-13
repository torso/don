#include <memory.h>
#include "common.h"
#include "intvector.h"

#define VECTOR_NAME intvector
#define VECTOR_TYPE int
#define VECTOR_FUNC(name) IV##name
#include "vector.inc"
