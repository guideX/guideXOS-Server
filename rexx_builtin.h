#ifndef GUIDEXOS_REXX_BUILTIN_H
#define GUIDEXOS_REXX_BUILTIN_H

/*
 * Native, allocation-safe REXX built-in string functions.
 *
 * Arguments and the result are RexxValue objects owned by the caller. A
 * result passed to rexx_builtin_dispatch must be initialized with
 * REXX_VALUE_INIT (or already contain a valid value); dispatch replaces it.
 * Keep result separate from the argument array. Argument storage remains
 * untouched by the dispatcher.
 */

#include "rexx_vm.h"

#include <stddef.h>

/* Built-in string results stay bounded by the interpreter's source limit. */
#define REXX_BUILTIN_MAX_RESULT_LENGTH REXX_VM_MAX_PARSE_SOURCE_LENGTH

#ifdef __cplusplus
extern "C" {
#endif

typedef RexxVMStatus (*RexxBuiltinFunction)(const RexxValue *arguments,
                                            size_t argument_count,
                                            RexxValue *result);

typedef struct RexxBuiltinDescriptor {
    const char *name; /* Uppercase, NUL-terminated canonical spelling. */
    size_t minimum_arguments;
    size_t maximum_arguments;
    RexxBuiltinFunction function;
} RexxBuiltinDescriptor;

/* Case-insensitive lookup over an explicit-length function name. */
const RexxBuiltinDescriptor *rexx_builtin_lookup(const char *name,
                                                 size_t name_length);

/*
 * Validate the name and arity, then invoke its built-in. Unknown names and
 * bad arities return dedicated VM diagnostics; no host functions are called.
 */
RexxVMStatus rexx_builtin_dispatch(const char *name,
                                   size_t name_length,
                                   const RexxValue *arguments,
                                   size_t argument_count,
                                   RexxValue *result);

#ifdef __cplusplus
}
#endif

#endif /* GUIDEXOS_REXX_BUILTIN_H */
