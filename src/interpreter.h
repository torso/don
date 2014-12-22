#ifdef DEBUG
extern bool trace;
#endif

struct _LinkedProgram;
extern nonnull void InterpreterExecute(const struct _LinkedProgram *program, int target);
