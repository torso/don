#ifndef BYTEVECTOR_H
#error bytevector.h not included
#endif
#ifndef TARGETINDEX_H
#error targetindex.h not included
#endif

extern nonnull ErrorCode InterpreterExecute(
    const bytevector *restrict bytecode,
    const bytevector *restrict valueBytecode,
    targetref target);
