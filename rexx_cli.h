#ifndef GUIDEXOS_REXX_CLI_H
#define GUIDEXOS_REXX_CLI_H

#include "rexx_output.h"

/*
 * Native command-line front end for the guideXOS REXX interpreter.
 *
 * A shell command table can register guide_rexx_main directly. The function
 * accepts the conventional argc/argv shape and returns a process-style
 * status: the integer result from an explicit EXIT value when representable,
 * zero on normal completion or valueless EXIT, one for a script/file/runtime
 * failure, and two for invalid command-line usage.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Execute one REXX source file.
 *
 * Supported forms:
 *   rexx <script.rx> [arguments...]
 *   rexx --help
 *   rexx -- <script-path> [arguments...]
 *
 * The wrapper owns and releases all source, parser, bytecode, and VM state
 * created while executing the script. The argv strings remain caller-owned.
 */
int guide_rexx_main(int argc, char **argv);

/* Execute with an invocation-specific stdout/stderr host. The caller retains
 * ownership of host and its context until this synchronous call returns. */
int guide_rexx_main_with_output(int argc,
                                char **argv,
                                const RexxOutputHost *host);

#ifdef __cplusplus
}
#endif

#endif /* GUIDEXOS_REXX_CLI_H */
