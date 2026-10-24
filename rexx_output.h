#ifndef GUIDEXOS_REXX_OUTPUT_H
#define GUIDEXOS_REXX_OUTPUT_H

#include <stddef.h>
#include "rexx_output_capability.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum RexxOutputStream {
    REXX_OUTPUT_STDOUT = 0,
    REXX_OUTPUT_STDERR = 1
} RexxOutputStream;

typedef int (*RexxOutputWriter)(void *context,
                                RexxOutputStream stream,
                                const char *bytes,
                                size_t length);
typedef int (*RexxOutputFlusher)(void *context, RexxOutputStream stream);

typedef struct RexxOutputHost {
    RexxOutputWriter write;
    RexxOutputFlusher flush;
    void *context;
    size_t max_stdout_bytes;
    size_t max_stderr_bytes;
    /* Optional read-only query for the same destination used by write. */
    RexxVMOutputCapabilityReader read_capability;
} RexxOutputHost;

/* Mutable accounting belongs to one CLI execution, never a process global. */
typedef struct RexxOutputContext {
    const RexxOutputHost *host;
    size_t stdout_bytes;
    size_t stderr_bytes;
} RexxOutputContext;

void rexx_output_init(RexxOutputContext *output,
                      const RexxOutputHost *host);
int rexx_output_write(RexxOutputContext *output,
                      RexxOutputStream stream,
                      const char *bytes,
                      size_t length);
int rexx_output_flush(RexxOutputContext *output, RexxOutputStream stream);
int rexx_output_format(RexxOutputContext *output,
                       RexxOutputStream stream,
                       const char *format,
                       ...);

#ifdef __cplusplus
}
#endif

#endif
