//
// Target-neutral IR for the bounded bare-metal compiler bootstrap language.
//

#pragma once

#include "kernel/types.h"
#include "compiler_diagnostics.h"

namespace kernel {
namespace compiler {

static const uint32_t COMPILER_MAX_IDENTIFIER_BYTES = 63;
static const uint32_t COMPILER_MAX_SOURCE_BYTES = 64 * 1024;
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
static const uint32_t COMPILER_MAX_ARRAY_ELEMENTS = 256;
// Local arrays share one bounded function-frame budget.  A 64-element array
// is 256 bytes; the aggregate local-storage cap keeps recursion accounting
// deterministic even when a function has several arrays and scalars.
static const uint32_t COMPILER_MAX_LOCAL_ARRAY_ELEMENTS = 64;
static const uint32_t COMPILER_MAX_LOCAL_STORAGE_BYTES = 256;
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
static const uint32_t COMPILER_MAX_LINKED_CODE_BYTES = 65536;
static const uint32_t COMPILER_MAX_LINKED_DATA_BYTES = 8192;
static const uint32_t COMPILER_MAX_TRANSLATION_UNITS = 16;
static const uint32_t COMPILER_MAX_DECLARATIONS = 16;
static const uint32_t COMPILER_MAX_PROJECT_EXPORTS = 128;
static const uint32_t COMPILER_MAX_PROJECT_IMPORTS = 128;
static const uint32_t COMPILER_MAX_PROJECT_RELOCATIONS = 256;
static const uint32_t COMPILER_MAX_MODULE_RELOCATIONS = 64;
static const uint32_t COMPILER_MAX_SOURCE_PATH_BYTES = 256;
static const uint32_t COMPILER_MAX_GLOBALS = 32;
static const uint32_t COMPILER_MAX_MODULE_SYMBOLS = COMPILER_MAX_FUNCTIONS + COMPILER_MAX_GLOBALS;

// Persistent guideXOS object identity.  These values are intentionally
// independent from the compiler phase number: changing object-producing
// semantics requires incrementing COMPILER_OBJECT_ABI_VERSION.
static const uint16_t COMPILER_OBJECT_FORMAT_VERSION = 1;
static const uint16_t COMPILER_OBJECT_ABI_VERSION = 2;
static const uint32_t COMPILER_OBJECT_ARCH_AMD64 = 1;
static const uint32_t COMPILER_OBJECT_TARGET_ABI_GUIDEXOS_C_V1 = 1;
static const uint32_t COMPILER_MAX_OBJECT_BYTES = 131072;
static const uint32_t COMPILER_RUNTIME_STATUS_CALL_DEPTH = 1;
static const uint32_t COMPILER_RUNTIME_STATUS_ARRAY_BOUNDS = 2;

static const uint16_t COMPILER_INVALID_INDEX = 0xFFFFU;

enum class ExpressionKind : uint8_t {
    Constant,
    LoadLocal,
    LoadGlobal,
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
    LoadIndexed,
};

enum class StorageKind : uint8_t {
    ScalarInt,
    ArrayInt,
};

enum class IndexedBaseKind : uint8_t {
    Local,
    Global,
};

struct Expression {
    ExpressionKind kind;
    uint8_t reserved;
    uint16_t left;
    uint16_t right;
    uint16_t localIndex;
    uint16_t globalIndex;
    uint16_t callIndex;
    uint16_t elementCount;
    uint16_t elementSize;
    IndexedBaseKind indexedBaseKind;
    uint8_t indexedReserved;
    int32_t value;
    SourceLocation location;
};

enum class StatementKind : uint8_t {
    DeclareLocal,
    StoreLocal,
    StoreGlobal,
    EvaluateExpression,
    HostLog,
    Return,
    If,
    While,
    Break,
    Continue,
    Block,
    StoreIndexed,
};

struct Statement {
    StatementKind kind;
    uint8_t reserved;
    uint16_t expression;
    uint16_t localIndex;
    uint16_t globalIndex;
    uint16_t stringIndex;
    uint16_t thenBlock;
    uint16_t elseBlock;
    uint16_t nextStatement;
    uint16_t indexExpression;
    uint16_t elementCount;
    uint16_t elementSize;
    IndexedBaseKind indexedBaseKind;
    uint8_t indexedReserved;
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
    StorageKind kind;
    uint8_t reserved;
    uint16_t slot;
    uint16_t elementCount;
    uint16_t elementSize;
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
    uint16_t expectedParameterCount;
    bool external;
    uint8_t reserved;
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
    uint32_t codeOffset;
    uint32_t dataOffset;
    uint32_t statementCount;
    uint32_t blockCount;
    uint32_t expressionCount;
    uint32_t localCount;
    uint32_t localStorageBytes;
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

struct FunctionDeclaration {
    char name[COMPILER_FUNCTION_NAME_CAPACITY];
    uint16_t parameterCount;
    bool usesAppContext;
    uint8_t reserved;
    SourceLocation location;
};

struct GlobalSymbolIR {
    char name[COMPILER_FUNCTION_NAME_CAPACITY];
    StorageKind kind;
    bool isDefinition;
    bool hasInitializer;
    uint16_t elementCount;
    uint16_t elementSize;
    int32_t initialValue;
    uint16_t initializerCount;
    uint16_t reserved;
    int32_t initialValues[COMPILER_MAX_ARRAY_ELEMENTS];
    uint32_t moduleDataOffset;
    uint32_t size;
    uint32_t alignment;
    SourceLocation location;
};

struct TranslationUnitIR {
    uint32_t functionCount;
    uint32_t globalCount;
    uint16_t entryFunction;
    uint16_t callGraphEdgeCount;
    uint16_t recursiveSccCount;
    uint16_t declarationCount;
    FunctionIR functions[COMPILER_MAX_FUNCTIONS];
    FunctionSymbol functionSymbols[COMPILER_MAX_FUNCTIONS];
    FunctionDeclaration declarations[COMPILER_MAX_DECLARATIONS];
    GlobalSymbolIR globals[COMPILER_MAX_GLOBALS];
    bool callGraph[COMPILER_MAX_FUNCTIONS][COMPILER_MAX_FUNCTIONS];
    bool recursiveFunction[COMPILER_MAX_FUNCTIONS];
};

enum class RelocationKind : uint8_t {
    CallRel32,
    DataAddress64,
    GlobalDataAddress64,
};

enum class SymbolKind : uint8_t {
    Function,
    Data,
    DataArray,
};

inline bool symbol_is_data(SymbolKind kind)
{
    return kind == SymbolKind::Data || kind == SymbolKind::DataArray;
}

struct RelocationRecord {
    RelocationKind kind;
    uint8_t width;
    uint16_t reserved;
    uint32_t patchOffset;
    uint32_t dataOffset;
    char targetSymbolName[COMPILER_FUNCTION_NAME_CAPACITY];
    SourceLocation location;
};

struct ExportSymbol {
    SymbolKind kind;
    char name[COMPILER_FUNCTION_NAME_CAPACITY];
    uint32_t moduleCodeOffset;
    uint32_t moduleDataOffset;
    uint32_t size;
    uint32_t alignment;
    uint16_t elementCount;
    uint16_t elementSize;
    uint16_t parameterCount;
    bool isEntry;
    uint8_t reserved;
    SourceLocation location;
};

struct ImportSymbol {
    SymbolKind kind;
    char name[COMPILER_FUNCTION_NAME_CAPACITY];
    uint16_t expectedParameterCount;
    uint16_t reserved;
    uint32_t size;
    uint32_t alignment;
    uint16_t elementCount;
    uint16_t elementSize;
    SourceLocation location;
};

// This is the deliberately small guideXOS-internal object representation.
// It is held in memory for one build and is never serialized as a general ELF
// relocatable object file.
struct CompiledModule {
    char sourcePath[COMPILER_MAX_SOURCE_PATH_BYTES];
    uint32_t sourceBytes;
    uint32_t tokenCount;
    uint32_t codeBytes;
    uint8_t code[COMPILER_MAX_CODE_BYTES];
    uint32_t dataBytes;
    uint8_t data[COMPILER_MAX_LINKED_DATA_BYTES];
    uint32_t mutableDataBytes;
    uint8_t mutableData[COMPILER_MAX_LINKED_DATA_BYTES];
    uint32_t exportCount;
    ExportSymbol exports[COMPILER_MAX_MODULE_SYMBOLS];
    uint32_t importCount;
    ImportSymbol imports[COMPILER_MAX_MODULE_SYMBOLS];
    uint32_t relocationCount;
    RelocationRecord relocations[COMPILER_MAX_MODULE_RELOCATIONS];
    uint32_t entryCodeOffset;
    uint32_t functionCount;
    uint32_t globalCount;
    bool hasEntry;
    bool hasHostLog;
    bool returnConstantValid;
    int32_t returnConstant;
    bool recursiveFunction[COMPILER_MAX_FUNCTIONS];
    uint32_t localStorageBytes[COMPILER_MAX_FUNCTIONS];
    bool callGraph[COMPILER_MAX_FUNCTIONS][COMPILER_MAX_FUNCTIONS];
    uint64_t sourceHash;
    uint16_t recursiveSccCount;
    uint16_t reserved;
};

struct GlobalFunctionSymbol {
    SymbolKind kind;
    char name[COMPILER_FUNCTION_NAME_CAPACITY];
    uint16_t moduleIndex;
    uint16_t parameterCount;
    uint32_t moduleCodeOffset;
    uint32_t finalCodeOffset;
    uint32_t moduleDataOffset;
    uint32_t finalDataOffset;
    uint32_t size;
    uint32_t alignment;
    uint16_t elementCount;
    uint16_t elementSize;
    bool isEntry;
    uint8_t reserved[3];
};

struct LinkedProgram {
    uint32_t moduleCount;
    uint32_t codeBytes;
    uint8_t code[COMPILER_MAX_LINKED_CODE_BYTES];
    uint32_t dataBytes;
    uint8_t data[COMPILER_MAX_LINKED_DATA_BYTES];
    uint32_t mutableDataBytes;
    uint8_t mutableData[COMPILER_MAX_LINKED_DATA_BYTES];
    uint32_t entryCodeOffset;
    uint32_t dataFileOffset;
    uint32_t mutableDataFileOffset;
    uint32_t exportCount;
    GlobalFunctionSymbol exports[COMPILER_MAX_PROJECT_EXPORTS];
    uint32_t importCount;
    uint32_t relocationCount;
    uint16_t recursiveSccCount;
    bool recursiveFunction[COMPILER_MAX_PROJECT_EXPORTS];
    bool linked;
};

} // namespace compiler
} // namespace kernel
