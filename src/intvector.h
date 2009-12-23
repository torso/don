#define INTVECTOR_H

typedef struct
{
    uint size;
    int *data;
} intvector;

extern nonnull void IntVectorInit(intvector *v);
extern nonnull void IntVectorInitCopy(intvector *restrict v,
                                      const intvector *restrict data);
extern nonnull void IntVectorFree(intvector *v);
extern nonnull uint IntVectorSize(const intvector *v);
extern nonnull void IntVectorAdd(intvector *v, int value);
extern nonnull void IntVectorAdd4(intvector *v, int value1, int value2,
                                  int value3, int value4);
extern nonnull int IntVectorGet(const intvector *v, uint index);
extern nonnull const int *IntVectorGetPointer(const intvector *v,
                                              uint index);
extern nonnull int IntVectorPop(intvector *v);
extern nonnull void IntVectorSet(intvector *v, uint index, int value);
