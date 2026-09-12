#include "kernel/core/native_elf/native_elf_debug_watches.h"

#include <iostream>

namespace {

using kernel::native_elf::NativeDebugWatchFrame;
using kernel::native_elf::NativeDebugWatchResult;
using kernel::native_elf::NativeDebugWatchStatus;
using kernel::native_elf::NativeDebugWatchValueType;
using kernel::native_elf::native_debug_watch_evaluate;

bool expect(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << "native_debug_watches_test FAIL: " << message << "\n";
    return false;
}

void addVariable(gx_development_debug_variables* variables, uint32_t index,
                 const char* name, uint32_t type, uint64_t rawValue,
                 int64_t signedValue, uint32_t flags =
                     GX_DEVELOPMENT_DEBUG_VARIABLE_VALIDATED |
                     GX_DEVELOPMENT_DEBUG_VARIABLE_LIVE |
                     GX_DEVELOPMENT_DEBUG_VARIABLE_INITIALIZED |
                     GX_DEVELOPMENT_DEBUG_VARIABLE_STABLE_FRAME_SLOT |
                     GX_DEVELOPMENT_DEBUG_VARIABLE_VALUE_VALID)
{
    gx_development_debug_variable& variable = variables->variables[index];
    uint32_t i = 0;
    while (name && name[i] && i + 1 < GX_DEVELOPMENT_DEBUG_MAX_VARIABLE_NAME_BYTES) {
        variable.name[i] = name[i];
        ++i;
    }
    variable.name[i] = '\0';
    variable.type = type;
    variable.location = GX_DEVELOPMENT_DEBUG_VARIABLE_LOCATION_RBP_RELATIVE;
    variable.flags = flags;
    variable.sizeBytes = type == GX_DEVELOPMENT_DEBUG_VARIABLE_TYPE_POINTER ? 8 : 4;
    variable.availability = GX_DEVELOPMENT_DEBUG_VARIABLE_AVAILABILITY_AVAILABLE;
    variable.rawValue = rawValue;
    variable.signedValue = signedValue;
    variable.unsignedValue = rawValue;
    ++variables->variableCount;
}

NativeDebugWatchFrame frameFor(const gx_development_debug_variables& variables)
{
    NativeDebugWatchFrame frame = {};
    frame.paused = true;
    frame.frameIndex = 1;
    frame.sessionGeneration = 9;
    frame.stopGeneration = 3;
    frame.instructionPointer = variables.instructionPointer;
    frame.framePointer = variables.framePointer;
    frame.variables = &variables;
    frame.variableMetadataAvailable = true;
    return frame;
}

bool evaluates(const char* expression, const NativeDebugWatchFrame& frame,
               NativeDebugWatchStatus status, NativeDebugWatchValueType type,
               int64_t signedValue)
{
    NativeDebugWatchResult result = {};
    const bool ok = native_debug_watch_evaluate(expression, frame, &result);
    return (status == NativeDebugWatchStatus::Success ? ok : !ok) &&
        result.status == status && result.type == type && result.signedValue == signedValue;
}

} // namespace

