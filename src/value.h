typedef enum
{
    OBJECT_LIST
} ObjectType;

extern uint ValueGetOffset(uint bp, uint value);
extern nonnull uint ValueGetRelativeOffset(const RunState *state,
                                           uint valueOffset,
                                           uint bytecodeOffset);
extern nonnull boolean ValueGetBoolean(RunState *state, uint bp,
                                       uint valueIndex);
extern nonnull void ValueSetStackframeValue(RunState *state, uint valueOffset,
                                            uint stackframe);

extern nonnull uint ValueCreateStackframe(RunState *state, uint ip,
                                          uint argumentCount);
extern nonnull void ValueDestroyStackframe(RunState *state);
extern nonnull void ValuePrint(RunState *state, uint valueOffset);
extern nonnull void ValueDump(const RunState *state);
