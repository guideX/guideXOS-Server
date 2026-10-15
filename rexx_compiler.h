#ifndef GUIDEXOS_REXX_COMPILER_H
#define GUIDEXOS_REXX_COMPILER_H

/*
 * Small, dependency-free compiler for the AST in rexx_parser.h.
 *
 * Instructions are fixed-size records in execution order. Branch operands
 * are absolute instruction indexes (not byte offsets), and constant/symbol
 * operands index the corresponding owned pool in RexxBytecodeChunk. The
 * evaluation stack is left-to-right: binary instructions pop right then
 * left and push one result; stores, SAY, COMMAND, and ADDRESS consume their
 * value; conditional jumps consume their condition. OP_CALL pops `auxiliary`
 * arguments and pushes a result.
 */

#include "rexx_parser.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum RexxOpcode {
    OP_HALT = 0,
    OP_NOP,
    OP_LOAD_CONST,
    OP_LOAD_VAR,
    OP_STORE_VAR,
    OP_LOAD_STEM,
    OP_STORE_STEM,
    OP_POP,
    OP_POSITIVE,
    OP_NEGATE,
    OP_LOGICAL_NOT,
    OP_POWER,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_INTEGER_DIVIDE,
    OP_REMAINDER,
    OP_ADD,
    OP_SUB,
    OP_CONCAT,
    OP_CONCAT_BLANK,
    OP_CONCAT_ABUTTAL,
    OP_EQUAL,
    OP_STRICT_EQUAL,
    OP_NOT_EQUAL,
    OP_STRICT_NOT_EQUAL,
    OP_GREATER,
    OP_LESS,
    OP_GREATER_EQUAL,
    OP_LESS_EQUAL,
    OP_NOT_GREATER,
    OP_NOT_LESS,
    OP_STRICT_GREATER,
    OP_STRICT_LESS,
    OP_STRICT_GREATER_EQUAL,
    OP_STRICT_LESS_EQUAL,
    OP_STRICT_NOT_GREATER,
    OP_STRICT_NOT_LESS,
    OP_LOGICAL_AND,
    OP_LOGICAL_OR,
    OP_LOGICAL_XOR,
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_JUMP_IF_TRUE,
    OP_SAY,
    OP_CALL,
    OP_RETURN,
    OP_EXIT,
    OP_COMMAND,
    OP_ADDRESS,
    OP_LABEL,
    OP_DO_SETUP,
    OP_DO_TEST,
    OP_DO_NEXT,
    OP_DO_END,
    OP_LOAD_UNSET,
    OP_NUMERIC_DIGITS
} RexxOpcode;

/*
 * An instruction always occupies the same record size. For most opcodes,
 * `operand` is the sole operand and `auxiliary` is zero. OP_CALL uses
 * `operand` for the function symbol and `auxiliary` for argument count.
 * OP_LOAD_UNSET pushes an unset value for an omitted positional argument.
 * OP_ADDRESS uses `operand` for an environment symbol when
 * REXX_INSTRUCTION_HAS_ENVIRONMENT is set; otherwise its environment is
 * KERNEL.
 * OP_DO_SETUP uses `operand` for a control symbol (or REXX_NO_SYMBOL) and
 * `auxiliary` for RexxDoFlags. Before setup, the compiler pushes the initial
 * value and then any present TO, BY, and FOR expressions in that order.
 * Setup consumes them and opens a loop frame. OP_DO_TEST pushes true while a
 * count or controlled range has another iteration (false otherwise), after
 * which the compiler emits OP_JUMP_IF_FALSE. OP_DO_NEXT advances the active
 * loop frame; the following OP_JUMP returns to the test. OP_DO_END closes the
 * frame. A DO without a count/control variable still gets a frame so LEAVE
 * and ITERATE work consistently.
 */
typedef struct RexxInstruction {
    uint8_t opcode;
    uint8_t flags;
    uint16_t reserved;
    uint32_t operand;
    uint32_t auxiliary;
} RexxInstruction;