int main()
{
    gx_development_debug_variables variables = {};
    variables.size = sizeof(variables);
    variables.version = GX_DEVELOPMENT_DEBUG_API_VERSION;
    variables.status = GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_SUCCESS;
    variables.sessionGeneration = 9;
    variables.stopGeneration = 3;
    variables.instructionPointer = 0x100010b4;
    variables.framePointer = 0x101fff30;
    addVariable(&variables, 0, "counter", GX_DEVELOPMENT_DEBUG_VARIABLE_TYPE_SIGNED_INT32, 10, 10);
    addVariable(&variables, 1, "argc", GX_DEVELOPMENT_DEBUG_VARIABLE_TYPE_SIGNED_INT32, 2, 2);
    addVariable(&variables, 2, "x", GX_DEVELOPMENT_DEBUG_VARIABLE_TYPE_SIGNED_INT32, 3, 3);
    addVariable(&variables, 3, "y", GX_DEVELOPMENT_DEBUG_VARIABLE_TYPE_SIGNED_INT32, 4, 4);
    addVariable(&variables, 4, "ptr", GX_DEVELOPMENT_DEBUG_VARIABLE_TYPE_POINTER, 0x101fff00, 0);
    addVariable(&variables, 5, "dead", GX_DEVELOPMENT_DEBUG_VARIABLE_TYPE_SIGNED_INT32, 99, 99,
                GX_DEVELOPMENT_DEBUG_VARIABLE_VALIDATED);
    addVariable(&variables, 6, "unvalidated", GX_DEVELOPMENT_DEBUG_VARIABLE_TYPE_SIGNED_INT32, 7, 7,
                GX_DEVELOPMENT_DEBUG_VARIABLE_LIVE | GX_DEVELOPMENT_DEBUG_VARIABLE_VALUE_VALID);
    const NativeDebugWatchFrame frame = frameFor(variables);

    if (!expect(evaluates("counter", frame, NativeDebugWatchStatus::Success,
                          NativeDebugWatchValueType::SignedInt32, 10), "identifier evaluation")) return 1;
    if (!expect(evaluates("counter + 1", frame, NativeDebugWatchStatus::Success,
                          NativeDebugWatchValueType::SignedInt32, 11), "arithmetic evaluation")) return 1;
    if (!expect(evaluates("x + y * 2", frame, NativeDebugWatchStatus::Success,
                          NativeDebugWatchValueType::SignedInt32, 11), "operator precedence")) return 1;
    if (!expect(evaluates("(x + y) * 2", frame, NativeDebugWatchStatus::Success,
                          NativeDebugWatchValueType::SignedInt32, 14), "parentheses precedence")) return 1;
    if (!expect(evaluates("counter == 10 && argc == 2", frame, NativeDebugWatchStatus::Success,
                          NativeDebugWatchValueType::Boolean, 1), "boolean evaluation")) return 1;
    if (!expect(evaluates("-counter + +1", frame, NativeDebugWatchStatus::Success,
                          NativeDebugWatchValueType::SignedInt32, -9), "unary operators")) return 1;
    if (!expect(evaluates("0x10 + 2", frame, NativeDebugWatchStatus::Success,
                          NativeDebugWatchValueType::SignedInt32, 18), "hex literal")) return 1;
    if (!expect(evaluates("-7 / 2", frame, NativeDebugWatchStatus::Success,
                          NativeDebugWatchValueType::SignedInt32, -3), "signed division")) return 1;
    if (!expect(evaluates("-7 % 2", frame, NativeDebugWatchStatus::Success,
                          NativeDebugWatchValueType::SignedInt32, -1), "signed modulo")) return 1;
    if (!expect(evaluates("counter / 0", frame, NativeDebugWatchStatus::DivideByZero,
                          NativeDebugWatchValueType::SignedInt32, 0), "divide by zero")) return 1;
    if (!expect(evaluates("dead", frame, NativeDebugWatchStatus::NotLive,
                          NativeDebugWatchValueType::SignedInt32, 0), "not-live variable")) return 1;
    if (!expect(evaluates("unvalidated", frame, NativeDebugWatchStatus::NotLive,
                          NativeDebugWatchValueType::SignedInt32, 0), "unvalidated variable")) return 1;
    if (!expect(evaluates("missing", frame, NativeDebugWatchStatus::UnknownIdentifier,
                          NativeDebugWatchValueType::SignedInt32, 0), "unknown identifier")) return 1;
    if (!expect(evaluates("counter = 5", frame, NativeDebugWatchStatus::UnsupportedOperator,
                          NativeDebugWatchValueType::SignedInt32, 0), "assignment rejected")) return 1;
    if (!expect(evaluates("counter++", frame, NativeDebugWatchStatus::SyntaxError,
                          NativeDebugWatchValueType::SignedInt32, 0), "increment rejected")) return 1;
    if (!expect(evaluates("foo()", frame, NativeDebugWatchStatus::UnsupportedOperator,
                          NativeDebugWatchValueType::SignedInt32, 0), "function call rejected")) return 1;
    if (!expect(evaluates("9223372036854775807 + 1", frame, NativeDebugWatchStatus::Overflow,
                          NativeDebugWatchValueType::SignedInt32, 0), "overflow policy")) return 1;
    if (!expect(evaluates("-2147483648", frame, NativeDebugWatchStatus::Success,
                          NativeDebugWatchValueType::SignedInt32,
                          static_cast<int64_t>(-2147483647LL - 1)), "minimum integer")) return 1;
    NativeDebugWatchResult pointerResult = {};
    if (!expect(native_debug_watch_evaluate("ptr", frame, &pointerResult) &&
                    pointerResult.type == NativeDebugWatchValueType::Pointer &&
                    pointerResult.rawValue == 0x101fff00ULL,
                "pointer scalar")) return 1;
    NativeDebugWatchResult pointerComparisonResult = {};
    if (!expect(native_debug_watch_evaluate("ptr != 0", frame, &pointerComparisonResult) &&
                    pointerComparisonResult.type == NativeDebugWatchValueType::Boolean &&
                    pointerComparisonResult.booleanValue &&
                    std::string(pointerComparisonResult.formatted) == "true",
                "pointer comparison")) return 1;
    if (!expect(evaluates("0x", frame, NativeDebugWatchStatus::SyntaxError,
                          NativeDebugWatchValueType::SignedInt32, 0), "malformed hex")) return 1;
    if (!expect(evaluates("*ptr", frame, NativeDebugWatchStatus::UnsupportedOperator,
                          NativeDebugWatchValueType::SignedInt32, 0), "pointer dereference rejected")) return 1;

    NativeDebugWatchResult staleResult = {};
    NativeDebugWatchFrame stale = frame;
    stale.stopGeneration = 4;
    if (!expect(!native_debug_watch_evaluate("counter", stale, &staleResult) &&
                    staleResult.status == NativeDebugWatchStatus::StaleGeneration,
                "stale generation rejection")) return 1;

    NativeDebugWatchResult invalidResult = {};
    NativeDebugWatchFrame invalid = frame;
    invalid.frameIndex = GX_DEVELOPMENT_DEBUG_MAX_CALL_STACK_FRAMES;
    if (!expect(!native_debug_watch_evaluate("counter", invalid, &invalidResult) &&
                    invalidResult.status == NativeDebugWatchStatus::InvalidSelectedFrame,
                "invalid frame rejection without fallback")) return 1;

    NativeDebugWatchResult runningResult = {};
    NativeDebugWatchFrame running = frame;
    running.paused = false;
    if (!expect(!native_debug_watch_evaluate("counter", running, &runningResult) &&
                    runningResult.status == NativeDebugWatchStatus::Running,
                "running state rejection")) return 1;

    NativeDebugWatchResult malformedResult = {};
    const char tooManyOperators[] = "x+x+x+x+x+x+x+x+x+x+x+x+x+x+x+x+x+x+x+x+x+x+x+x+x+x+x+x+x+x+x+x+x+x";
    if (!expect(!native_debug_watch_evaluate(tooManyOperators, frame, &malformedResult) &&
                    malformedResult.status == NativeDebugWatchStatus::TooComplex,
                "complexity bound")) return 1;

    std::cout << "native_debug_watches_test: PASS\n";
    return 0;
}
