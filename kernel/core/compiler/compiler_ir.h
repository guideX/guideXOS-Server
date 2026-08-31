//
// Target-neutral IR for the bounded bare-metal compiler bootstrap language.
//

#pragma once

#include "kernel/types.h"
#include "compiler_diagnostics.h"

namespace kernel {
namespace compiler {

static const uint32_t COMPILER_MAX_IDENTIFIER_BYTES = 63;
static const uint32_t COMPILER_FUNCTION_NAME_CAPACITY = COMPILER_MAX_IDENTIFIER_BYTES + 1;
static const uint32_t COMPILER_PARAMETER_NAME_CAPACITY = COMPILER_FUNCTION_NAME_CAPACITY;
static const uint32_t COMPILER_MAX_FUNCTIONS = 16;
static const uint32_t COMPILER_MAX_PARAMETERS = 4;
static const uint32_t COMPILER_MAX_CALL_EXPRESSIONS = 32;
static const uint32_t COMPILER_MAX_CALL_ARGUMENT_NODES = 128;
static const uint32_t COMPILER_MAX_CALL_GRAPH_EDGES = 128;
static const uint32_t COMPILER_MAX_TEMPORARY_SLOTS = 64;
static const uint32_t COMPILER_MAX_CALL_NESTING = 8;
static const uint32_t COMPILER_MAX_STRING_LITERAL_BYTES = 255;
static const uint32_t COMPILER_MAX_STRING_LITERALS = 16;
static const uint32_t COMPILER_MAX_TOTAL_STRING_DATA = 2048;
static const uint32_t COMPILER_MAX_LOCALS = 32;
static const uint32_t COMPILER_MAX_STATEMENTS = 256;
static const uint32_t COMPILER_MAX_EXPRESSION_NODES = 1024;
static const uint32_t COMPILER_MAX_EXPRESSION_NESTING = 16;
static const uint32_t COMPILER_MAX_BLOCKS = 32;
static const uint32_t COMPILER_MAX_BLOCK_NESTING = 16;
static const uint32_t COMPILER_MAX_CONDITIONAL_NESTING = 16;
static const uint32_t COMPILER_MAX_LOOP_NESTING = 8;
// Backend loop-control targets use the same bound as parser loop nesting.
static const uint32_t COMPILER_MAX_LOOP_TARGET_DEPTH = COMPILER_MAX_LOOP_NESTING;
static const uint32_t COMPILER_MAX_CODE_BYTES = 24576;

static const uint16_t COMPILER_INVALID_INDEX = 0xFFFFU;

enum class ExpressionKind : uint8_t {
    Constant,
    LoadLocal,
    Add,
    Subtract,
    Multiply,
    Negate,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    LogicalAnd,
    LogicalOr,
    Call,
};

struct Expression {
    ExpressionKind kind;
    uint8_t reserved;
    uint16_t left;
    uint16_t right;
    uint16_t localIndex;
    uint16_t callIndex;
    int32_t value;
    SourceLocation location;
};

enum class StatementKind : uint8_t {
    DeclareLocal,
    StoreLocal,
    HostLog,
    Return,
    If,
    While,
    Break,
    Continue,
    Block,
};

struct Statement {
    StatementKind kind;
    uint8_t reserved;
    uint16_t expression;
    uint16_t localIndex;
    uint16_t stringIndex;
    uint16_t thenBlock;
    uint16_t elseBlock;
    uint16_t nextStatement;
    SourceLocation location;
};

struct Block {
    uint16_t firstStatement;
    uint16_t lastStatement;
    uint16_t depth;
    uint16_t reserved;
};

enum class ParameterKind : uint8_t {
    Integer,
    AppContextPointer,
};

struct ParameterSymbol {
    char name[COMPILER_PARAMETER_NAME_CAPACITY];
    ParameterKind kind;
    uint8_t reserved;
    uint16_t slot;
    bool initialized;
};

struct LocalSymbol {
    char name[COMPILER_FUNCTION_NAME_CAPACITY];
    uint16_t slot;
    bool initialized;
};

struct StringLiteral {
    char data[COMPILER_MAX_STRING_LITERAL_BYTES + 1];
    uint16_t bytes;
};

struct CallSite {
    char calleeName[COMPILER_FUNCTION_NAME_CAPACITY];
    uint16_t argumentStart;
    uint16_t argumentCount;
    uint16_t calleeFunction;
    uint16_t reserved;
    SourceLocation location;
};

struct FunctionIR {
    char name[COMPILER_FUNCTION_NAME_CAPACITY];
    // Compatibility view retained for the Phase 27D host checks.  It is the
    // context parameter name when one exists; new code uses parameters[].
    char parameterName[COMPILER_PARAMETER_NAME_CAPACITY];
    SourceLocation location;
    bool usesAppContext;
    bool hasHostLog;
    bool returnConstantValid;
    uint16_t functionIndex;
    uint16_t parameterCount;
    uint16_t integerParameterCount;
    uint16_t callCount;
    uint16_t callArgumentCount;
    uint16_t maxTemporarySlots;
    uint16_t codeLabel;
    uint32_t dataOffset;
    uint32_t statementCount;
    uint32_t blockCount;
    uint32_t expressionCount;
    uint32_t localCount;
    uint32_t stringCount;
    uint32_t stringDataBytes;
    uint16_t returnExpression;
    uint32_t returnCount;
    uint16_t rootBlock;
    int32_t returnConstant;

    uint32_t logMessageBytes;
    char logMessage[COMPILER_MAX_STRING_LITERAL_BYTES + 1];

    ParameterSymbol parameters[COMPILER_MAX_PARAMETERS];
    LocalSymbol locals[COMPILER_MAX_LOCALS];
    StringLiteral strings[COMPILER_MAX_STRING_LITERALS];
    uint16_t stringOffsets[COMPILER_MAX_STRING_LITERALS];
    Expression expressions[COMPILER_MAX_EXPRESSION_NODES];
    Statement statements[COMPILER_MAX_STATEMENTS];
    Block blocks[COMPILER_MAX_BLOCKS];
    // Call storage is owned by the bounded parser arena so compatibility
    // FunctionIR values remain small enough for existing host tests.
    CallSite* calls;
    uint16_t* callArguments;
};

struct FunctionSymbol {
    char name[COMPILER_FUNCTION_NAME_CAPACITY];
    uint16_t functionIndex;
    uint16_t parameterCount;
    uint16_t codeLabel;
    uint16_t reserved;
    SourceLocation location;
};

struct TranslationUnitIR {
    uint32_t functionCount;
    uint16_t entryFunction;
    uint16_t callGraphEdgeCount;
    uint16_t recursiveSccCount;
    FunctionIR functions[COMPILER_MAX_FUNCTIONS];
    FunctionSymbol functionSymbols[COMPILER_MAX_FUNCTIONS];
    bool callGraph[COMPILER_MAX_FUNCTIONS][COMPILER_MAX_FUNCTIONS];
    bool recursiveFunction[COMPILER_MAX_FUNCTIONS];
};

} // namespace compiler
} // namespace kernel
