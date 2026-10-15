// guideXOS REXX shell command bridge.
//
// This layer converts the shell's original command line into a bounded argv
// vector, then calls the C-linkage REXX CLI entry point. The tokenizer owns
// one temporary allocation which is released before it returns.

#ifndef KERNEL_SHELL_REXX_SHELL_CMD_H
#define KERNEL_SHELL_REXX_SHELL_CMD_H

namespace kernel {
namespace shell {

enum class RexxShellTokenizeStatus {
    Success,
    InvalidInput,
    OutOfMemory
};

// Standard argv callback for dispatchers that already have tokenized input.
// Returns the linked REXX CLI's exact process-style status.
int shell_cmd_rexx(int argc, char** argv);

// Tokenize and run a complete shell command line (including the leading
// "rexx" token). exit_code is always initialized and receives the CLI status
// on success. Invalid quoting/limits and allocation failure are reported
// separately so the shell can provide a useful diagnostic and status code.
RexxShellTokenizeStatus shell_cmd_rexx_line(const char* command_line,
                                           int* exit_code);

// The CLI is a separately compiled C component in some kernel configurations.
// This lets the shell explain a missing link instead of calling a null weak
// symbol when a build does not include the interpreter objects.
bool rexx_cli_available();

} // namespace shell
} // namespace kernel

#endif // KERNEL_SHELL_REXX_SHELL_CMD_H
