# Don

**This is experimental software that is not yet ready for general use.**

When finished, Don will be a build tool that uses procedural build scripts. The
speculative VM provides automatic parallelization.

The build cache makes it easy to support incremental builds. Don decides where
to put build artifacts, not only for convenience, but also to keep them out of
the source tree and making it trivial to support/detect build configuration
changes. It also becomes very hard to accidentally use stale files when changing
the build script.

The goal is a portable and robust build tool that is also the fastest, without
sacrificing the convenience and flexibility of a procedural build script language.