typedef enum RexxConstantKind {
    REXX_CONSTANT_NUMBER = 0,
    REXX_CONSTANT_STRING,
    REXX_CONSTANT_HEX_STRING,
    REXX_CONSTANT_BINARY_STRING
} RexxConstantKind;

/* Literal bytes are copied exactly from the source token, including quotes. */
typedef struct RexxConstant {
    RexxConstantKind kind;
    char *text;
    size_t length;
} RexxConstant;

typedef enum RexxSymbolKind {
    REXX_SYMBOL_VARIABLE = 0,
    REXX_SYMBOL_STEM,
    REXX_SYMBOL_ROUTINE,
    REXX_SYMBOL_LABEL
} RexxSymbolKind;

/* Symbol names are copied and ASCII-uppercased for case-insensitive lookup. */
typedef struct RexxSymbol {
    char *name;
    size_t length;
    RexxSymbolKind kind;
} RexxSymbol;

typedef struct RexxBytecodeChunk {
    RexxInstruction *instructions;
    size_t instruction_count;
    RexxConstant *constants;
    size_t constant_count;
    RexxSymbol *symbols;
    size_t symbol_count;
} RexxBytecodeChunk;

typedef enum RexxDoFlags {
    REXX_DO_FLAG_CONTROLLED = 1u << 0,
    REXX_DO_FLAG_COUNTED = 1u << 1,
    REXX_DO_FLAG_HAS_TO = 1u << 2,
    REXX_DO_FLAG_HAS_BY = 1u << 3,
    REXX_DO_FLAG_HAS_FOR = 1u << 4
} RexxDoFlags;

/* Instruction flag bits used by SAY, ADDRESS, RETURN, EXIT, and NUMERIC. */
#define REXX_INSTRUCTION_HAS_VALUE ((uint8_t)1u)
#define REXX_INSTRUCTION_HAS_ENVIRONMENT ((uint8_t)2u)

typedef enum RexxCompilerStatus {
    REXX_COMPILER_STATUS_OK = 0,
    REXX_COMPILER_STATUS_INVALID_ARGUMENT,
    REXX_COMPILER_STATUS_OUT_OF_MEMORY,
    REXX_COMPILER_STATUS_RESOURCE_LIMIT,
    REXX_COMPILER_STATUS_UNSUPPORTED_AST,
    REXX_COMPILER_STATUS_MALFORMED_AST
} RexxCompilerStatus;

typedef struct RexxCompilerError {
    RexxCompilerStatus status;
    RexxToken token;        /* Best available source coordinate, if any. */
    const char *message;    /* Static string; caller must not free it. */
} RexxCompilerError;

#define REXX_NO_SYMBOL UINT32_MAX
#define REXX_COMPILER_MAX_INSTRUCTIONS ((size_t)1048576u)
#define REXX_COMPILER_MAX_POOL_ENTRIES ((size_t)65536u)
#define REXX_COMPILER_MAX_NESTING ((size_t)128u)

/*
 * Compiles a complete parser-owned AST. On success, *chunk receives an
 * allocated chunk, which must be released with rexx_bytecode_chunk_free().
 * Free any prior chunk before reusing its output variable.
 * On any failure *chunk is NULL and error, when non-NULL, describes the first
 * failure. AST token slices must remain valid for the duration of the call.
 */
RexxCompilerStatus rexx_compiler_compile(const RexxAstProgram *program,
                                         RexxBytecodeChunk **chunk,
                                         RexxCompilerError *error);

/* Releases all arrays and copied pool strings. NULL is accepted. */
void rexx_bytecode_chunk_free(RexxBytecodeChunk *chunk);

/* Returns a static printable name for a compiler status. */
const char *rexx_compiler_status_name(RexxCompilerStatus status);

#ifdef __cplusplus
}
#endif

#endif /* GUIDEXOS_REXX_COMPILER_H */
