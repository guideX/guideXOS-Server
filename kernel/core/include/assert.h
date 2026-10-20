#ifndef GXOS_FREESTANDING_ASSERT_H
#define GXOS_FREESTANDING_ASSERT_H

// Freestanding assertion support for kernel and bundled runtime code.
// Failed assertions trap on the target CPU without hosted I/O or abort().
#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
#define assert(expression) ((expression) ? (void)0 : __builtin_trap())
#endif

#endif // GXOS_FREESTANDING_ASSERT_H
