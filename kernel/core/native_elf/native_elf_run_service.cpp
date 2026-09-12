//
// Bare-metal Developer Studio Run service.
//

#include "native_elf_run_service.h"

#include "native_elf_development_app_model.h"
#include "native_elf_loader.h"
#include "native_elf_scheduler.h"
#include "native_elf_validator.h"
#include "native_elf_source_step.h"
#include "native_elf_step_out.h"
#include "native_elf_call_stack.h"
#include "native_elf_debug_watches.h"
#include "../compiler/elf_writer.h"
#include "../include/kernel/kernel_app.h"
#include "kernel/serial_debug.h"
#include "kernel/vfs.h"

#if defined(__x86_64__)
#include "arch/amd64.h"
#include "arch/context_switch.h"
#endif

namespace kernel {
namespace native_elf {
namespace NativeElfRunService {
namespace {

static const char kTarget[] = "guidexos.amd64.baremetal.bootstrap.native";
static const char kProjectKind[] = "native-gui-application";
static const char kAbi[] = "guidexos-c-abi-v1";
static const char kArchitecture[] = "amd64";
static const char kManifest[] = "app/app.json";
static const uint32_t kLegacySnapshotBytes =
    static_cast<uint32_t>(offsetof(gx_development_run_snapshot, outputCount));
static const uint32_t kMaxPath = 256U;
static const uint32_t kMaxProjectBytes = 16U * 1024U;
static const uint64_t kAmd64TrapFlag = 0x100ULL;
static const uint32_t kDebugStartBytes = 16U;
static const uint32_t kStepOverCallBytes = 5U;
static const uint32_t kStepOverFunctionProbeBytes = 256U;
static const uint32_t kStepOverNestedReturnLimit = 8U;
static const uint32_t kStepOutCallerProbeBytes = 256U;
static const uint32_t kStepOutPrologueProbeBytes = 128U;

struct Operation {
    bool used;
    gx_development_run_handle handle;
    gx_development_run_state state;
    gx_development_run_error_code error;
    bool closeRequested;
    bool cancellationRequested;
    bool appModelRegistered;
    bool cleanupComplete;
    bool nativeRuntimeStarted;
    bool debugControlled;
    bool debugBreakpointInstalled;
    bool debugBreakpointHit;
    bool debugCancelRequested;
    bool debugStepActive;
    bool debugStepTrapObserved;
    bool debugSourceStepActive;
    bool debugStepOverActive;
    bool debugStepOverReturnBreakpointInstalled;
    bool debugStepOverReturnBreakpointHit;
    bool debugStepOutActive;
    bool debugStepOutReturnBreakpointInstalled;
    bool debugStepOutReturnBreakpointHit;
    bool debugSourceSelected;
    bool debugCurrentSourceMappingValid;
    uint64_t registrationGeneration;
    uint64_t debugBreakpointAddress;
    uint8_t debugBreakpointOriginalByte;
    uint64_t debugStopGeneration;
    uint64_t debugStepToken;
    uint64_t debugStepStartRip;
    uint64_t debugStepRflagsBefore;
    uint64_t debugStepRflagsWithTrapFlag;
    uint64_t debugStepRflagsAfterClear;
    uint64_t debugStepTrapRip;
    uint64_t debugStepOverToken;
    uint64_t debugStepOverCallRip;
    uint64_t debugStepOverCallTargetAddress;
    uint64_t debugStepOverReturnAddress;
    uint64_t debugStepOverStartingRsp;
    uint64_t debugStepOverStartingRbp;
    uint64_t debugStepOverReturnTrapRip;
    uint64_t debugStepOverFinalRsp;
    uint64_t debugStepOverFinalRbp;
    uint8_t debugStepOverOriginalReturnByte;
    uint8_t debugStepOverOriginalReturnByteValid;
    uint32_t debugStepOverNestedReturnCount;
    uint32_t debugStepOverTemporaryBreakpointCount;
    uint32_t debugStepOverInternalMachineStepCount;
    uint32_t debugStepOverCallerFrameVerified;
    uint32_t debugSourceLine;
    uint32_t debugSourceColumn;
    uint32_t debugResolvedFinalCodeOffset;
    uint32_t debugSourceInstructionBytes;
    uint32_t debugCodeFileOffset;
    uint32_t debugCodeBytes;
    uint32_t debugSourceStepResult;
    uint32_t debugSourceStepInstructionCount;
    uint64_t debugSourceStepStartRip;
    uint64_t debugSourceStepFinalRip;
    uint32_t debugSourceStepStartLine;
    uint32_t debugSourceStepStartColumn;
    char debugSourceStepStartPath[GX_DEVELOPMENT_DEBUG_MAX_SOURCE_PATH_BYTES];
    char debugSourceStepStartFunctionName[GX_DEVELOPMENT_DEBUG_MAX_FUNCTION_NAME_BYTES];
    char debugStepOverCalleeFunctionName[GX_DEVELOPMENT_DEBUG_MAX_FUNCTION_NAME_BYTES];
    uint32_t debugStepOutResult;
    uint32_t debugStepOutInstructionCount;
    uint64_t debugStepOutStartRip;
    uint64_t debugStepOutStartRsp;
    uint64_t debugStepOutStartRbp;
    uint64_t debugStepOutSavedCallerRbp;
    uint64_t debugStepOutReturnAddress;
    uint64_t debugStepOutReturnTrapRip;
    uint64_t debugStepOutFinalRip;
    uint64_t debugStepOutFinalRsp;
    uint64_t debugStepOutFinalRbp;
    uint32_t debugStepOutTemporaryBreakpointCount;
    uint32_t debugStepOutInternalMachineStepCount;
    uint32_t debugStepOutCallerFrameVerified;
    uint32_t debugStepOutRemainingCalleeExecuted;
    uint32_t debugStepOutIntermediatePauseCount;
    uint32_t debugStepOutCurrentFrameValid;
    uint64_t debugStepOutToken;
    uint8_t debugStepOutOriginalReturnByte;
    uint8_t debugStepOutOriginalReturnByteValid;
    char debugStepOutCalleeFunctionName[GX_DEVELOPMENT_DEBUG_MAX_FUNCTION_NAME_BYTES];
    char debugStepOutCallerFunctionName[GX_DEVELOPMENT_DEBUG_MAX_FUNCTION_NAME_BYTES];
    char debugStepOutCallerSourcePath[GX_DEVELOPMENT_DEBUG_MAX_SOURCE_PATH_BYTES];
    uint32_t debugStepOutCallerReturnSourceLine;
    uint32_t debugStartByteCount;
    uint8_t debugStartBytes[kDebugStartBytes];
    NativeElfDebugTrap::BreakpointContext* debugContext;
    char debugSourcePath[GX_DEVELOPMENT_RUN_MAX_PATH_BYTES];
    char debugFunctionName[GX_DEVELOPMENT_DEBUG_MAX_FUNCTION_NAME_BYTES];
    gx_development_debug_snapshot debugSnapshot;
    uint64_t artifactSize;
    char projectRoot[GX_DEVELOPMENT_RUN_MAX_PROJECT_ROOT_BYTES];
    char projectId[GX_DEVELOPMENT_RUN_MAX_PROJECT_ID_BYTES];
    char projectKind[64];
    char targetProfile[128];
    char manifestPath[GX_DEVELOPMENT_RUN_MAX_PATH_BYTES];
    char artifactPath[GX_DEVELOPMENT_RUN_MAX_PATH_BYTES];
    char artifactSha256[GX_DEVELOPMENT_RUN_MAX_SHA256_BYTES];
    char artifactArchitecture[32];
    char artifactAbi[64];
    char applicationId[GX_DEVELOPMENT_RUN_MAX_APP_ID_BYTES];
    char displayName[GX_DEVELOPMENT_RUN_MAX_DISPLAY_NAME_BYTES];
    char resolvedArtifact[kMaxPath];
    NativeElfRunReport report;
    int32_t exitCode;
    char errorMessage[GX_DEVELOPMENT_RUN_MAX_ERROR_BYTES];
};

static Operation s_operation = {};
static gx_development_run_handle s_nextHandle = 1;
static char s_projectText[kMaxProjectBytes + 1] = {};
static char s_manifestText[kMaxProjectBytes + 1] = {};
static char s_debugSource[compiler::COMPILER_MAX_SOURCE_BYTES + 1] = {};
static uint8_t s_artifact[NATIVE_APP_MAX_ELF_FILE_BYTES] = {};

#if defined(__x86_64__)
static const uint32_t kSchedulerStackBytes = 32U * 1024U;
static uint8_t s_schedulerStack[kSchedulerStackBytes] __attribute__((aligned(16))) = {};
using SchedulerContext = arch::amd64::context::SwitchContext;
static SchedulerContext* s_ownerContext = nullptr;
static SchedulerContext* s_targetContext = nullptr;
static bool s_schedulerActive = false;
static bool s_schedulerInTarget = false;
static bool s_schedulerTargetComplete = false;
static void scheduled_debug_cancel_entry();
static SchedulerContext* make_debug_cancel_context();
#endif

static uint32_t text_length(const char* text, uint32_t capacity) {
    if (!text) return 0;
    uint32_t length = 0;
    while (length < capacity && text[length] != '\0') ++length;
    return length;
}

static bool copy_text(char* destination, uint32_t capacity, const char* source) {
    if (!destination || capacity == 0 || !source) return false;
    uint32_t i = 0;
    while (i + 1 < capacity && source[i] != '\0') {
        destination[i] = source[i];
        ++i;
    }
    if (source[i] != '\0') {
        destination[0] = '\0';
        return false;
    }
    destination[i] = '\0';
    return true;
}

static bool equal_text(const char* left, const char* right) {
    if (!left || !right) return left == right;
    uint32_t i = 0;
    while (left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) return false;
        ++i;
    }
    return left[i] == right[i];
}

static bool starts_with(const char* value, const char* prefix)
{
    if (!value || !prefix) return false;
    uint32_t i = 0;
    while (prefix[i] != '\0') {
        if (value[i] != prefix[i]) return false;
        ++i;
    }
    return true;
}

static uint32_t snapshot_capacity(const gx_development_run_snapshot* snapshot) {
    if (!snapshot) return 0;
    return snapshot->size < sizeof(gx_development_run_snapshot)
        ? snapshot->size : static_cast<uint32_t>(sizeof(gx_development_run_snapshot));
}

static bool snapshot_has(const gx_development_run_snapshot* snapshot, uint32_t offset, uint32_t bytes) {
    const uint32_t capacity = snapshot_capacity(snapshot);
    return capacity >= offset && bytes <= capacity - offset;
}

static void clear_snapshot(gx_development_run_snapshot* snapshot) {
    if (!snapshot) return;
    const uint32_t capacity = snapshot_capacity(snapshot);
    if (capacity < kLegacySnapshotBytes) return;
    for (uint32_t i = 0; i < capacity; ++i) reinterpret_cast<uint8_t*>(snapshot)[i] = 0;
    snapshot->size = capacity;
    snapshot->version = GX_DEVELOPMENT_RUN_API_VERSION;
    snapshot->state = GX_DEVELOPMENT_RUN_EMPTY;
    snapshot->errorCode = GX_DEVELOPMENT_RUN_ERROR_NONE;
}

static void set_failure(gx_development_run_snapshot* snapshot,
                        gx_development_run_error_code error,
                        const char* message) {
    if (!snapshot || snapshot_capacity(snapshot) < kLegacySnapshotBytes) return;
    const uint32_t capacity = snapshot_capacity(snapshot);
    clear_snapshot(snapshot);
    snapshot->state = GX_DEVELOPMENT_RUN_FAILED;
    snapshot->errorCode = error;
    copy_text(snapshot->errorMessage, sizeof(snapshot->errorMessage), message ? message : "bare-metal Run failed");
    (void)capacity;
}

static void snapshot_operation(const Operation& operation, gx_development_run_snapshot* snapshot) {
    if (!snapshot || snapshot_capacity(snapshot) < kLegacySnapshotBytes) return;
    const uint32_t capacity = snapshot_capacity(snapshot);
    clear_snapshot(snapshot);
    snapshot->handle = operation.handle;
    snapshot->state = operation.state;
    snapshot->errorCode = operation.error;
    snapshot->exitCode = operation.exitCode;
    snapshot->cleanupComplete = operation.cleanupComplete ? 1U : 0U;
    copy_text(snapshot->applicationId, sizeof(snapshot->applicationId), operation.applicationId);
    copy_text(snapshot->displayName, sizeof(snapshot->displayName), operation.displayName);
    copy_text(snapshot->artifactSha256, sizeof(snapshot->artifactSha256), operation.artifactSha256);
    copy_text(snapshot->errorMessage, sizeof(snapshot->errorMessage), operation.errorMessage);
    if (snapshot_has(snapshot, offsetof(gx_development_run_snapshot, outputCount), sizeof(snapshot->outputCount))) {
        const NativeAppExecutionContext* liveRuntime = native_elf_execution_active()
            ? native_elf_runtime_context() : nullptr;
        const uint32_t liveLogCount = liveRuntime ? liveRuntime->hostLogCount : 0U;
        const uint32_t reportLogCount = operation.report.hostLogCount;
        const uint32_t logCount = liveLogCount > reportLogCount ? liveLogCount : reportLogCount;
        snapshot->outputCount = logCount > GX_DEVELOPMENT_RUN_MAX_OUTPUT_LINES
            ? GX_DEVELOPMENT_RUN_MAX_OUTPUT_LINES : logCount;
        snapshot->outputTruncated = operation.report.hostLogTruncated ||
            (liveRuntime && liveRuntime->hostLogTruncated) ||
            logCount > GX_DEVELOPMENT_RUN_MAX_OUTPUT_LINES ? 1U : 0U;
        const uint32_t available = capacity > offsetof(gx_development_run_snapshot, output)
            ? (capacity - static_cast<uint32_t>(offsetof(gx_development_run_snapshot, output))) /
                sizeof(gx_development_run_output_line) : 0U;
        if (snapshot->outputCount > available) {
            snapshot->outputCount = available;
            snapshot->outputTruncated = 1U;
        }
        for (uint32_t i = 0; i < snapshot->outputCount; ++i) {
            if (liveRuntime && i < liveLogCount) {
                copy_text(snapshot->output[i].text, sizeof(snapshot->output[i].text), liveRuntime->hostLog[i]);
            } else {
                copy_text(snapshot->output[i].text, sizeof(snapshot->output[i].text), operation.report.hostLog[i]);
            }
        }
    }
    if (snapshot_has(snapshot, offsetof(gx_development_run_snapshot, closeRequested),
                     sizeof(snapshot->closeRequested))) {
        snapshot->closeRequested = operation.closeRequested ? 1U : 0U;
    }
    if (snapshot_has(snapshot, offsetof(gx_development_run_snapshot, cancellationRequested),
                     sizeof(snapshot->cancellationRequested))) {
        snapshot->cancellationRequested = operation.cancellationRequested ? 1U : 0U;
    }
    if (snapshot_has(snapshot, offsetof(gx_development_run_snapshot, generation),
                     sizeof(snapshot->generation))) {
        snapshot->generation = operation.registrationGeneration;
    }
}

static void clear_debug_snapshot(gx_development_debug_snapshot* snapshot)
{
    if (!snapshot) return;
    *snapshot = {};
    snapshot->size = sizeof(*snapshot);
    snapshot->version = GX_DEVELOPMENT_DEBUG_API_VERSION;
    snapshot->status = GX_DEVELOPMENT_DEBUG_STATUS_NONE;
}

static void set_debug_error(gx_development_debug_snapshot* snapshot, const char* message)
{
    clear_debug_snapshot(snapshot);
    if (!snapshot) return;
    snapshot->status = GX_DEVELOPMENT_DEBUG_STATUS_REJECTED;
    copy_text(snapshot->errorMessage, sizeof(snapshot->errorMessage),
              message ? message : "NativeElf debug request rejected");
}

static void set_debug_identity(const Operation& operation,
                               gx_development_debug_snapshot* snapshot)
{
    if (!snapshot) return;
    snapshot->bindingId = 1;
    snapshot->processId = 0;
    snapshot->nativeRuntimeId = operation.registrationGeneration;
    snapshot->threadId = 1;
    snapshot->sessionGeneration = operation.registrationGeneration;
    snapshot->targetAddress = operation.debugBreakpointAddress;
    snapshot->originalByte = operation.debugBreakpointOriginalByte;
    snapshot->installedByte = operation.debugBreakpointInstalled ? 0xCC : operation.debugBreakpointOriginalByte;
    snapshot->originalByteValid = operation.debugBreakpointInstalled || operation.debugBreakpointHit ? 1U : 0U;
    snapshot->bindingInstalled = operation.debugBreakpointInstalled ? 1U : 0U;
    snapshot->bindingCount = operation.debugBreakpointInstalled ? 1U : 0U;
    copy_text(snapshot->functionName, sizeof(snapshot->functionName),
              operation.debugCurrentSourceMappingValid
                  ? operation.debugFunctionName : "gx_main");
    if (operation.debugCurrentSourceMappingValid) {
        snapshot->sourceMappingValid = 1;
        copy_text(snapshot->sourcePath, sizeof(snapshot->sourcePath), operation.debugSourcePath);
        snapshot->sourceLine = operation.debugSourceLine;
        snapshot->sourceColumn = operation.debugSourceColumn;
    }
}

static void set_source_step_metadata(const Operation& operation,
                                     gx_development_debug_snapshot* snapshot);
static void set_source_step_over_metadata(
    const Operation& operation, gx_development_debug_snapshot* snapshot);
static void set_source_step_out_metadata(
    const Operation& operation, gx_development_debug_snapshot* snapshot);

static void set_debug_ready_snapshot(const Operation& operation,
                                      gx_development_debug_snapshot* snapshot)
{
    clear_debug_snapshot(snapshot);
    if (!snapshot) return;
    snapshot->status = GX_DEVELOPMENT_DEBUG_STATUS_READY;
    set_debug_identity(operation, snapshot);
    set_source_step_metadata(operation, snapshot);
    if (operation.debugStepOverActive) set_source_step_over_metadata(operation, snapshot);
    if (operation.debugStepOutActive || operation.debugStepOutReturnAddress != 0)
        set_source_step_out_metadata(operation, snapshot);
}

static bool resolve_debug_mapping_at_address(
    const Operation& operation, uint64_t address,
    compiler::ResolvedSourceMapping* mapping, const char** error)
{
    if (operation.debugCodeBytes == 0) {
        if (error) *error = "debug session has no source-map code range";
        return false;
    }
    return compiler::resolve_bootstrap_source_mapping_at_address(
        s_artifact, static_cast<uint32_t>(operation.artifactSize),
        guidexos::native_elf::IMAGE_BASE, operation.debugCodeFileOffset,
        operation.debugCodeBytes, address, mapping, error);
}

static void clear_unmapped_source_identity(gx_development_debug_snapshot* snapshot)
{
    if (!snapshot) return;
    snapshot->sourceMappingValid = 0;
    snapshot->sourcePath[0] = '\0';
    snapshot->sourceLine = 0;
    snapshot->sourceColumn = 0;
}

static void set_debug_source_mapping(Operation& operation,
                                     gx_development_debug_snapshot* snapshot,
                                     const compiler::ResolvedSourceMapping& mapping)
{
    if (!snapshot) return;
    operation.debugCurrentSourceMappingValid = true;
    copy_text(operation.debugSourcePath, sizeof(operation.debugSourcePath), mapping.sourcePath);
    operation.debugSourceLine = mapping.line;
    operation.debugSourceColumn = mapping.column;
    copy_text(operation.debugFunctionName, sizeof(operation.debugFunctionName), mapping.functionName);
    snapshot->sourceMappingValid = 1;
    copy_text(snapshot->sourcePath, sizeof(snapshot->sourcePath), mapping.sourcePath);
    snapshot->sourceLine = mapping.line;
    snapshot->sourceColumn = mapping.column;
    copy_text(snapshot->functionName, sizeof(snapshot->functionName), mapping.functionName);
}

static void set_source_step_metadata(const Operation& operation,
                                     gx_development_debug_snapshot* snapshot)
{
    if (!snapshot) return;
    snapshot->sourceStepResult = operation.debugSourceStepResult;
    snapshot->sourceStepInstructionCount = operation.debugSourceStepInstructionCount;
    snapshot->sourceStepInstructionLimit = kNativeElfSourceStepInstructionLimit;
    snapshot->sourceStepStartingMappingValid =
        operation.debugSourceStepStartPath[0] != '\0' ? 1U : 0U;
    snapshot->sourceStepStartRip = operation.debugSourceStepStartRip;
    snapshot->sourceStepFinalRip = operation.debugSourceStepFinalRip;
    snapshot->sourceStepStartLine = operation.debugSourceStepStartLine;
    snapshot->sourceStepStartColumn = operation.debugSourceStepStartColumn;
    copy_text(snapshot->sourceStepStartPath, sizeof(snapshot->sourceStepStartPath),
              operation.debugSourceStepStartPath);
    copy_text(snapshot->sourceStepStartFunctionName,
              sizeof(snapshot->sourceStepStartFunctionName),
              operation.debugSourceStepStartFunctionName);
}

static void set_source_step_over_metadata(
    const Operation& operation, gx_development_debug_snapshot* snapshot)
{
    if (!snapshot) return;
    snapshot->sourceStepOverResult = operation.debugSourceStepResult;
    snapshot->sourceStepOverInstructionCount = operation.debugSourceStepInstructionCount;
    snapshot->sourceStepOverInstructionLimit = kNativeElfSourceStepInstructionLimit;
    snapshot->sourceStepOverCallDetected =
        (operation.debugStepOverActive || operation.debugStepOverCallRip != 0) ? 1U : 0U;
    snapshot->sourceStepOverNestedReturnCount = operation.debugStepOverNestedReturnCount;
    snapshot->sourceStepOverTemporaryBreakpointCount = operation.debugStepOverTemporaryBreakpointCount;
    snapshot->sourceStepOverCallRip = operation.debugStepOverCallRip;
    snapshot->sourceStepOverCallTargetAddress = operation.debugStepOverCallTargetAddress;
    snapshot->sourceStepOverReturnAddress = operation.debugStepOverReturnAddress;
    snapshot->sourceStepOverStartingRsp = operation.debugStepOverStartingRsp;
    snapshot->sourceStepOverStartingRbp = operation.debugStepOverStartingRbp;
    snapshot->sourceStepOverReturnTrapRip = operation.debugStepOverReturnTrapRip;
    snapshot->sourceStepOverFinalRsp = operation.debugStepOverFinalRsp;
    snapshot->sourceStepOverFinalRbp = operation.debugStepOverFinalRbp;
    snapshot->sourceStepOverInternalMachineStepCount =
        operation.debugStepOverInternalMachineStepCount;
    snapshot->sourceStepOverCallerFrameVerified = operation.debugStepOverCallerFrameVerified;
    copy_text(snapshot->sourceStepOverCalleeFunctionName,
              sizeof(snapshot->sourceStepOverCalleeFunctionName),
              operation.debugStepOverCalleeFunctionName);
    snapshot->sourceStepOverOriginalReturnByte = operation.debugStepOverOriginalReturnByte;
    snapshot->sourceStepOverOriginalReturnByteValid =
        operation.debugStepOverOriginalReturnByteValid;
}

static void set_source_step_out_metadata(
    const Operation& operation, gx_development_debug_snapshot* snapshot)
{
    if (!snapshot) return;
    snapshot->sourceStepOutResult = operation.debugStepOutResult;
    snapshot->sourceStepOutInstructionCount = operation.debugStepOutInstructionCount;
    snapshot->sourceStepOutInstructionLimit = kNativeElfStepOutInstructionLimit;
    snapshot->sourceStepOutCurrentFrameValid = operation.debugStepOutCurrentFrameValid;
    snapshot->sourceStepOutStartRip = operation.debugStepOutStartRip;
    snapshot->sourceStepOutStartRsp = operation.debugStepOutStartRsp;
    snapshot->sourceStepOutStartRbp = operation.debugStepOutStartRbp;
    snapshot->sourceStepOutSavedCallerRbp = operation.debugStepOutSavedCallerRbp;
    snapshot->sourceStepOutReturnAddress = operation.debugStepOutReturnAddress;
    snapshot->sourceStepOutReturnTrapRip = operation.debugStepOutReturnTrapRip;
    snapshot->sourceStepOutFinalRip = operation.debugStepOutFinalRip;
    snapshot->sourceStepOutFinalRsp = operation.debugStepOutFinalRsp;
    snapshot->sourceStepOutFinalRbp = operation.debugStepOutFinalRbp;
    snapshot->sourceStepOutTemporaryBreakpointCount =
        operation.debugStepOutTemporaryBreakpointCount;
    snapshot->sourceStepOutInternalMachineStepCount =
        operation.debugStepOutInternalMachineStepCount;
    snapshot->sourceStepOutCallerFrameVerified = operation.debugStepOutCallerFrameVerified;
    snapshot->sourceStepOutRemainingCalleeExecuted =
        operation.debugStepOutRemainingCalleeExecuted;
    snapshot->sourceStepOutIntermediatePauseCount =
        operation.debugStepOutIntermediatePauseCount;
    copy_text(snapshot->sourceStepOutCalleeFunctionName,
              sizeof(snapshot->sourceStepOutCalleeFunctionName),
              operation.debugStepOutCalleeFunctionName);
    copy_text(snapshot->sourceStepOutCallerFunctionName,
              sizeof(snapshot->sourceStepOutCallerFunctionName),
              operation.debugStepOutCallerFunctionName);
    copy_text(snapshot->sourceStepOutCallerSourcePath,
              sizeof(snapshot->sourceStepOutCallerSourcePath),
              operation.debugStepOutCallerSourcePath);
    snapshot->sourceStepOutOriginalReturnByte = operation.debugStepOutOriginalReturnByte;
    snapshot->sourceStepOutOriginalReturnByteValid =
        operation.debugStepOutOriginalReturnByteValid;
}

static SourceStepLocation source_step_location_from_mapping(
    const compiler::ResolvedSourceMapping& mapping)
{
    SourceStepLocation location = {};
    location.mapped = true;
    location.line = mapping.line;
    location.column = mapping.column;
    copy_text(location.sourcePath, sizeof(location.sourcePath), mapping.sourcePath);
    copy_text(location.functionName, sizeof(location.functionName), mapping.functionName);
    return location;
}

struct DirectUserCallPlan {
    bool callFound;
    bool valid;
    uint64_t callRip;
    uint64_t targetAddress;
    uint64_t returnAddress;
    char calleeFunctionName[GX_DEVELOPMENT_DEBUG_MAX_FUNCTION_NAME_BYTES];
};

static bool debug_code_address(const Operation& operation, uint64_t address)
{
    const NativeAppExecutionContext* runtime = native_elf_runtime_context();
    if (!runtime || operation.debugCodeBytes == 0 ||
        runtime->imageBase > ~static_cast<uint64_t>(0) - operation.debugCodeFileOffset) return false;
    const uint64_t codeStart = runtime->imageBase + operation.debugCodeFileOffset;
    return operation.debugCodeBytes <= ~static_cast<uint64_t>(0) - codeStart &&
        address >= codeStart && address - codeStart < operation.debugCodeBytes;
}

static bool debug_read_stack_u64(const Operation& operation, uint64_t address,
                                 uint64_t* value)
{
    const NativeAppExecutionContext* runtime = native_elf_runtime_context();
    if (!value || !runtime || runtime->stackBase == 0 || runtime->stackSize == 0 ||
        runtime->stackBase > ~static_cast<uint64_t>(0) - runtime->stackSize ||
        !step_out_stack_range_contains(runtime->stackBase,
                                       runtime->stackBase + runtime->stackSize,
                                       address, sizeof(uint64_t)) ||
        (address & 7ULL) != 0) return false;
    (void)operation;
    *value = *reinterpret_cast<const volatile uint64_t*>(static_cast<uintptr_t>(address));
    return true;
}

static bool debug_read_stack_bytes(const Operation& operation, uint64_t address,
                                   uint32_t bytes, uint8_t* value)
{
    const NativeAppExecutionContext* runtime = native_elf_runtime_context();
    if (!value || !runtime || bytes == 0 || runtime->stackBase == 0 ||
        runtime->stackSize == 0 || runtime->stackBase >
            ~static_cast<uint64_t>(0) - runtime->stackSize ||
        !step_out_stack_range_contains(runtime->stackBase,
                                        runtime->stackBase + runtime->stackSize,
                                        address, bytes)) return false;
    (void)operation;
    volatile const uint8_t* source = reinterpret_cast<volatile const uint8_t*>(
        static_cast<uintptr_t>(address));
    for (uint32_t index = 0; index < bytes; ++index) value[index] = source[index];
    return true;
}

static bool debug_read_code_byte(const Operation& operation, uint64_t address,
                                 uint8_t* value);

static bool debug_compiler_frame_prologue(const Operation& operation,
                                          const compiler::ResolvedSourceMapping& mapping)
{
    static const uint8_t kPrologue[] = {0x55, 0x48, 0x89, 0xE5};
    if (mapping.targetAddress < sizeof(kPrologue)) return false;
    const uint64_t first = mapping.targetAddress - sizeof(kPrologue);
    const uint64_t lower = mapping.targetAddress > kStepOutPrologueProbeBytes
        ? mapping.targetAddress - kStepOutPrologueProbeBytes : 0;
    for (uint64_t candidate = first;; --candidate) {
        bool match = true;
        for (uint32_t i = 0; i < sizeof(kPrologue); ++i) {
            uint8_t value = 0;
            if (!debug_read_code_byte(operation, candidate + i, &value) ||
                value != kPrologue[i]) {
                match = false;
                break;
            }
        }
        if (match) return true;
        if (candidate == lower) break;
    }
    return false;
}

static bool resolve_debug_mapping_near_address(
    const Operation& operation, uint64_t address,
    compiler::ResolvedSourceMapping* mapping, const char** error)
{
    if (error) *error = "caller return address has no mapped user source";
    if (mapping) *mapping = {};
    compiler::ResolvedSourceMapping exact = {};
    const char* exactError = nullptr;
    if (resolve_debug_mapping_at_address(operation, address, &exact, &exactError)) {
        if (mapping) *mapping = exact;
        return true;
    }
    if (exactError && !source_step_text_equal(exactError, "source-map address is unmapped")) {
        if (error) *error = exactError;
        return false;
    }
    for (uint32_t distance = 1; distance <= kStepOutCallerProbeBytes; ++distance) {
        const uint64_t candidates[2] = {
            address <= ~static_cast<uint64_t>(0) - distance ? address + distance : 0,
            address >= distance ? address - distance : 0
        };
        for (uint32_t i = 0; i < 2; ++i) {
            if (candidates[i] == 0 || !debug_code_address(operation, candidates[i])) continue;
            compiler::ResolvedSourceMapping nearby = {};
            const char* nearbyError = nullptr;
            if (resolve_debug_mapping_at_address(operation, candidates[i], &nearby,
                                                 &nearbyError)) {
                if (mapping) *mapping = nearby;
                return true;
            }
            if (nearbyError &&
                !source_step_text_equal(nearbyError, "source-map address is unmapped")) {
                if (error) *error = nearbyError;
                return false;
            }
        }
    }
    return false;
}

static bool resolve_debug_mapping_forward_address(
    const Operation& operation, uint64_t address,
    compiler::ResolvedSourceMapping* mapping)
{
    if (mapping) *mapping = {};
    if (resolve_debug_mapping_at_address(operation, address, mapping, nullptr)) return true;
    for (uint32_t distance = 1; distance <= kStepOutCallerProbeBytes; ++distance) {
        if (address > ~static_cast<uint64_t>(0) - distance) break;
        compiler::ResolvedSourceMapping nearby = {};
        if (resolve_debug_mapping_at_address(operation, address + distance,
                                              &nearby, nullptr)) {
            if (mapping) *mapping = nearby;
            return true;
        }
    }
    return false;
}

struct StepOutFramePlan {
    bool valid;
    bool callerResolved;
    uint32_t failureResult;
    uint64_t startRip;
    uint64_t startRsp;
    uint64_t startRbp;
    uint64_t savedCallerRbp;
    uint64_t returnAddress;
    compiler::ResolvedSourceMapping callerMapping;
};

static StepOutFramePlan capture_step_out_frame(
    const Operation& operation, const compiler::ResolvedSourceMapping& currentMapping,
    const NativeElfDebugTrap::BreakpointContext* context, const char** error)
{
    StepOutFramePlan plan = {};
    if (error) *error = "NativeElf Step Out current frame is invalid";
    if (!context || context->cs != 0x08 || !debug_compiler_frame_prologue(operation, currentMapping)) {
        plan.failureResult = GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_UNSUPPORTED_FRAME;
        if (error) *error = "NativeElf Step Out requires the compiler frame-pointer ABI";
        return plan;
    }
    const NativeAppExecutionContext* runtime = native_elf_runtime_context();
    if (!runtime || runtime->stackBase > ~static_cast<uint64_t>(0) - runtime->stackSize ||
        !step_out_frame_shape_valid(context->rsp, context->rbp, runtime->stackBase,
                                    runtime->stackBase + runtime->stackSize)) {
        plan.failureResult = GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_INVALID_FRAME;
        if (error) *error = "NativeElf Step Out current frame is outside the runtime stack";
        return plan;
    }
    uint64_t savedCallerRbp = 0;
    uint64_t returnAddress = 0;
    if (!debug_read_stack_u64(operation, context->rbp, &savedCallerRbp) ||
        !debug_read_stack_u64(operation, context->rbp + sizeof(uint64_t), &returnAddress)) {
        plan.failureResult = GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_INVALID_FRAME;
        if (error) *error = "NativeElf Step Out frame links are unreadable";
        return plan;
    }
    if (!step_out_caller_link_valid(context->rbp, savedCallerRbp, returnAddress,
                                    runtime->stackBase,
                                    runtime->stackBase + runtime->stackSize,
                                    runtime->imageBase, runtime->imageSize)) {
        if (error) *error = returnAddress == 0 ||
            !debug_code_address(operation, returnAddress)
                ? "NativeElf Step Out has no caller frame in the user image"
                : "NativeElf Step Out caller frame link is invalid";
        plan.failureResult = returnAddress == 0 || !debug_code_address(operation, returnAddress)
            ? GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_NO_CALLER_FRAME
            : GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_INVALID_FRAME;
        plan.startRip = context->rip;
        plan.startRsp = context->rsp;
        plan.startRbp = context->rbp;
        plan.savedCallerRbp = savedCallerRbp;
        plan.returnAddress = returnAddress;
        return plan;
    }
    compiler::ResolvedSourceMapping callerMapping = {};
    const char* callerError = nullptr;
    if (!resolve_debug_mapping_near_address(operation, returnAddress, &callerMapping,
                                            &callerError)) {
        plan.failureResult = GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_INVALID_SOURCE_MAP;
        if (error) *error = callerError ? callerError :
            "NativeElf Step Out caller return address is not source mapped";
        plan.startRip = context->rip;
        plan.startRsp = context->rsp;
        plan.startRbp = context->rbp;
        plan.savedCallerRbp = savedCallerRbp;
        plan.returnAddress = returnAddress;
        return plan;
    }
    plan.valid = true;
    plan.callerResolved = true;
    plan.startRip = context->rip;
    plan.startRsp = context->rsp;
    plan.startRbp = context->rbp;
    plan.savedCallerRbp = savedCallerRbp;
    plan.returnAddress = returnAddress;
    plan.callerMapping = callerMapping;
    return plan;
}

static bool debug_read_code_byte(const Operation& operation, uint64_t address, uint8_t* value)
{
    if (!value || !debug_code_address(operation, address)) return false;
    *value = *reinterpret_cast<const volatile uint8_t*>(static_cast<uintptr_t>(address));
    return true;
}

static bool debug_function_at_entry(const Operation& operation, uint64_t address,
                                    const char* callerFunction, char* calleeFunction,
                                    uint32_t calleeCapacity)
{
    if (!calleeFunction || calleeCapacity == 0 || !debug_code_address(operation, address)) return false;
    calleeFunction[0] = '\0';
    for (uint32_t offset = 0; offset < kStepOverFunctionProbeBytes; ++offset) {
        if (address > ~static_cast<uint64_t>(0) - offset) break;
        if (!debug_code_address(operation, address + offset)) break;
        compiler::ResolvedSourceMapping mapping = {};
        const char* error = nullptr;
        if (!resolve_debug_mapping_at_address(operation, address + offset, &mapping, &error)) continue;
        if (callerFunction && equal_text(mapping.functionName, callerFunction)) continue;
        return copy_text(calleeFunction, calleeCapacity, mapping.functionName);
    }
    return false;
}

static DirectUserCallPlan classify_direct_user_call(
    const Operation& operation, const compiler::ResolvedSourceMapping& sourceMapping,
    const char** error)
{
    DirectUserCallPlan plan = {};
    if (error) *error = nullptr;
    const uint64_t mappingStart = sourceMapping.targetAddress;
    if (!debug_code_address(operation, mappingStart) || sourceMapping.instructionBytes == 0 ||
        sourceMapping.instructionBytes > kNativeElfSourceStepInstructionLimit * 64U) {
        if (error) *error = "source operation has no bounded executable span";
        return plan;
    }
    const uint64_t currentRip = operation.debugContext ? operation.debugContext->rip : mappingStart;
    const uint64_t start = currentRip > mappingStart ? currentRip : mappingStart;
    if (start < mappingStart || start - mappingStart >= sourceMapping.instructionBytes) {
        if (error) *error = "paused RIP is outside its trusted source operation";
        return plan;
    }
    const uint64_t end = mappingStart + sourceMapping.instructionBytes;
    uint32_t callCount = 0;
    for (uint64_t address = start; address < end; ++address) {
        uint8_t opcode = 0;
        if (!debug_read_code_byte(operation, address, &opcode)) break;
        if (opcode != 0xE8 || end - address < kStepOverCallBytes) continue;
        ++callCount;
        if (callCount != 1) continue;
        uint8_t displacementBytes[4] = {};
        for (uint32_t i = 0; i < sizeof(displacementBytes); ++i) {
            if (!debug_read_code_byte(operation, address + 1U + i, &displacementBytes[i])) {
                if (error) *error = "direct call displacement is unreadable";
                return plan;
            }
        }
        const uint32_t rawDisplacement = static_cast<uint32_t>(displacementBytes[0]) |
            (static_cast<uint32_t>(displacementBytes[1]) << 8) |
            (static_cast<uint32_t>(displacementBytes[2]) << 16) |
            (static_cast<uint32_t>(displacementBytes[3]) << 24);
        const int64_t displacement = static_cast<int32_t>(rawDisplacement);
        const uint64_t afterCall = address + kStepOverCallBytes;
        const uint64_t target = displacement >= 0
            ? afterCall + static_cast<uint64_t>(displacement)
            : afterCall - static_cast<uint64_t>(-displacement);
        plan.callRip = address;
        plan.targetAddress = target;
        plan.returnAddress = afterCall;
    }
    if (callCount == 0) return plan;
    if (callCount != 1) {
        if (error) *error = "multiple direct calls on one source operation are unsupported";
        return plan;
    }
    plan.callFound = true;
    if (!debug_code_address(operation, plan.targetAddress) ||
        !debug_code_address(operation, plan.returnAddress) ||
        !debug_function_at_entry(operation, plan.targetAddress,
                                 sourceMapping.functionName,
                                 plan.calleeFunctionName,
                                 sizeof(plan.calleeFunctionName))) {
        if (error) *error = "direct call target is not a mapped user function";
        return plan;
    }
    plan.valid = true;
    return plan;
}

static void copy_debug_start_bytes(const Operation& operation,
                                   gx_development_debug_snapshot* snapshot)
{
    if (!snapshot) return;
    snapshot->byteCount = operation.debugStartByteCount;
    if (snapshot->byteCount > sizeof(snapshot->bytes))
        snapshot->byteCount = sizeof(snapshot->bytes);
    for (uint32_t i = 0; i < snapshot->byteCount; ++i)
        snapshot->bytes[i] = operation.debugStartBytes[i];
}

static bool debug_request_identity_matches(const Operation& operation,
                                           const gx_development_debug_request& request)
{
    if (request.handle != operation.handle) return false;
    if (request.sessionGeneration != 0 &&
        request.sessionGeneration != operation.registrationGeneration) return false;
    if (request.processId != 0) return false;
    if (request.nativeRuntimeId != 0 &&
        request.nativeRuntimeId != operation.registrationGeneration) return false;
    if (request.threadId != 0 && request.threadId != 1) return false;
    if (request.breakpointId != 0 && request.breakpointId != 1) return false;
    if (request.targetAddress != 0 &&
        request.targetAddress != operation.debugBreakpointAddress) return false;
    if (request.artifactSha256 &&
        !equal_text(request.artifactSha256, operation.artifactSha256)) return false;
    if (request.stopGeneration != 0 &&
        request.stopGeneration != operation.debugStopGeneration) return false;
    return true;
}

static void serial_debug_hex(const char* prefix, uint64_t value)
{
    serial::puts(prefix);
    serial::put_hex64(value);
}

static void capture_debug_start_bytes(Operation& operation,
                                      const NativeAppExecutionContext& runtime,
                                      uint8_t originalByte)
{
    operation.debugStartByteCount = 0;
    for (uint32_t i = 0; i < kDebugStartBytes; ++i) operation.debugStartBytes[i] = 0;
    if (operation.debugBreakpointAddress < runtime.imageBase ||
        runtime.imageBase > ~static_cast<uint64_t>(0) - runtime.imageSize ||
        operation.debugBreakpointAddress >= runtime.imageBase + runtime.imageSize) return;
    const uint64_t available = runtime.imageBase + runtime.imageSize -
        operation.debugBreakpointAddress;
    const uint32_t count = available < kDebugStartBytes
        ? static_cast<uint32_t>(available) : kDebugStartBytes;
    if (count == 0) return;
    operation.debugStartBytes[0] = originalByte;
    for (uint32_t i = 1; i < count; ++i) {
        operation.debugStartBytes[i] = reinterpret_cast<const uint8_t*>(
            static_cast<uintptr_t>(operation.debugBreakpointAddress))[i];
    }
    operation.debugStartByteCount = count;
}

#if defined(__x86_64__)
extern "C" __attribute__((noinline)) int32_t native_elf_debug_cancel_return()
{
    return 0;
}
#endif

static bool is_slash(char value) { return value == '/' || value == '\\'; }

static bool safe_relative(const char* value) {
    if (!value || value[0] == '\0' || value[0] == '/' || value[0] == '\\' || value[1] == ':') return false;
    const uint32_t length = text_length(value, kMaxPath);
    if (length == 0 || length >= kMaxPath) return false;
    uint32_t segmentStart = 0;
    for (uint32_t i = 0; i <= length; ++i) {
        if (i < length && !is_slash(value[i])) {
            if (static_cast<unsigned char>(value[i]) < 0x20) return false;
            continue;
        }
        const uint32_t segmentLength = i - segmentStart;
        if (segmentLength == 0 || (segmentLength == 1 && value[segmentStart] == '.') ||
            (segmentLength == 2 && value[segmentStart] == '.' && value[segmentStart + 1] == '.')) return false;
        segmentStart = i + 1;
    }
    return true;
}

static bool safe_name(const char* value) {
    if (!value || value[0] == '\0' || value[0] == '.') return false;
    const uint32_t length = text_length(value, 128);
    if (length == 0 || length >= 128) return false;
    for (uint32_t i = 0; i < length; ++i) {
        const char c = value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
    }
    return true;
}

static bool join_path(const char* root, const char* relative, char* output, uint32_t capacity) {
    if (!root || !relative || !output || root[0] != '/' || !safe_relative(relative)) return false;
    if (!copy_text(output, capacity, root)) return false;
    uint32_t length = text_length(output, capacity);
    if (length + 1 >= capacity) return false;
    if (length == 0 || output[length - 1] != '/') output[length++] = '/';
    output[length] = '\0';
    return copy_text(output + length, capacity - length, relative);
}

static bool read_bounded(const char* path, char* output, uint32_t capacity) {
    if (!path || !output || capacity < 2) return false;
    vfs::FileInfo info = {};
    if (vfs::stat(path, &info) != vfs::VFS_OK || info.type != vfs::FILE_TYPE_REGULAR || info.size >= capacity) return false;
    const uint32_t bytes = static_cast<uint32_t>(info.size);
    const int32_t read = bytes == 0 ? 0 : vfs::read_file(path, output, bytes);
    if (read < 0 || static_cast<uint32_t>(read) != bytes) return false;
    output[bytes] = '\0';
    return true;
}

static uint64_t fnv1a_bytes(const uint8_t* bytes, uint32_t count)
{
    uint64_t hash = 1469598103934665603ULL;
    if (!bytes) return hash;
    for (uint32_t i = 0; i < count; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static bool json_string(const char* text, uint32_t length, const char* key, char* output, uint32_t capacity) {
    if (!text || !key || !output || capacity == 0) return false;
    output[0] = '\0';
    const uint32_t keyLength = text_length(key, 96);
    for (uint32_t i = 0; i + keyLength + 3 < length; ++i) {
        if (text[i] != '"') continue;
        uint32_t j = 0;
        while (j < keyLength && text[i + 1 + j] == key[j]) ++j;
        if (j != keyLength || text[i + 1 + j] != '"') continue;
        uint32_t cursor = i + keyLength + 2;
        while (cursor < length && (text[cursor] == ' ' || text[cursor] == '\t' || text[cursor] == '\r' || text[cursor] == '\n')) ++cursor;
        if (cursor >= length || text[cursor++] != ':') continue;
        while (cursor < length && (text[cursor] == ' ' || text[cursor] == '\t' || text[cursor] == '\r' || text[cursor] == '\n')) ++cursor;
        if (cursor >= length || text[cursor++] != '"') return false;
        uint32_t written = 0;
        while (cursor < length) {
            const char value = text[cursor++];
            if (value == '"') { output[written] = '\0'; return true; }
            if (value == '\\' || static_cast<unsigned char>(value) < 0x20 || written + 1 >= capacity) return false;
            output[written++] = value;
        }
        return false;
    }
    return false;
}

static void sha256_rotr(uint32_t value, uint32_t count, uint32_t* result) {
    *result = (value >> count) | (value << (32U - count));
}

class Sha256 {
public:
    Sha256() : bitCount(0), bufferBytes(0) {
        state[0]=0x6A09E667U; state[1]=0xBB67AE85U; state[2]=0x3C6EF372U; state[3]=0xA54FF53AU;
        state[4]=0x510E527FU; state[5]=0x9B05688CU; state[6]=0x1F83D9ABU; state[7]=0x5BE0CD19U;
    }
    void update(const uint8_t* bytes, uint32_t count) {
        while (count != 0) {
            const uint32_t room = 64U - bufferBytes;
            const uint32_t take = count < room ? count : room;
            for (uint32_t i = 0; i < take; ++i) buffer[bufferBytes + i] = bytes[i];
            bufferBytes += take; bytes += take; count -= take; bitCount += static_cast<uint64_t>(take) * 8ULL;
            if (bufferBytes == 64U) { transform(buffer); bufferBytes = 0; }
        }
    }
    void finish(char output[GX_DEVELOPMENT_RUN_MAX_SHA256_BYTES]) {
        buffer[bufferBytes++] = 0x80;
        while (bufferBytes != 56U) { if (bufferBytes == 64U) { transform(buffer); bufferBytes = 0; } buffer[bufferBytes++] = 0; }
        for (uint32_t i = 0; i < 8; ++i) buffer[56U + i] = static_cast<uint8_t>(bitCount >> (56U - i * 8U));
        transform(buffer);
        const char hex[] = "0123456789abcdef"; uint32_t out = 0;
        for (uint32_t i = 0; i < 8; ++i) for (uint32_t j = 0; j < 4; ++j) {
            const uint8_t value = static_cast<uint8_t>(state[i] >> (24U - j * 8U));
            output[out++] = hex[value >> 4]; output[out++] = hex[value & 0x0FU];
        }
        output[out] = '\0';
    }
private:
    void transform(const uint8_t block[64]) {
        static const uint32_t k[64] = {
            0x428A2F98U,0x71374491U,0xB5C0FBCFU,0xE9B5DBA5U,0x3956C25BU,0x59F111F1U,0x923F82A4U,0xAB1C5ED5U,
            0xD807AA98U,0x12835B01U,0x243185BEU,0x550C7DC3U,0x72BE5D74U,0x80DEB1FEU,0x9BDC06A7U,0xC19BF174U,
            0xE49B69C1U,0xEFBE4786U,0x0FC19DC6U,0x240CA1CCU,0x2DE92C6FU,0x4A7484AAU,0x5CB0A9DCU,0x76F988DAU,
            0x983E5152U,0xA831C66DU,0xB00327C8U,0xBF597FC7U,0xC6E00BF3U,0xD5A79147U,0x06CA6351U,0x14292967U,
            0x27B70A85U,0x2E1B2138U,0x4D2C6DFCU,0x53380D13U,0x650A7354U,0x766A0ABBU,0x81C2C92EU,0x92722C85U,
            0xA2BFE8A1U,0xA81A664BU,0xC24B8B70U,0xC76C51A3U,0xD192E819U,0xD6990624U,0xF40E3585U,0x106AA070U,
            0x19A4C116U,0x1E376C08U,0x2748774CU,0x34B0BCB5U,0x391C0CB3U,0x4ED8AA4AU,0x5B9CCA4FU,0x682E6FF3U,
            0x748F82EEU,0x78A5636FU,0x84C87814U,0x8CC70208U,0x90BEFFFAU,0xA4506CEBU,0xBEF9A3F7U,0xC67178F2U};
        uint32_t words[64] = {};
        for (uint32_t i = 0; i < 16; ++i) words[i] = (static_cast<uint32_t>(block[i*4]) << 24) | (static_cast<uint32_t>(block[i*4+1]) << 16) | (static_cast<uint32_t>(block[i*4+2]) << 8) | block[i*4+3];
        for (uint32_t i = 16; i < 64; ++i) { uint32_t a=0,b=0,c=0; sha256_rotr(words[i-15],7,&a); sha256_rotr(words[i-15],18,&b); c=words[i-15]>>3; const uint32_t s0=a^b^c; sha256_rotr(words[i-2],17,&a); sha256_rotr(words[i-2],19,&b); c=words[i-2]>>10; words[i]=words[i-16]+s0+words[i-7]+(a^b^c); }
        uint32_t a=state[0],b=state[1],c=state[2],d=state[3],e=state[4],f=state[5],g=state[6],h=state[7];
        for (uint32_t i = 0; i < 64; ++i) { uint32_t r1=0,r2=0,r3=0; sha256_rotr(e,6,&r1); sha256_rotr(e,11,&r2); sha256_rotr(e,25,&r3); const uint32_t s1=r1^r2^r3; const uint32_t ch=(e&f)^((~e)&g); sha256_rotr(a,2,&r1); sha256_rotr(a,13,&r2); sha256_rotr(a,22,&r3); const uint32_t s0=r1^r2^r3; const uint32_t maj=(a&b)^(a&c)^(b&c); const uint32_t t1=h+s1+ch+k[i]+words[i],t2=s0+maj; h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2; }
        state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
    }
    uint32_t state[8]; uint64_t bitCount; uint8_t buffer[64]; uint32_t bufferBytes;
};

static bool validate_identity(Operation& operation) {
    vfs::FileInfo info = {};
    if (vfs::stat(operation.resolvedArtifact, &info) != vfs::VFS_OK || info.type != vfs::FILE_TYPE_REGULAR) {
        operation.error = GX_DEVELOPMENT_RUN_ERROR_ARTIFACT_MISSING;
        copy_text(operation.errorMessage, sizeof(operation.errorMessage), "Build artifact is missing");
        return false;
    }
    if (info.size != operation.artifactSize || info.size == 0 || info.size > sizeof(s_artifact)) {
        operation.error = GX_DEVELOPMENT_RUN_ERROR_ARTIFACT_SIZE_CHANGED;
        copy_text(operation.errorMessage, sizeof(operation.errorMessage), "Build artifact size differs from BuildResult");
        return false;
    }
    const uint32_t bytes = static_cast<uint32_t>(info.size);
    const int32_t read = vfs::read_file(operation.resolvedArtifact, s_artifact, bytes);
    if (read < 0 || static_cast<uint32_t>(read) != bytes) {
        operation.error = GX_DEVELOPMENT_RUN_ERROR_ARTIFACT_INVALID;
        copy_text(operation.errorMessage, sizeof(operation.errorMessage), "Build artifact could not be read");
        return false;
    }
    char actualHash[GX_DEVELOPMENT_RUN_MAX_SHA256_BYTES] = {};
    Sha256 hash;
    hash.update(s_artifact, bytes);
    hash.finish(actualHash);
    if (!equal_text(actualHash, operation.artifactSha256)) {
        operation.error = GX_DEVELOPMENT_RUN_ERROR_ARTIFACT_CHANGED;
        copy_text(operation.errorMessage, sizeof(operation.errorMessage), "Build artifact hash differs from BuildResult");
        return false;
    }
    NativeElfValidationResult validation = {};
    if (!validate_native_elf(s_artifact, bytes, default_validation_policy(), &validation) || validation.entryPoint == 0) {
        operation.error = GX_DEVELOPMENT_RUN_ERROR_ARTIFACT_INVALID;
        copy_text(operation.errorMessage, sizeof(operation.errorMessage), "Build artifact failed NativeElf validation");
        return false;
    }
    if (validation.entryLoadIndex >= validation.loadCount ||
        validation.loads[validation.entryLoadIndex].fileSize < compiler::BOOTSTRAP_CODE_OFFSET ||
        validation.loads[validation.entryLoadIndex].fileSize - compiler::BOOTSTRAP_CODE_OFFSET > 0xFFFFFFFFULL) {
        operation.error = GX_DEVELOPMENT_RUN_ERROR_ARTIFACT_INVALID;
        copy_text(operation.errorMessage, sizeof(operation.errorMessage),
                  "Build artifact has no bounded executable code range");
        return false;
    }
    operation.debugCodeFileOffset = compiler::BOOTSTRAP_CODE_OFFSET;
    operation.debugCodeBytes = static_cast<uint32_t>(
        validation.loads[validation.entryLoadIndex].fileSize - compiler::BOOTSTRAP_CODE_OFFSET);
    if (operation.debugSourceSelected) {
        compiler::ResolvedSourceMapping mapping = {};
        const char* mappingError = nullptr;
        if (!compiler::resolve_bootstrap_source_mapping(
                s_artifact, bytes, validation.imageBase,
                compiler::BOOTSTRAP_CODE_OFFSET,
                static_cast<uint32_t>(validation.loads[validation.entryLoadIndex].fileSize) -
                    compiler::BOOTSTRAP_CODE_OFFSET, operation.debugSourcePath,
                operation.debugSourceLine, operation.debugSourceColumn, &mapping,
                &mappingError)) {
            operation.error = GX_DEVELOPMENT_RUN_ERROR_ARTIFACT_INVALID;
            copy_text(operation.errorMessage, sizeof(operation.errorMessage),
                      mappingError ? mappingError : "Requested source line is not mapped");
            return false;
        }
        char sourcePath[kMaxPath] = {};
        if (!join_path(operation.projectRoot, operation.debugSourcePath,
                       sourcePath, sizeof(sourcePath)) ||
            !read_bounded(sourcePath, s_debugSource, sizeof(s_debugSource)) ||
            fnv1a_bytes(reinterpret_cast<const uint8_t*>(s_debugSource),
                        text_length(s_debugSource, sizeof(s_debugSource))) != mapping.sourceHash ||
            text_length(s_debugSource, sizeof(s_debugSource)) != mapping.sourceBytes) {
            operation.error = GX_DEVELOPMENT_RUN_ERROR_ARTIFACT_CHANGED;
            copy_text(operation.errorMessage, sizeof(operation.errorMessage),
                      "Source breakpoint file differs from the built source identity");
            return false;
        }
        operation.debugBreakpointAddress = mapping.targetAddress;
        operation.debugResolvedFinalCodeOffset = mapping.finalCodeOffset;
        operation.debugSourceInstructionBytes = mapping.instructionBytes;
        operation.debugCurrentSourceMappingValid = true;
        copy_text(operation.debugSourcePath, sizeof(operation.debugSourcePath), mapping.sourcePath);
        operation.debugSourceLine = mapping.line;
        operation.debugSourceColumn = mapping.column;
        copy_text(operation.debugFunctionName, sizeof(operation.debugFunctionName), mapping.functionName);
    }
    return true;
}

static bool validate_request(Operation& operation, const gx_development_run_request& request) {
    if (request.size < sizeof(gx_development_run_request) || request.version != GX_DEVELOPMENT_RUN_API_VERSION ||
        !request.projectRoot || !request.projectId || !request.projectKind || !request.targetProfile ||
        !request.manifestPath || !request.artifactPath || !request.artifactSha256 ||
        !request.artifactArchitecture || !request.artifactAbi) {
        operation.error = GX_DEVELOPMENT_RUN_ERROR_INVALID_REQUEST;
        copy_text(operation.errorMessage, sizeof(operation.errorMessage), "Bare-metal Run request is incomplete");
        return false;
    }
    if (!copy_text(operation.projectRoot, sizeof(operation.projectRoot), request.projectRoot) ||
        !copy_text(operation.projectId, sizeof(operation.projectId), request.projectId) ||
        !copy_text(operation.projectKind, sizeof(operation.projectKind), request.projectKind) ||
        !copy_text(operation.targetProfile, sizeof(operation.targetProfile), request.targetProfile) ||
        !copy_text(operation.manifestPath, sizeof(operation.manifestPath), request.manifestPath) ||
        !copy_text(operation.artifactPath, sizeof(operation.artifactPath), request.artifactPath) ||
        !copy_text(operation.artifactSha256, sizeof(operation.artifactSha256), request.artifactSha256) ||
        !copy_text(operation.artifactArchitecture, sizeof(operation.artifactArchitecture), request.artifactArchitecture) ||
        !copy_text(operation.artifactAbi, sizeof(operation.artifactAbi), request.artifactAbi)) {
        operation.error = GX_DEVELOPMENT_RUN_ERROR_INVALID_REQUEST;
        copy_text(operation.errorMessage, sizeof(operation.errorMessage), "Bare-metal Run request text is too long");
        return false;
    }
    operation.artifactSize = request.artifactSize;
    operation.debugSourceSelected = request.debugSourcePath != nullptr ||
        request.debugSourceLine != 0 || request.debugSourceColumn != 0;
    if (operation.debugSourceSelected) {
        if ((request.flags & GX_DEVELOPMENT_RUN_FLAG_DEBUG_CONTROLLED) == 0 ||
            !request.debugSourcePath || request.debugSourceLine == 0 ||
            !safe_relative(request.debugSourcePath) ||
            !copy_text(operation.debugSourcePath, sizeof(operation.debugSourcePath),
                       request.debugSourcePath)) {
            operation.error = GX_DEVELOPMENT_RUN_ERROR_INVALID_REQUEST;
            copy_text(operation.errorMessage, sizeof(operation.errorMessage),
                      "Source breakpoint selection is invalid");
            return false;
        }
        operation.debugSourceLine = request.debugSourceLine;
        operation.debugSourceColumn = request.debugSourceColumn;
    }
    if (!equal_text(operation.projectKind, kProjectKind) || !equal_text(operation.targetProfile, kTarget) ||
        !equal_text(operation.artifactArchitecture, kArchitecture) || !equal_text(operation.artifactAbi, kAbi) ||
        !equal_text(operation.manifestPath, kManifest) || text_length(operation.artifactSha256, sizeof(operation.artifactSha256)) != 64 ||
        operation.artifactSize == 0 || !safe_relative(operation.artifactPath) ||
        !starts_with(operation.projectId, "dev.guidexos.")) {
        operation.error = GX_DEVELOPMENT_RUN_ERROR_UNSUPPORTED_TARGET;
        copy_text(operation.errorMessage, sizeof(operation.errorMessage), "Bare-metal Run supports bootstrap NativeElf AMD64 projects only");
        return false;
    }
    if (!join_path(operation.projectRoot, operation.artifactPath,
                   operation.resolvedArtifact, sizeof(operation.resolvedArtifact))) {
        operation.error = GX_DEVELOPMENT_RUN_ERROR_ARTIFACT_INVALID;
        copy_text(operation.errorMessage, sizeof(operation.errorMessage), "Build artifact path is invalid");
        return false;
    }
    char projectPath[kMaxPath] = {};
    char manifestPath[kMaxPath] = {};
    if (!join_path(operation.projectRoot, "guidexos.project", projectPath, sizeof(projectPath)) ||
        !join_path(operation.projectRoot, operation.manifestPath, manifestPath, sizeof(manifestPath)) ||
        !read_bounded(projectPath, s_projectText, sizeof(s_projectText)) ||
        !read_bounded(manifestPath, s_manifestText, sizeof(s_manifestText))) {
        operation.error = GX_DEVELOPMENT_RUN_ERROR_PROJECT_INVALID;
        copy_text(operation.errorMessage, sizeof(operation.errorMessage), "Project metadata or manifest is unavailable");
        return false;
    }
    char value[256] = {};
    const uint32_t projectBytes = text_length(s_projectText, sizeof(s_projectText));
    if (!json_string(s_projectText, projectBytes, "projectId", value, sizeof(value)) || !equal_text(value, operation.projectId) ||
        !json_string(s_projectText, projectBytes, "projectKind", value, sizeof(value)) || !equal_text(value, kProjectKind) ||
        !json_string(s_projectText, projectBytes, "defaultTargetProfile", value, sizeof(value)) || !equal_text(value, kTarget) ||
        !json_string(s_projectText, projectBytes, "entryPoint", value, sizeof(value)) || !equal_text(value, "gx_main") ||
        !json_string(s_projectText, projectBytes, "abi", value, sizeof(value)) || !equal_text(value, kAbi) ||
        !json_string(s_projectText, projectBytes, "architecture", value, sizeof(value)) || !equal_text(value, kArchitecture) ||
        !json_string(s_projectText, projectBytes, "outputName", value, sizeof(value)) || !safe_name(value)) {
        operation.error = GX_DEVELOPMENT_RUN_ERROR_PROJECT_INVALID;
        copy_text(operation.errorMessage, sizeof(operation.errorMessage), "Project metadata is not a supported bootstrap target");
        return false;
    }
    char expected[GX_DEVELOPMENT_RUN_MAX_PATH_BYTES] = "build/bin/amd64/";
    uint32_t expectedLength = text_length(expected, sizeof(expected));
    if (!copy_text(expected + expectedLength, sizeof(expected) - expectedLength, value)) {
        operation.error = GX_DEVELOPMENT_RUN_ERROR_PROJECT_INVALID;
        return false;
    }
    expectedLength = text_length(expected, sizeof(expected));
    if (!copy_text(expected + expectedLength, sizeof(expected) - expectedLength, ".elf") || !equal_text(expected, operation.artifactPath)) {
        operation.error = GX_DEVELOPMENT_RUN_ERROR_MANIFEST_MISMATCH;
        copy_text(operation.errorMessage, sizeof(operation.errorMessage), "BuildResult artifact path does not match project output");
        return false;
    }
    const uint32_t manifestBytes = text_length(s_manifestText, sizeof(s_manifestText));
    if (!json_string(s_manifestText, manifestBytes, "id", value, sizeof(value)) || !equal_text(value, operation.projectId) ||
        !json_string(s_manifestText, manifestBytes, "displayName", operation.displayName, sizeof(operation.displayName)) ||
        !json_string(s_manifestText, manifestBytes, "path", value, sizeof(value)) ||
        !equal_text(value, operation.artifactPath + 6) ||
        !json_string(s_manifestText, manifestBytes, "entryPoint", value, sizeof(value)) || !equal_text(value, "gx_main") ||
        !json_string(s_manifestText, manifestBytes, "abi", value, sizeof(value)) || !equal_text(value, kAbi) ||
        !json_string(s_manifestText, manifestBytes, "runtime", value, sizeof(value)) || !equal_text(value, "native-elf")) {
        operation.error = GX_DEVELOPMENT_RUN_ERROR_MANIFEST_MISMATCH;
        copy_text(operation.errorMessage, sizeof(operation.errorMessage), "Application manifest does not match BuildResult");
        return false;
    }
    if (!validate_identity(operation)) return false;
    return copy_text(operation.applicationId, sizeof(operation.applicationId), operation.projectId);
}

static bool unregister_application(Operation& operation)
{
    if (!operation.appModelRegistered) {
        operation.cleanupComplete = true;
        return true;
    }
    const bool removed = NativeElfDevelopmentAppModel::unregister_temporary(
        operation.handle, operation.registrationGeneration, operation.applicationId);
    if (removed || !NativeElfDevelopmentAppModel::has_active_registration())
        operation.appModelRegistered = false;
    operation.cleanupComplete = !operation.appModelRegistered;
    return operation.cleanupComplete;
}

static void fail_and_cleanup(Operation& operation,
                             gx_development_run_error_code error,
                             const char* message)
{
    operation.state = GX_DEVELOPMENT_RUN_CLEANING_UP;
    operation.error = error;
    copy_text(operation.errorMessage, sizeof(operation.errorMessage), message);
    const bool registrationClean = unregister_application(operation);
    const bool runtimeClean = !operation.nativeRuntimeStarted || operation.report.teardownComplete;
    operation.report.teardownComplete = runtimeClean && registrationClean;
    operation.cleanupComplete = registrationClean && runtimeClean;
    operation.state = GX_DEVELOPMENT_RUN_FAILED;
}

static bool decode(gx_development_run_handle handle) {
    return s_operation.used && handle != 0 && handle == s_operation.handle;
}

static gx_development_run_error_code runtime_error_code(const NativeElfRunReport& report)
{
    return report.runtimeStatus == NativeRuntimeStatus::CallDepthExceeded
        ? GX_DEVELOPMENT_RUN_ERROR_CALL_DEPTH_EXCEEDED
        : (report.runtimeStatus == NativeRuntimeStatus::ArrayBoundsExceeded
            ? GX_DEVELOPMENT_RUN_ERROR_ARRAY_BOUNDS_EXCEEDED
            : (report.runtimeStatus == NativeRuntimeStatus::InvalidPointerDereference
                ? GX_DEVELOPMENT_RUN_ERROR_INVALID_POINTER_DEREFERENCE
                : (report.runtimeStatus == NativeRuntimeStatus::PointerOutOfBounds
                    ? GX_DEVELOPMENT_RUN_ERROR_POINTER_OUT_OF_BOUNDS
                    : GX_DEVELOPMENT_RUN_ERROR_LAUNCH_FAILED)));
}

static void finish_execution(Operation& operation, bool success)
{
    const bool debugClean = restore_debug_entry_breakpoint();
    operation.debugStepOverReturnBreakpointInstalled = false;
    operation.debugStepOverActive = false;
    operation.debugStepOutReturnBreakpointInstalled = false;
    operation.debugStepOutActive = false;
    if (operation.debugControlled) NativeElfDebugTrap::uninstall();
    clear_development_identity();
    if (!debugClean) {
        operation.report.teardownComplete = false;
        fail_and_cleanup(operation, GX_DEVELOPMENT_RUN_ERROR_INTERNAL,
                         "NativeElf debug breakpoint cleanup failed");
        return;
    }
    if (!success) {
        const gx_development_run_error_code error = runtime_error_code(operation.report);
        const char* message = operation.report.error
            ? operation.report.error : "NativeElf application launch failed";
        fail_and_cleanup(operation, error, message);
        return;
    }

    operation.state = GX_DEVELOPMENT_RUN_EXITED;
    operation.state = GX_DEVELOPMENT_RUN_CLEANING_UP;
    const bool registrationClean = unregister_application(operation);
    const bool runtimeClean = operation.report.teardownComplete;
    operation.cleanupComplete = registrationClean && runtimeClean;
    operation.report.teardownComplete = operation.cleanupComplete;
    if (!operation.cleanupComplete) {
        operation.error = GX_DEVELOPMENT_RUN_ERROR_INTERNAL;
        copy_text(operation.errorMessage, sizeof(operation.errorMessage),
                  "NativeElf run cleanup failed");
        operation.state = GX_DEVELOPMENT_RUN_FAILED;
        return;
    }

    operation.error = operation.cancellationRequested
        ? GX_DEVELOPMENT_RUN_ERROR_CANCELLED : GX_DEVELOPMENT_RUN_ERROR_NONE;
    operation.state = operation.cancellationRequested
        ? GX_DEVELOPMENT_RUN_CANCELLED : GX_DEVELOPMENT_RUN_COMPLETED;
}

#if defined(__x86_64__)
static void scheduled_task_entry(void*)
{
    s_schedulerInTarget = true;
    s_operation.nativeRuntimeStarted = true;
    int32_t exitCode = 0;
    const bool success = native_elf_execution_active()
        ? run_file_nested(s_operation.resolvedArtifact, &exitCode, &s_operation.report)
        : run_file(s_operation.resolvedArtifact, &exitCode, &s_operation.report);
    s_operation.exitCode = exitCode;
    finish_execution(s_operation, success);
    s_schedulerTargetComplete = true;
    s_schedulerInTarget = false;
    s_schedulerActive = false;
    arch::amd64::context::switch_context(&s_targetContext, s_ownerContext);
    for (;;) arch::amd64::halt();
}

static void scheduled_debug_cancel_entry()
{
    // The paused application stack is not a valid call stack for an
    // arbitrary return. Teardown therefore begins on the reserved scheduler
    // stack, then uses the normal completion path to report cancellation.
    const bool runtimeClean = abort_execution(&s_operation.report);
    finish_execution(s_operation, runtimeClean);
    s_schedulerTargetComplete = true;
    s_schedulerInTarget = false;
    s_schedulerActive = false;
    arch::amd64::context::switch_context(&s_targetContext, s_ownerContext);
    for (;;) arch::halt();
}

static SchedulerContext* make_debug_cancel_context()
{
    uint64_t stackTop = reinterpret_cast<uint64_t>(s_schedulerStack + sizeof(s_schedulerStack));
    stackTop &= ~0xFULL;
    stackTop -= sizeof(SchedulerContext) + sizeof(uint64_t);
    SchedulerContext* context = reinterpret_cast<SchedulerContext*>(stackTop);
    context->rbx = 0;
    context->rbp = 0;
    context->rdi = 0;
    context->rsi = 0;
    context->r12 = 0;
    context->r13 = 0;
    context->r14 = 0;
    context->r15 = 0;
    context->rsp = stackTop;
    context->rip = reinterpret_cast<uint64_t>(&scheduled_debug_cancel_entry);
    reinterpret_cast<uint64_t*>(stackTop)[sizeof(SchedulerContext) / sizeof(uint64_t)] =
        context->rip;
    return context;
}
#endif

} // namespace

bool native_elf_scheduler_in_target()
{
#if defined(__x86_64__)
    return s_schedulerInTarget;
#else
    return false;
#endif
}

bool native_elf_scheduler_yield()
{
#if defined(__x86_64__)
    if (!s_schedulerActive || !s_schedulerInTarget || !s_ownerContext || !s_targetContext)
        return false;
    s_schedulerInTarget = false;
    arch::amd64::context::switch_context(&s_targetContext, s_ownerContext);
    s_schedulerInTarget = true;
    return true;
#else
    return false;
#endif
}

bool native_elf_scheduler_pump()
{
#if defined(__x86_64__)
    if (!s_schedulerActive || s_schedulerTargetComplete || s_schedulerInTarget ||
        !s_targetContext) return false;
    // The owner context is a continuation on this individual pump call's
    // stack.  Start() must not leave a later desktop tick resuming through a
    // dead start() frame, so every owner-side pump captures a fresh context.
    s_ownerContext = nullptr;
    s_schedulerInTarget = true;
    arch::amd64::context::switch_context(&s_ownerContext, s_targetContext);
    s_schedulerInTarget = false;
    return true;
#else
    return false;
#endif
}

gx_result prepare(const gx_development_run_request& request,
                  gx_development_run_handle* outHandle,
                  gx_development_run_snapshot* outSnapshot) {
    if (!outHandle || !outSnapshot || snapshot_capacity(outSnapshot) < kLegacySnapshotBytes) return GX_ERROR_INVALID_ARGUMENT;
    *outHandle = 0;
    clear_snapshot(outSnapshot);
    if (s_operation.used) {
        set_failure(outSnapshot, GX_DEVELOPMENT_RUN_ERROR_RUNTIME_BUSY, "Bare-metal NativeElf runtime is busy");
        return GX_OK;
    }
    s_operation = Operation();
    s_operation.used = true;
    s_operation.handle = s_nextHandle++;
    if (s_operation.handle == 0) s_operation.handle = s_nextHandle++;
    s_operation.state = GX_DEVELOPMENT_RUN_VALIDATING;
    s_operation.error = GX_DEVELOPMENT_RUN_ERROR_NONE;
    if (!validate_request(s_operation, request)) {
        s_operation.state = GX_DEVELOPMENT_RUN_FAILED;
        s_operation.cleanupComplete = true;
        s_operation.report.teardownComplete = true;
        snapshot_operation(s_operation, outSnapshot);
        s_operation = Operation();
        return GX_OK;
    }
    s_operation.debugControlled =
        (request.flags & GX_DEVELOPMENT_RUN_FLAG_DEBUG_CONTROLLED) != 0;
    if (app::AppManager::isAppAvailable(s_operation.applicationId)) {
        s_operation.state = GX_DEVELOPMENT_RUN_FAILED;
        s_operation.error = GX_DEVELOPMENT_RUN_ERROR_APPLICATION_ID_INSTALLED;
        copy_text(s_operation.errorMessage, sizeof(s_operation.errorMessage),
                  "Development identity collides with a registered kernel application");
        s_operation.cleanupComplete = true;
        s_operation.report.teardownComplete = true;
        snapshot_operation(s_operation, outSnapshot);
        s_operation = Operation();
        return GX_OK;
    }
    NativeElfDevelopmentAppModel::RegistrationRequest registrationRequest = {};
    registrationRequest.handle = s_operation.handle;
    registrationRequest.generation = s_operation.handle;
    registrationRequest.applicationId = s_operation.applicationId;
    registrationRequest.displayName = s_operation.displayName;
    registrationRequest.projectRoot = s_operation.projectRoot;
    registrationRequest.artifactPath = s_operation.artifactPath;
    registrationRequest.artifactSize = s_operation.artifactSize;
    registrationRequest.artifactSha256 = s_operation.artifactSha256;
    NativeElfDevelopmentAppModel::Registration registration = {};
    const NativeElfDevelopmentAppModel::RegistrationResult registrationResult =
        NativeElfDevelopmentAppModel::register_temporary(registrationRequest, &registration);
    if (registrationResult != NativeElfDevelopmentAppModel::RegistrationResult::Registered) {
        s_operation.state = GX_DEVELOPMENT_RUN_FAILED;
        s_operation.error = registrationResult == NativeElfDevelopmentAppModel::RegistrationResult::DeploymentAlreadyActive
            ? GX_DEVELOPMENT_RUN_ERROR_DEPLOYMENT_ALREADY_ACTIVE
            : (registrationResult == NativeElfDevelopmentAppModel::RegistrationResult::ApplicationIdInUse
                ? GX_DEVELOPMENT_RUN_ERROR_APPLICATION_ID_IN_USE
                : GX_DEVELOPMENT_RUN_ERROR_INTERNAL);
        copy_text(s_operation.errorMessage, sizeof(s_operation.errorMessage),
                  "Temporary NativeElf App Model registration failed");
        s_operation.cleanupComplete = !NativeElfDevelopmentAppModel::has_active_registration();
        s_operation.report.teardownComplete = s_operation.cleanupComplete;
        snapshot_operation(s_operation, outSnapshot);
        s_operation = Operation();
        return GX_OK;
    }
    s_operation.appModelRegistered = true;
    s_operation.registrationGeneration = registration.generation;
    s_operation.cleanupComplete = false;
    s_operation.state = GX_DEVELOPMENT_RUN_REGISTERED;
    *outHandle = s_operation.handle;
    snapshot_operation(s_operation, outSnapshot);
    return GX_OK;
}

gx_result start(gx_development_run_handle handle) {
    if (!decode(handle)) return GX_ERROR_FAILED;
    if (s_operation.state != GX_DEVELOPMENT_RUN_REGISTERED) return GX_ERROR_BUSY;
    if (s_operation.closeRequested) {
        fail_and_cleanup(s_operation, GX_DEVELOPMENT_RUN_ERROR_CANCELLED,
                         "Bare-metal Run was cancelled before start");
        return GX_OK;
    }
    NativeElfDevelopmentAppModel::Registration registration = {};
    if (!NativeElfDevelopmentAppModel::resolve_temporary(
            s_operation.handle, s_operation.registrationGeneration,
            s_operation.applicationId, &registration) ||
        !equal_text(registration.artifactPath, s_operation.artifactPath) ||
        registration.artifactSize != s_operation.artifactSize ||
        !equal_text(registration.artifactSha256, s_operation.artifactSha256)) {
        fail_and_cleanup(s_operation, GX_DEVELOPMENT_RUN_ERROR_STALE_DEPLOYMENT,
                         "Temporary NativeElf deployment is stale");
        return GX_OK;
    }
    // Prepare records the exact BuildResult identity, but the file can still
    // be replaced before Start. Revalidate immediately before launch so a
    // stale or tampered artifact is never executed.
    if (!validate_identity(s_operation)) {
        const gx_development_run_error_code error = s_operation.error;
        char message[GX_DEVELOPMENT_RUN_MAX_ERROR_BYTES] = {};
        copy_text(message, sizeof(message), s_operation.errorMessage);
        fail_and_cleanup(s_operation, error, message);
        return GX_OK;
    }
    s_operation.state = GX_DEVELOPMENT_RUN_LAUNCHING;
    if (!configure_development_identity(s_operation.registrationGeneration,
                                        s_operation.applicationId)) {
        fail_and_cleanup(s_operation, GX_DEVELOPMENT_RUN_ERROR_INTERNAL,
                         "NativeElf development identity could not be bound");
        return GX_OK;
    }
    s_operation.state = GX_DEVELOPMENT_RUN_RUNNING;
    s_operation.report = NativeElfRunReport();
    s_operation.exitCode = 0;

#if defined(__x86_64__)
    // The existing AMD64 context-switch primitive supplies one cooperative
    // execution owner.  Start gives that owner exactly one bounded slice; the
    // target returns through native_elf_scheduler_yield at the next desktop
    // pump, so this call never owns the target lifetime synchronously.
    s_schedulerActive = true;
    s_schedulerInTarget = false;
    s_schedulerTargetComplete = false;
    s_ownerContext = nullptr;
    s_targetContext = arch::amd64::context::init_context(
        reinterpret_cast<uint64_t>(s_schedulerStack + sizeof(s_schedulerStack)),
        &scheduled_task_entry,
        &s_operation);
    if (!s_targetContext) {
        s_schedulerActive = false;
        fail_and_cleanup(s_operation, GX_DEVELOPMENT_RUN_ERROR_LAUNCH_UNAVAILABLE,
                         "NativeElf execution context could not be created");
        return GX_OK;
    }
    if (s_operation.debugControlled &&
        !NativeElfDebugTrap::install(native_elf_debug_breakpoint_exception,
                                     native_elf_debug_single_step_exception)) {
        native_elf_debug_breakpoint_install_failed();
        s_schedulerActive = false;
        s_targetContext = nullptr;
        fail_and_cleanup(s_operation, GX_DEVELOPMENT_RUN_ERROR_LAUNCH_UNAVAILABLE,
                         "NativeElf debug trap could not be installed");
        return GX_OK;
    }
    if (!native_elf_scheduler_pump()) {
        s_schedulerActive = false;
        s_targetContext = nullptr;
        fail_and_cleanup(s_operation, GX_DEVELOPMENT_RUN_ERROR_LAUNCH_UNAVAILABLE,
                         "NativeElf execution owner could not be scheduled");
    }
#else
    fail_and_cleanup(s_operation,
                     s_operation.debugControlled
                         ? GX_DEVELOPMENT_RUN_ERROR_UNSUPPORTED_TARGET
                         : GX_DEVELOPMENT_RUN_ERROR_LAUNCH_UNAVAILABLE,
                     s_operation.debugControlled
                         ? "NativeElf entry debugging is unavailable on this architecture"
                         : "Asynchronous NativeElf ownership is unavailable on this architecture");
#endif
    return GX_OK;
}

gx_result pump(gx_development_run_handle handle) {
    if (!decode(handle)) return GX_ERROR_FAILED;
    if (s_operation.state == GX_DEVELOPMENT_RUN_COMPLETED ||
        s_operation.state == GX_DEVELOPMENT_RUN_FAILED ||
        s_operation.state == GX_DEVELOPMENT_RUN_CANCELLED) return GX_OK;
    if (s_operation.state != GX_DEVELOPMENT_RUN_RUNNING &&
        s_operation.state != GX_DEVELOPMENT_RUN_CLOSING &&
        s_operation.state != GX_DEVELOPMENT_RUN_LAUNCHING) return GX_ERROR_BUSY;
    (void)native_elf_scheduler_pump();
    return GX_OK;
}

gx_result poll(gx_development_run_handle handle, gx_development_run_snapshot* outSnapshot) {
    if (!outSnapshot || snapshot_capacity(outSnapshot) < kLegacySnapshotBytes) return GX_ERROR_INVALID_ARGUMENT;
    if (!decode(handle)) return GX_ERROR_FAILED;
    snapshot_operation(s_operation, outSnapshot);
    return GX_OK;
}

gx_result request_close(gx_development_run_handle handle) {
    if (!decode(handle)) return GX_ERROR_FAILED;
    if (s_operation.state == GX_DEVELOPMENT_RUN_COMPLETED ||
        s_operation.state == GX_DEVELOPMENT_RUN_FAILED ||
        s_operation.state == GX_DEVELOPMENT_RUN_CANCELLED) return GX_OK;
    if (s_operation.state == GX_DEVELOPMENT_RUN_REGISTERED) {
        s_operation.closeRequested = true;
        fail_and_cleanup(s_operation, GX_DEVELOPMENT_RUN_ERROR_CANCELLED,
                         "Bare-metal Run cancelled before start");
        s_operation.state = GX_DEVELOPMENT_RUN_CANCELLED;
        return GX_OK;
    }
    if (s_operation.state == GX_DEVELOPMENT_RUN_CLOSING) return GX_OK;
    if (s_operation.state != GX_DEVELOPMENT_RUN_RUNNING) return GX_ERROR_BUSY;

    s_operation.closeRequested = true;
    s_operation.state = GX_DEVELOPMENT_RUN_CLOSING;
    if (!request_native_elf_gui_close(s_operation.registrationGeneration)) {
        s_operation.state = GX_DEVELOPMENT_RUN_RUNNING;
        s_operation.closeRequested = false;
        return GX_ERROR_UNSUPPORTED;
    }
    (void)native_elf_scheduler_pump();
    return GX_OK;
}

gx_result cancel(gx_development_run_handle handle) {
    if (!decode(handle)) return GX_ERROR_FAILED;
    if (s_operation.state == GX_DEVELOPMENT_RUN_COMPLETED ||
        s_operation.state == GX_DEVELOPMENT_RUN_FAILED ||
        s_operation.state == GX_DEVELOPMENT_RUN_CANCELLED) return GX_OK;
    if (s_operation.state == GX_DEVELOPMENT_RUN_REGISTERED) {
        s_operation.cancellationRequested = true;
        fail_and_cleanup(s_operation, GX_DEVELOPMENT_RUN_ERROR_CANCELLED,
                         "Bare-metal Run cancelled before start");
        s_operation.state = GX_DEVELOPMENT_RUN_CANCELLED;
        return GX_OK;
    }
    if (s_operation.state == GX_DEVELOPMENT_RUN_CLOSING) return GX_OK;
    if (s_operation.state != GX_DEVELOPMENT_RUN_RUNNING) return GX_ERROR_BUSY;

    s_operation.cancellationRequested = true;
    s_operation.state = GX_DEVELOPMENT_RUN_CLOSING;
    // Safe cancellation is deliberately cooperative: only a suspended GUI
    // target can be closed without destroying an arbitrary NativeElf stack.
    if (!request_native_elf_gui_close(s_operation.registrationGeneration)) {
        s_operation.state = GX_DEVELOPMENT_RUN_RUNNING;
        s_operation.cancellationRequested = false;
        return GX_ERROR_UNSUPPORTED;
    }
    (void)native_elf_scheduler_pump();
    return GX_OK;
}

bool native_elf_debug_entry_breakpoint_requested()
{
    return s_operation.used && s_operation.debugControlled &&
        s_operation.state != GX_DEVELOPMENT_RUN_COMPLETED &&
        s_operation.state != GX_DEVELOPMENT_RUN_FAILED &&
        s_operation.state != GX_DEVELOPMENT_RUN_CANCELLED;
}

bool native_elf_debug_breakpoint_target(uint64_t* targetAddress)
{
    if (targetAddress) *targetAddress = 0;
    if (!s_operation.used || !s_operation.debugControlled ||
        s_operation.state == GX_DEVELOPMENT_RUN_COMPLETED ||
        s_operation.state == GX_DEVELOPMENT_RUN_FAILED ||
        s_operation.state == GX_DEVELOPMENT_RUN_CANCELLED) return false;
    if (targetAddress && s_operation.debugSourceSelected)
        *targetAddress = s_operation.debugBreakpointAddress;
    return true;
}

bool native_elf_debug_breakpoint_installed(uint64_t targetAddress,
                                           uint8_t originalByte)
{
    if (!s_operation.used || !s_operation.debugControlled ||
        s_operation.state != GX_DEVELOPMENT_RUN_RUNNING ||
        !debug_entry_breakpoint_installed() || targetAddress == 0 ||
        (s_operation.debugSourceSelected &&
         targetAddress != s_operation.debugBreakpointAddress)) return false;
    s_operation.debugBreakpointInstalled = true;
    s_operation.debugBreakpointAddress = targetAddress;
    s_operation.debugBreakpointOriginalByte = originalByte;
    serial_debug_hex(s_operation.debugSourceSelected
                         ? "DEVELOPER_STUDIO_PHASE28A_BREAKPOINT_INSTALLED target=0x"
                         : "DEVELOPER_STUDIO_PHASE27Z_BREAKPOINT_INSTALLED target=0x",
                     targetAddress);
    serial::puts(" function=");
    serial::puts(s_operation.debugSourceSelected ? s_operation.debugFunctionName : "gx_main");
    serial::puts(" original=0x");
    serial::put_hex32(originalByte);
    serial::putc('\n');
    return true;
}

void native_elf_debug_breakpoint_install_failed()
{
    s_operation.debugBreakpointInstalled = false;
    s_operation.debugBreakpointHit = false;
    s_operation.debugBreakpointAddress = 0;
    s_operation.debugBreakpointOriginalByte = 0;
    if (s_operation.used && s_operation.debugControlled)
        serial::puts(s_operation.debugSourceSelected
                         ? "DEVELOPER_STUDIO_PHASE28A_BREAKPOINT_INSTALL_FAIL\n"
                         : "DEVELOPER_STUDIO_PHASE27Z_BREAKPOINT_INSTALL_FAIL\n");
}

static bool handle_step_out_return_trap(
    NativeElfDebugTrap::BreakpointContext* context,
    const NativeAppExecutionContext* runtime)
{
#if defined(__x86_64__)
    const bool valid = context && s_operation.used && s_operation.debugControlled &&
        s_operation.debugStepOutActive &&
        s_operation.debugStepOutReturnBreakpointInstalled &&
        !s_operation.debugStepOutReturnBreakpointHit &&
        s_operation.state == GX_DEVELOPMENT_RUN_STEPPING && s_schedulerActive &&
        s_schedulerInTarget && s_ownerContext && s_targetContext && context->cs == 0x08 &&
        context->rip == s_operation.debugStepOutReturnAddress + 1ULL && runtime &&
        runtime->state == NativeAppExecutionState::Running &&
        debug_code_address(s_operation, s_operation.debugStepOutReturnAddress) &&
        runtime->stackBase <= ~static_cast<uint64_t>(0) - runtime->stackSize &&
        step_out_stack_range_contains(runtime->stackBase,
                                      runtime->stackBase + runtime->stackSize,
                                      context->rsp, 1);
    if (!valid) return false;

    const uint64_t rawTrapRip = context->rip;
    if (!restore_debug_entry_breakpoint()) return false;
    s_operation.debugStepOutReturnBreakpointInstalled = false;
    s_operation.debugStepOutReturnBreakpointHit = true;
    s_operation.debugStepOutReturnTrapRip = rawTrapRip;
    s_operation.debugStepOutFinalRip = s_operation.debugStepOutReturnAddress;
    s_operation.debugStepOutFinalRsp = context->rsp;
    s_operation.debugStepOutFinalRbp = context->rbp;
    s_operation.debugStepOutRemainingCalleeExecuted = 1;
    s_operation.debugContext = context;
    s_operation.debugStepActive = false;
    s_operation.debugStepTrapObserved = true;
    ++s_operation.debugStopGeneration;
    if (s_operation.debugStopGeneration == 0) s_operation.debugStopGeneration = 1;
    s_operation.state = GX_DEVELOPMENT_RUN_PAUSED;
    context->rip = s_operation.debugStepOutReturnAddress;

    gx_development_debug_snapshot& snapshot = s_operation.debugSnapshot;
    clear_debug_snapshot(&snapshot);
    snapshot.status = GX_DEVELOPMENT_DEBUG_STATUS_TRAP;
    snapshot.trapKind = GX_DEVELOPMENT_DEBUG_TRAP_BREAKPOINT;
    snapshot.internalBreakpointTrap = 1;
    snapshot.internalBreakpointId = s_operation.debugStepOutToken;
    snapshot.internalBreakpointPurpose = GX_DEVELOPMENT_DEBUG_INTERNAL_BREAKPOINT_STEP_OUT;
    set_debug_identity(s_operation, &snapshot);
    snapshot.targetAddress = s_operation.debugStepOutReturnAddress;
    snapshot.instructionPointer = s_operation.debugStepOutReturnAddress;
    snapshot.rawTrapRip = rawTrapRip;
    snapshot.pauseReason = GX_DEVELOPMENT_DEBUG_PAUSE_REASON_NONE;
    snapshot.stackLow = runtime->stackBase;
    snapshot.stackHigh = runtime->stackBase <=
        ~static_cast<uint64_t>(0) - runtime->stackSize
            ? runtime->stackBase + runtime->stackSize : 0;
    snapshot.context.architecture = GX_DEVELOPMENT_DEBUG_ARCHITECTURE_AMD64;
    snapshot.context.valid = 1;
    snapshot.context.nativeRuntimeId = s_operation.registrationGeneration;
    snapshot.context.threadId = 1;
    snapshot.context.sessionGeneration = s_operation.registrationGeneration;
    snapshot.context.stopGeneration = s_operation.debugStopGeneration;
    snapshot.context.rip = s_operation.debugStepOutReturnAddress;
    snapshot.context.rflags = context->rflags;
    snapshot.context.rsp = context->rsp;
    snapshot.context.rbp = context->rbp;
    snapshot.context.rax = context->rax;
    snapshot.context.rbx = context->rbx;
    snapshot.context.rcx = context->rcx;
    snapshot.context.rdx = context->rdx;
    snapshot.context.rsi = context->rsi;
    snapshot.context.rdi = context->rdi;
    snapshot.context.r8 = context->r8;
    snapshot.context.r9 = context->r9;
    snapshot.context.r10 = context->r10;
    snapshot.context.r11 = context->r11;
    snapshot.context.r12 = context->r12;
    snapshot.context.r13 = context->r13;
    snapshot.context.r14 = context->r14;
    snapshot.context.r15 = context->r15;
    snapshot.sourceStepOutOriginalReturnByte = s_operation.debugStepOutOriginalReturnByte;
    snapshot.sourceStepOutOriginalReturnByteValid =
        s_operation.debugStepOutOriginalReturnByteValid;
    set_source_step_metadata(s_operation, &snapshot);
    set_source_step_out_metadata(s_operation, &snapshot);
    serial_debug_hex("DEVELOPER_STUDIO_PHASE28E_RETURN_TRAP rip=0x", rawTrapRip);
    serial::puts(" return=0x"); serial::put_hex64(s_operation.debugStepOutReturnAddress);
    serial::puts(" rbp=0x"); serial::put_hex64(context->rbp);
    serial::puts(" rsp=0x"); serial::put_hex64(context->rsp); serial::putc('\n');
    serial::puts("DEVELOPER_STUDIO_PHASE28E_RETURN_TRAP_PASS\n");
    if (!native_elf_scheduler_yield()) return false;
    if (s_operation.debugCancelRequested) {
        context->rflags &= ~kAmd64TrapFlag;
        context->rip = reinterpret_cast<uint64_t>(&native_elf_debug_cancel_return);
    } else if (s_operation.debugStepActive) {
        context->rflags |= kAmd64TrapFlag;
    } else {
        context->rflags &= ~kAmd64TrapFlag;
    }
    return true;
#else
    (void)context;
    (void)runtime;
    return false;
#endif
}

bool native_elf_debug_breakpoint_exception(
    NativeElfDebugTrap::BreakpointContext* context)
{
#if defined(__x86_64__)
    const NativeAppExecutionContext* runtime = native_elf_runtime_context();
    if (handle_step_out_return_trap(context, runtime)) return true;
    const bool stepOverReturnTrap = context && s_operation.used && s_operation.debugControlled &&
        s_operation.debugStepOverActive && s_operation.debugStepOverReturnBreakpointInstalled &&
        !s_operation.debugStepOverReturnBreakpointHit &&
        s_operation.state == GX_DEVELOPMENT_RUN_STEPPING && s_schedulerActive &&
        s_schedulerInTarget && s_ownerContext && s_targetContext && context->cs == 0x08 &&
        context->rip == s_operation.debugStepOverReturnAddress + 1ULL && runtime &&
        runtime->state == NativeAppExecutionState::Running &&
        debug_code_address(s_operation, s_operation.debugStepOverReturnAddress) &&
        context->rsp >= runtime->stackBase &&
        context->rsp < runtime->stackBase + runtime->stackSize;
    if (stepOverReturnTrap) {
        const uint64_t rawTrapRip = context->rip;
        if (!restore_debug_entry_breakpoint()) return false;
        s_operation.debugStepOverReturnBreakpointInstalled = false;
        s_operation.debugStepOverReturnBreakpointHit = true;
        s_operation.debugStepOverReturnTrapRip = rawTrapRip;
        s_operation.debugContext = context;
        s_operation.debugStepActive = false;
        s_operation.debugStepTrapObserved = true;
        ++s_operation.debugStopGeneration;
        if (s_operation.debugStopGeneration == 0) s_operation.debugStopGeneration = 1;
        s_operation.state = GX_DEVELOPMENT_RUN_PAUSED;
        context->rip = s_operation.debugStepOverReturnAddress;

        gx_development_debug_snapshot& snapshot = s_operation.debugSnapshot;
        clear_debug_snapshot(&snapshot);
        snapshot.status = GX_DEVELOPMENT_DEBUG_STATUS_TRAP;
        snapshot.trapKind = GX_DEVELOPMENT_DEBUG_TRAP_BREAKPOINT;
        snapshot.internalBreakpointTrap = 1;
        snapshot.internalBreakpointId = s_operation.debugStepOverToken;
        snapshot.internalBreakpointPurpose = GX_DEVELOPMENT_DEBUG_INTERNAL_BREAKPOINT_STEP_OVER;
        set_debug_identity(s_operation, &snapshot);
        snapshot.targetAddress = s_operation.debugStepOverReturnAddress;
        snapshot.instructionPointer = s_operation.debugStepOverReturnAddress;
        snapshot.rawTrapRip = rawTrapRip;
        snapshot.internalBreakpointTrap = 1;
        snapshot.pauseReason = GX_DEVELOPMENT_DEBUG_PAUSE_REASON_NONE;
        snapshot.stackLow = runtime->stackBase;
        snapshot.stackHigh = runtime->stackBase <= ~static_cast<uint64_t>(0) - runtime->stackSize
            ? runtime->stackBase + runtime->stackSize : 0;
        snapshot.context.architecture = GX_DEVELOPMENT_DEBUG_ARCHITECTURE_AMD64;
        snapshot.context.valid = 1;
        snapshot.context.nativeRuntimeId = s_operation.registrationGeneration;
        snapshot.context.threadId = 1;
        snapshot.context.sessionGeneration = s_operation.registrationGeneration;
        snapshot.context.stopGeneration = s_operation.debugStopGeneration;
        snapshot.context.rip = s_operation.debugStepOverReturnAddress;
        snapshot.context.rflags = context->rflags;
        snapshot.context.rsp = context->rsp;
        snapshot.context.rbp = context->rbp;
        snapshot.context.rax = context->rax;
        snapshot.context.rbx = context->rbx;
        snapshot.context.rcx = context->rcx;
        snapshot.context.rdx = context->rdx;
        snapshot.context.rsi = context->rsi;
        snapshot.context.rdi = context->rdi;
        snapshot.context.r8 = context->r8;
        snapshot.context.r9 = context->r9;
        snapshot.context.r10 = context->r10;
        snapshot.context.r11 = context->r11;
        snapshot.context.r12 = context->r12;
        snapshot.context.r13 = context->r13;
        snapshot.context.r14 = context->r14;
        snapshot.context.r15 = context->r15;
        snapshot.sourceStepOverOriginalReturnByte = s_operation.debugStepOverOriginalReturnByte;
        snapshot.sourceStepOverOriginalReturnByteValid =
            s_operation.debugStepOverOriginalReturnByteValid;
        set_source_step_metadata(s_operation, &snapshot);
        set_source_step_over_metadata(s_operation, &snapshot);
        serial_debug_hex("DEVELOPER_STUDIO_PHASE28D_RETURN_TRAP rip=0x", rawTrapRip);
        serial::puts(" return=0x"); serial::put_hex64(s_operation.debugStepOverReturnAddress);
        serial::puts(" rbp=0x"); serial::put_hex64(context->rbp);
        serial::puts(" rsp=0x"); serial::put_hex64(context->rsp); serial::putc('\n');
        if (!native_elf_scheduler_yield()) return false;
        if (s_operation.debugCancelRequested) {
            context->rflags &= ~kAmd64TrapFlag;
            context->rip = reinterpret_cast<uint64_t>(&native_elf_debug_cancel_return);
        } else if (s_operation.debugStepActive) {
            context->rflags |= kAmd64TrapFlag;
        } else {
            context->rflags &= ~kAmd64TrapFlag;
        }
        return true;
    }
    if (!context || !s_operation.used || !s_operation.debugControlled ||
        !s_operation.debugBreakpointInstalled || s_operation.debugBreakpointHit ||
        s_operation.state != GX_DEVELOPMENT_RUN_RUNNING || !s_schedulerActive ||
        !s_schedulerInTarget || !s_ownerContext || !s_targetContext ||
        context->cs != 0x08 || context->rip != s_operation.debugBreakpointAddress + 1ULL) {
        return false;
    }
    runtime = native_elf_runtime_context();
    if (!runtime || runtime->state != NativeAppExecutionState::Running ||
        !native_app_pointer_in_range(s_operation.debugBreakpointAddress, runtime->imageBase,
                                     runtime->imageSize)) return false;

    ++s_operation.debugStopGeneration;
    if (s_operation.debugStopGeneration == 0) s_operation.debugStopGeneration = 1;
    s_operation.debugBreakpointHit = true;
    s_operation.debugContext = context;
    s_operation.debugStepActive = false;
    s_operation.debugStepTrapObserved = false;
    capture_debug_start_bytes(s_operation, *runtime,
                              s_operation.debugBreakpointOriginalByte);
    s_operation.state = GX_DEVELOPMENT_RUN_PAUSED;
    gx_development_debug_snapshot& snapshot = s_operation.debugSnapshot;
    clear_debug_snapshot(&snapshot);
    snapshot.status = GX_DEVELOPMENT_DEBUG_STATUS_TRAP;
    snapshot.trapKind = GX_DEVELOPMENT_DEBUG_TRAP_BREAKPOINT;
    set_debug_identity(s_operation, &snapshot);
    snapshot.instructionPointer = s_operation.debugBreakpointAddress;
    snapshot.rawTrapRip = context->rip;
    snapshot.pauseReason = s_operation.debugSourceSelected
        ? GX_DEVELOPMENT_DEBUG_PAUSE_REASON_SOURCE_BREAKPOINT
        : GX_DEVELOPMENT_DEBUG_PAUSE_REASON_ENTRY_BREAKPOINT;
    snapshot.stackLow = runtime->stackBase;
    snapshot.stackHigh = runtime->stackBase <= ~static_cast<uint64_t>(0) - runtime->stackSize
        ? runtime->stackBase + runtime->stackSize : 0;
    snapshot.context.architecture = GX_DEVELOPMENT_DEBUG_ARCHITECTURE_AMD64;
    snapshot.context.valid = 1;
    snapshot.context.processId = 0;
    snapshot.context.nativeRuntimeId = s_operation.registrationGeneration;
    snapshot.context.threadId = 1;
    snapshot.context.sessionGeneration = s_operation.registrationGeneration;
    snapshot.context.stopGeneration = s_operation.debugStopGeneration;
    snapshot.context.rip = s_operation.debugBreakpointAddress;
    snapshot.context.rflags = context->rflags;
    snapshot.context.rsp = context->rsp;
    snapshot.context.rbp = context->rbp;
    snapshot.context.rax = context->rax;
    snapshot.context.rbx = context->rbx;
    snapshot.context.rcx = context->rcx;
    snapshot.context.rdx = context->rdx;
    snapshot.context.rsi = context->rsi;
    snapshot.context.rdi = context->rdi;
    snapshot.context.r8 = context->r8;
    snapshot.context.r9 = context->r9;
    snapshot.context.r10 = context->r10;
    snapshot.context.r11 = context->r11;
    snapshot.context.r12 = context->r12;
    snapshot.context.r13 = context->r13;
    snapshot.context.r14 = context->r14;
    snapshot.context.r15 = context->r15;
    copy_debug_start_bytes(s_operation, &snapshot);
    snapshot.rflagsAfterTrapFlagClear = context->rflags & ~kAmd64TrapFlag;
    serial_debug_hex(s_operation.debugSourceSelected
                         ? "DEVELOPER_STUDIO_PHASE28A_BREAKPOINT_HIT target=0x"
                         : "DEVELOPER_STUDIO_PHASE27Z_BREAKPOINT_HIT target=0x",
                     s_operation.debugBreakpointAddress);
    serial_debug_hex(" raw_rip=0x", context->rip);
    serial::puts(" function=");
    serial::puts(s_operation.debugSourceSelected ? s_operation.debugFunctionName : "gx_main");
    serial::puts(" stop=");
    serial::put_hex64(s_operation.debugStopGeneration);
    serial::putc('\n');
    serial::puts(s_operation.debugSourceSelected
                     ? "DEVELOPER_STUDIO_PHASE28A_PAUSED\n"
                     : "DEVELOPER_STUDIO_PHASE27Z_PAUSED\n");

    if (!native_elf_scheduler_yield()) return false;
    context->rip = s_operation.debugCancelRequested
        ? reinterpret_cast<uint64_t>(&native_elf_debug_cancel_return)
        : s_operation.debugBreakpointAddress;
    return true;
#else
    (void)context;
    return false;
#endif
}

bool native_elf_debug_single_step_exception(
    NativeElfDebugTrap::BreakpointContext* context)
{
#if defined(__x86_64__)
    const NativeAppExecutionContext* runtime = native_elf_runtime_context();
    const bool runtimeBoundaryTrap = context && s_operation.debugSourceStepActive && runtime &&
        !native_app_pointer_in_range(context->rip, runtime->imageBase, runtime->imageSize);
    const bool validProvenance = context && s_operation.used && s_operation.debugControlled &&
        s_operation.debugStepActive && !s_operation.debugStepTrapObserved &&
        s_operation.state == GX_DEVELOPMENT_RUN_STEPPING && s_schedulerActive &&
        s_schedulerInTarget && s_ownerContext && s_targetContext && context->cs == 0x08 &&
        (context->rflags & kAmd64TrapFlag) != 0 && s_operation.debugStepToken != 0 &&
        runtime && runtime->state == NativeAppExecutionState::Running &&
        (native_app_pointer_in_range(context->rip, runtime->imageBase, runtime->imageSize) ||
         runtimeBoundaryTrap) &&
        context->rsp >= runtime->stackBase &&
        context->rsp < runtime->stackBase + runtime->stackSize;
    if (!validProvenance) {
        serial::puts("DEVELOPER_STUDIO_PHASE28B_VECTOR1_REJECT state=");
        serial::put_hex32(static_cast<uint32_t>(s_operation.state));
        serial::puts(" active="); serial::put_hex32(s_operation.debugStepActive ? 1U : 0U);
        serial::puts(" in_target="); serial::put_hex32(s_schedulerInTarget ? 1U : 0U);
        serial::puts(" cs="); serial::put_hex64(context ? context->cs : 0);
        serial::puts(" rip=0x="); serial::put_hex64(context ? context->rip : 0);
        serial::puts(" rsp=0x"); serial::put_hex64(context ? context->rsp : 0);
        serial::puts(" tf="); serial::put_hex32(context && (context->rflags & kAmd64TrapFlag) ? 1U : 0U);
        serial::puts(" runtime="); serial::put_hex32(runtime ? static_cast<uint32_t>(runtime->state) : 0U);
        serial::putc('\n');
        return false;
    }

    const uint64_t trapRip = context->rip;
    // The application may have changed RSP since the previous instruction,
    // so each #DB naturally owns a fresh hardware frame. Preserve provenance
    // through the active scheduler/runtime identity and the bounded target
    // stack, then make this current frame the one used by the next command.
    s_operation.debugContext = context;
    s_operation.debugStepTrapRip = trapRip;
    s_operation.debugStepRflagsAfterClear = context->rflags & ~kAmd64TrapFlag;
    context->rflags = s_operation.debugStepRflagsAfterClear;
    s_operation.debugStepActive = false;
    s_operation.debugStepTrapObserved = true;
    ++s_operation.debugStopGeneration;
    if (s_operation.debugStopGeneration == 0) s_operation.debugStopGeneration = 1;
    s_operation.state = GX_DEVELOPMENT_RUN_PAUSED;

    gx_development_debug_snapshot& snapshot = s_operation.debugSnapshot;
    clear_debug_snapshot(&snapshot);
    snapshot.status = GX_DEVELOPMENT_DEBUG_STATUS_TRAP;
    snapshot.trapKind = GX_DEVELOPMENT_DEBUG_TRAP_SINGLE_STEP;
    snapshot.singleStepKind = GX_DEVELOPMENT_DEBUG_SINGLE_STEP_USER_SOURCE;
    set_debug_identity(s_operation, &snapshot);
    snapshot.targetAddress = s_operation.debugBreakpointAddress;
    snapshot.instructionPointer = trapRip;
    snapshot.rawTrapRip = trapRip;
    snapshot.pauseReason = GX_DEVELOPMENT_DEBUG_PAUSE_REASON_SINGLE_STEP;
    compiler::ResolvedSourceMapping currentMapping = {};
    const char* currentMappingError = nullptr;
    if (resolve_debug_mapping_at_address(s_operation, trapRip, &currentMapping,
                                         &currentMappingError)) {
        set_debug_source_mapping(s_operation, &snapshot, currentMapping);
    } else {
        s_operation.debugCurrentSourceMappingValid = false;
        clear_unmapped_source_identity(&snapshot);
    }
    snapshot.stackLow = runtime->stackBase;
    snapshot.stackHigh = runtime->stackBase <= ~static_cast<uint64_t>(0) - runtime->stackSize
        ? runtime->stackBase + runtime->stackSize : 0;
    snapshot.context.architecture = GX_DEVELOPMENT_DEBUG_ARCHITECTURE_AMD64;
    snapshot.context.valid = 1;
    snapshot.context.processId = 0;
    snapshot.context.nativeRuntimeId = s_operation.registrationGeneration;
    snapshot.context.threadId = 1;
    snapshot.context.sessionGeneration = s_operation.registrationGeneration;
    snapshot.context.stopGeneration = s_operation.debugStopGeneration;
    snapshot.context.rip = trapRip;
    snapshot.context.rflags = context->rflags;
    snapshot.context.rsp = context->rsp;
    snapshot.context.rbp = context->rbp;
    snapshot.context.rax = context->rax;
    snapshot.context.rbx = context->rbx;
    snapshot.context.rcx = context->rcx;
    snapshot.context.rdx = context->rdx;
    snapshot.context.rsi = context->rsi;
    snapshot.context.rdi = context->rdi;
    snapshot.context.r8 = context->r8;
    snapshot.context.r9 = context->r9;
    snapshot.context.r10 = context->r10;
    snapshot.context.r11 = context->r11;
    snapshot.context.r12 = context->r12;
    snapshot.context.r13 = context->r13;
    snapshot.context.r14 = context->r14;
    snapshot.context.r15 = context->r15;
    snapshot.rflagsBeforeStep = s_operation.debugStepRflagsBefore;
    snapshot.rflagsWithTrapFlag = s_operation.debugStepRflagsWithTrapFlag;
    snapshot.rflagsAfterTrapFlagClear = s_operation.debugStepRflagsAfterClear;
    copy_debug_start_bytes(s_operation, &snapshot);
    set_source_step_metadata(s_operation, &snapshot);
    if (s_operation.debugStepOverActive) set_source_step_over_metadata(s_operation, &snapshot);

    serial_debug_hex("DEVELOPER_STUDIO_PHASE28B_SINGLE_STEP_TRAP trap_rip=0x", trapRip);
    serial_debug_hex(" rflags_with_tf=0x", s_operation.debugStepRflagsWithTrapFlag);
    serial_debug_hex(" rflags_after_clear=0x", s_operation.debugStepRflagsAfterClear);
    serial::puts(" token=");
    serial::put_hex64(s_operation.debugStepToken);
    serial::putc('\n');
    serial_debug_hex("DEVELOPER_STUDIO_PHASE28B_PAUSED_AFTER_STEP rip=0x", trapRip);
    serial::puts(" stop=");
    serial::put_hex64(s_operation.debugStopGeneration);
    serial::putc('\n');

    if (!native_elf_scheduler_yield()) return false;
    if (s_operation.debugCancelRequested) {
        context->rflags &= ~kAmd64TrapFlag;
        context->rip = reinterpret_cast<uint64_t>(&native_elf_debug_cancel_return);
    } else if (s_operation.debugStepActive) {
        // A subsequent Step command arms the saved iretq frame while this
        // vector-1 handler is suspended. Preserve that arm across the owner
        // round-trip so the next architectural instruction reaches #DB.
        context->rflags |= kAmd64TrapFlag;
    } else {
        context->rflags &= ~kAmd64TrapFlag;
    }
    return true;
#else
    (void)context;
    return false;
#endif
}

static bool begin_debug_instruction_step(
    Operation& operation, gx_development_debug_snapshot* outSnapshot,
    bool sourceStep)
{
    if (operation.state != GX_DEVELOPMENT_RUN_PAUSED ||
        (!operation.debugBreakpointInstalled && !operation.debugStepTrapObserved) ||
        !operation.debugContext || operation.debugStepActive ||
        operation.debugContext->cs != 0x08) return false;
    if (operation.debugContext->rflags & kAmd64TrapFlag) return false;
    if (operation.debugBreakpointInstalled && !restore_debug_entry_breakpoint()) return false;
    operation.debugBreakpointInstalled = false;
    operation.debugStepStartRip = operation.debugContext->rip;
    operation.debugStepRflagsBefore = operation.debugContext->rflags;
    operation.debugStepRflagsWithTrapFlag = operation.debugStepRflagsBefore | kAmd64TrapFlag;
    operation.debugContext->rflags = operation.debugStepRflagsWithTrapFlag;
    ++operation.debugStepToken;
    if (operation.debugStepToken == 0) operation.debugStepToken = 1;
    operation.debugStepActive = true;
    operation.debugStepTrapObserved = false;
    operation.state = GX_DEVELOPMENT_RUN_STEPPING;
    clear_debug_snapshot(outSnapshot);
    if (!outSnapshot) return true;
    outSnapshot->status = GX_DEVELOPMENT_DEBUG_STATUS_SINGLE_STEP_PENDING;
    outSnapshot->singleStepKind = GX_DEVELOPMENT_DEBUG_SINGLE_STEP_USER_SOURCE;
    set_debug_identity(operation, outSnapshot);
    outSnapshot->targetAddress = operation.debugBreakpointAddress;
    outSnapshot->instructionPointer = operation.debugStepStartRip;
    outSnapshot->rflagsBeforeStep = operation.debugStepRflagsBefore;
    outSnapshot->rflagsWithTrapFlag = operation.debugStepRflagsWithTrapFlag;
    copy_debug_start_bytes(operation, outSnapshot);
    if (sourceStep) set_source_step_metadata(operation, outSnapshot);
    return true;
}

static bool step_over_caller_frame_matches(
    const Operation& operation, const NativeElfDebugTrap::BreakpointContext* context)
{
    if (!context || !operation.debugStepOverActive || context->cs != 0x08) return false;
    if (operation.debugStepOverStartingRbp != 0 &&
        context->rbp != operation.debugStepOverStartingRbp) return false;
    return true;
}

static bool install_temporary_return_breakpoint(uint64_t returnAddress,
                                                uint8_t* originalByte)
{
    // Both source Step Over and source Step Out deliberately use the loader's
    // one physical NativeElf breakpoint slot.  The controller owns the
    // operation-specific provenance; this helper owns only the byte patch.
    return returnAddress != 0 && install_debug_breakpoint(returnAddress, originalByte);
}

static bool step_over_install_return_breakpoint(Operation& operation)
{
    if (!operation.debugStepOverActive || operation.debugStepOverReturnAddress == 0 ||
        operation.debugStepOverReturnBreakpointInstalled) return false;
    uint8_t originalByte = 0;
    if (!install_temporary_return_breakpoint(operation.debugStepOverReturnAddress,
                                              &originalByte)) return false;
    operation.debugStepOverOriginalReturnByte = originalByte;
    operation.debugStepOverOriginalReturnByteValid = 1;
    operation.debugStepOverReturnBreakpointInstalled = true;
    operation.debugStepOverReturnBreakpointHit = false;
    ++operation.debugStepOverTemporaryBreakpointCount;
    return true;
}

static bool step_out_install_return_breakpoint(Operation& operation)
{
    if (!operation.debugStepOutActive || operation.debugStepOutReturnAddress == 0 ||
        operation.debugStepOutReturnBreakpointInstalled) return false;
    uint8_t originalByte = 0;
    if (!install_temporary_return_breakpoint(operation.debugStepOutReturnAddress,
                                              &originalByte)) return false;
    operation.debugStepOutOriginalReturnByte = originalByte;
    operation.debugStepOutOriginalReturnByteValid = 1;
    operation.debugStepOutReturnBreakpointInstalled = true;
    operation.debugStepOutReturnBreakpointHit = false;
    ++operation.debugStepOutTemporaryBreakpointCount;
    return true;
}

static void step_out_clear_state(Operation& operation)
{
    operation.debugStepOutActive = false;
    operation.debugStepOutReturnBreakpointInstalled = false;
    operation.debugStepOutReturnBreakpointHit = false;
    operation.debugStepOutToken = 0;
}

static bool step_out_caller_frame_matches(
    const Operation& operation, const NativeElfDebugTrap::BreakpointContext* context)
{
    const NativeAppExecutionContext* runtime = native_elf_runtime_context();
    const bool stackRangeValid = runtime &&
        runtime->stackBase <= ~static_cast<uint64_t>(0) - runtime->stackSize;
    return context && runtime && stackRangeValid && operation.debugStepOutActive &&
        context->cs == 0x08 &&
        kernel::native_elf::step_out_caller_frame_matches(
            context->rsp, context->rbp, operation.debugStepOutSavedCallerRbp,
            runtime->stackBase, runtime->stackBase + runtime->stackSize);
}

static void step_over_clear_state(Operation& operation)
{
    operation.debugStepOverActive = false;
    operation.debugStepOverReturnBreakpointInstalled = false;
    operation.debugStepOverReturnBreakpointHit = false;
    operation.debugStepOverToken = 0;
}

static bool complete_debug_instruction_step(
    Operation& operation, gx_development_debug_snapshot* outSnapshot)
{
    if (!native_elf_scheduler_pump()) return false;
    if (operation.state != GX_DEVELOPMENT_RUN_PAUSED ||
        !operation.debugStepTrapObserved) return false;
    if (outSnapshot) *outSnapshot = operation.debugSnapshot;
    return true;
}

static void set_source_step_result_snapshot(
    Operation& operation, gx_development_debug_snapshot* outSnapshot,
    uint32_t result, uint64_t finalRip)
{
    if (!outSnapshot) return;
    if (operation.state == GX_DEVELOPMENT_RUN_PAUSED && operation.debugSnapshot.size != 0)
        *outSnapshot = operation.debugSnapshot;
    else {
        clear_debug_snapshot(outSnapshot);
        set_debug_identity(operation, outSnapshot);
    }
    operation.debugSourceStepResult = result;
    operation.debugSourceStepFinalRip = finalRip;
    outSnapshot->sourceStepResult = result;
    outSnapshot->sourceStepInstructionCount = operation.debugSourceStepInstructionCount;
    outSnapshot->sourceStepInstructionLimit = kNativeElfSourceStepInstructionLimit;
    outSnapshot->sourceStepStartingMappingValid =
        operation.debugSourceStepStartPath[0] != '\0' ? 1U : 0U;
    outSnapshot->sourceStepStartRip = operation.debugSourceStepStartRip;
    outSnapshot->sourceStepFinalRip = finalRip;
    outSnapshot->sourceStepStartLine = operation.debugSourceStepStartLine;
    outSnapshot->sourceStepStartColumn = operation.debugSourceStepStartColumn;
    copy_text(outSnapshot->sourceStepStartPath, sizeof(outSnapshot->sourceStepStartPath),
              operation.debugSourceStepStartPath);
    copy_text(outSnapshot->sourceStepStartFunctionName,
              sizeof(outSnapshot->sourceStepStartFunctionName),
              operation.debugSourceStepStartFunctionName);
}

static gx_result source_step_into(gx_development_debug_snapshot* outSnapshot)
{
    Operation& operation = s_operation;
    if (operation.state != GX_DEVELOPMENT_RUN_PAUSED ||
        (!operation.debugBreakpointInstalled && !operation.debugStepTrapObserved) ||
        !operation.debugContext || operation.debugStepActive ||
        !operation.debugSourceSelected || operation.debugContext->cs != 0x08) {
        set_debug_error(outSnapshot,
                        "NativeElf Source Step Into requires a mapped Paused source target");
        outSnapshot->sourceStepResult = GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_NO_SOURCE_MAPPING;
        return GX_ERROR_BUSY;
    }
    if (operation.debugContext->rflags & kAmd64TrapFlag) {
        set_debug_error(outSnapshot, "NativeElf source target context already owns Trap Flag");
        return GX_ERROR_FAILED;
    }

    compiler::ResolvedSourceMapping startingMapping = {};
    const char* mappingError = nullptr;
    if (!resolve_debug_mapping_at_address(operation, operation.debugContext->rip,
                                          &startingMapping, &mappingError)) {
        set_debug_error(outSnapshot, mappingError ? mappingError :
                        "NativeElf paused RIP has no trustworthy source mapping");
        outSnapshot->sourceStepResult =
            GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_NO_SOURCE_MAPPING;
        return GX_ERROR_FAILED;
    }
    const SourceStepLocation start = source_step_location_from_mapping(startingMapping);
    if (!source_step_start_valid(start)) {
        set_debug_error(outSnapshot, "NativeElf paused source identity is incomplete");
        outSnapshot->sourceStepResult =
            GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_NO_SOURCE_MAPPING;
        return GX_ERROR_FAILED;
    }
    operation.debugSourceStepActive = true;
    operation.debugSourceStepResult = GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_PENDING;
    operation.debugSourceStepInstructionCount = 0;
    operation.debugSourceStepStartRip = operation.debugContext->rip;
    operation.debugSourceStepFinalRip = operation.debugContext->rip;
    operation.debugSourceStepStartLine = start.line;
    operation.debugSourceStepStartColumn = start.column;
    copy_text(operation.debugSourceStepStartPath, sizeof(operation.debugSourceStepStartPath),
              start.sourcePath);
    copy_text(operation.debugSourceStepStartFunctionName,
              sizeof(operation.debugSourceStepStartFunctionName), start.functionName);
    serial::puts("DEVELOPER_STUDIO_PHASE28C_SOURCE_STEP_REQUEST_PASS start=0x");
    serial::put_hex64(operation.debugSourceStepStartRip);
    serial::puts(" line="); serial::put_hex32(start.line);
    serial::puts(" function="); serial::puts(start.functionName); serial::putc('\n');

    for (;;) {
        if (!begin_debug_instruction_step(operation, outSnapshot, true)) {
            operation.debugSourceStepActive = false;
            operation.debugSourceStepResult = GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_TARGET_FAILED;
            set_debug_error(outSnapshot, "NativeElf source instruction step could not be armed");
            set_source_step_metadata(operation, outSnapshot);
            return GX_ERROR_FAILED;
        }
        if (!complete_debug_instruction_step(operation, outSnapshot)) {
            const bool completed = operation.state == GX_DEVELOPMENT_RUN_COMPLETED;
            const bool failed = operation.state == GX_DEVELOPMENT_RUN_FAILED;
            operation.debugSourceStepActive = false;
            operation.debugSourceStepResult = completed
                ? GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_TARGET_COMPLETED
                : GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_TARGET_FAILED;
            set_source_step_result_snapshot(operation, outSnapshot,
                operation.debugSourceStepResult, operation.debugStepTrapRip);
            if (failed) return GX_ERROR_FAILED;
            return completed ? GX_OK : GX_ERROR_FAILED;
        }
        operation.debugSourceStepFinalRip = operation.debugStepTrapRip;
        ++operation.debugSourceStepInstructionCount;

        const NativeAppExecutionContext* runtime = native_elf_runtime_context();
        if (!runtime || !native_app_pointer_in_range(
                operation.debugStepTrapRip, runtime->imageBase, runtime->imageSize)) {
            operation.debugSourceStepActive = false;
            operation.debugSourceStepResult =
                GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_UNSAFE_RUNTIME_BOUNDARY;
            operation.debugSnapshot.pauseReason = GX_DEVELOPMENT_DEBUG_PAUSE_REASON_SOURCE_STEP;
            set_source_step_result_snapshot(operation, outSnapshot,
                operation.debugSourceStepResult, operation.debugStepTrapRip);
            serial::puts("DEVELOPER_STUDIO_PHASE28C_UNSAFE_RUNTIME_BOUNDARY_PASS rip=0x");
            serial::put_hex64(operation.debugStepTrapRip);
            serial::putc('\n');
            return GX_OK;
        }

        compiler::ResolvedSourceMapping currentMapping = {};
        const char* currentError = nullptr;
        const bool mapped = resolve_debug_mapping_at_address(
            operation, operation.debugStepTrapRip, &currentMapping, &currentError);
        if (!mapped && currentError &&
            !source_step_text_equal(currentError, "source-map address is unmapped")) {
            operation.debugSourceStepActive = false;
            operation.debugSourceStepResult =
                GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_INVALID_SOURCE_MAP;
            set_source_step_result_snapshot(operation, outSnapshot,
                operation.debugSourceStepResult, operation.debugStepTrapRip);
            return GX_ERROR_FAILED;
        }
        const SourceStepLocation current = mapped
            ? source_step_location_from_mapping(currentMapping)
            : SourceStepLocation();
        uint32_t nextCount = operation.debugSourceStepInstructionCount;
        const SourceStepObservation observation = source_step_observe(
            start, current, operation.debugSourceStepInstructionCount,
            kNativeElfSourceStepInstructionLimit, true, false, &nextCount);
        operation.debugSourceStepInstructionCount = nextCount;
        if (mapped) set_debug_source_mapping(operation, &operation.debugSnapshot, currentMapping);
        else {
            operation.debugCurrentSourceMappingValid = false;
            clear_unmapped_source_identity(&operation.debugSnapshot);
        }
        set_source_step_metadata(operation, &operation.debugSnapshot);

        if (observation == SourceStepObservation::Completed) {
            operation.debugSourceStepActive = false;
            operation.debugSourceStepResult = GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_COMPLETED;
            operation.debugSnapshot.pauseReason = GX_DEVELOPMENT_DEBUG_PAUSE_REASON_SOURCE_STEP;
            set_debug_source_mapping(operation, &operation.debugSnapshot, currentMapping);
            set_source_step_metadata(operation, &operation.debugSnapshot);
            *outSnapshot = operation.debugSnapshot;
            serial::puts("DEVELOPER_STUDIO_PHASE28C_NEXT_SOURCE_PASS rip=0x");
            serial::put_hex64(operation.debugStepTrapRip);
            serial::puts(" line="); serial::put_hex32(currentMapping.line);
            serial::puts(" function="); serial::puts(currentMapping.functionName);
            serial::puts(" instructions=");
            serial::put_hex32(operation.debugSourceStepInstructionCount);
            serial::putc('\n');
            return GX_OK;
        }
        if (observation == SourceStepObservation::Limit) {
            operation.debugSourceStepActive = false;
            operation.debugSourceStepResult = GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_LIMIT;
            operation.debugSnapshot.pauseReason = GX_DEVELOPMENT_DEBUG_PAUSE_REASON_SOURCE_STEP;
            set_source_step_metadata(operation, &operation.debugSnapshot);
            *outSnapshot = operation.debugSnapshot;
            serial::puts("DEVELOPER_STUDIO_PHASE28C_STEP_LIMIT_PASS instructions=");
            serial::put_hex32(operation.debugSourceStepInstructionCount);
            serial::putc('\n');
            return GX_OK;
        }
        serial::puts("DEVELOPER_STUDIO_PHASE28C_INTERNAL_STEP_PASS count=");
        serial::put_hex32(operation.debugSourceStepInstructionCount);
        serial::puts(" rip=0x"); serial::put_hex64(operation.debugStepTrapRip);
        serial::putc('\n');
    }
}

static gx_result source_step_over(gx_development_debug_snapshot* outSnapshot)
{
    Operation& operation = s_operation;
    if (operation.state != GX_DEVELOPMENT_RUN_PAUSED ||
        (!operation.debugBreakpointInstalled && !operation.debugStepTrapObserved) ||
        !operation.debugContext || operation.debugStepActive ||
        !operation.debugSourceSelected || operation.debugContext->cs != 0x08) {
        set_debug_error(outSnapshot,
                        "NativeElf Source Step Over requires a mapped Paused source target");
        outSnapshot->sourceStepResult = GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_NO_SOURCE_MAPPING;
        outSnapshot->sourceStepOverResult =
            GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_NO_SOURCE_MAPPING;
        return GX_ERROR_BUSY;
    }
    if (operation.debugContext->rflags & kAmd64TrapFlag) {
        set_debug_error(outSnapshot, "NativeElf source target context already owns Trap Flag");
        return GX_ERROR_FAILED;
    }

    if (operation.debugBreakpointInstalled) {
        if (!restore_debug_entry_breakpoint()) {
            set_debug_error(outSnapshot, "NativeElf source breakpoint could not be restored");
            return GX_ERROR_FAILED;
        }
        operation.debugBreakpointInstalled = false;
        // The paused breakpoint context is a valid starting context even
        // though the source INT3 has now been removed.
        operation.debugStepTrapObserved = true;
    }

    compiler::ResolvedSourceMapping startingMapping = {};
    const char* mappingError = nullptr;
    if (!resolve_debug_mapping_at_address(operation, operation.debugContext->rip,
                                          &startingMapping, &mappingError)) {
        set_debug_error(outSnapshot, mappingError ? mappingError :
                        "NativeElf paused RIP has no trustworthy source mapping");
        outSnapshot->sourceStepResult = GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_NO_SOURCE_MAPPING;
        outSnapshot->sourceStepOverResult =
            GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_NO_SOURCE_MAPPING;
        return GX_ERROR_FAILED;
    }
    const SourceStepLocation start = source_step_location_from_mapping(startingMapping);
    if (!source_step_start_valid(start)) {
        set_debug_error(outSnapshot, "NativeElf paused source identity is incomplete");
        outSnapshot->sourceStepResult = GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_NO_SOURCE_MAPPING;
        outSnapshot->sourceStepOverResult =
            GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_NO_SOURCE_MAPPING;
        return GX_ERROR_FAILED;
    }

    operation.debugSourceStepActive = true;
    operation.debugSourceStepResult = GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_PENDING;
    operation.debugSourceStepInstructionCount = 0;
    operation.debugSourceStepStartRip = operation.debugContext->rip;
    operation.debugSourceStepFinalRip = operation.debugContext->rip;
    operation.debugSourceStepStartLine = start.line;
    operation.debugSourceStepStartColumn = start.column;
    copy_text(operation.debugSourceStepStartPath, sizeof(operation.debugSourceStepStartPath),
              start.sourcePath);
    copy_text(operation.debugSourceStepStartFunctionName,
              sizeof(operation.debugSourceStepStartFunctionName), start.functionName);
    operation.debugStepOverActive = false;
    operation.debugStepOverReturnBreakpointInstalled = false;
    operation.debugStepOverReturnBreakpointHit = false;
    operation.debugStepOverToken = 0;
    operation.debugStepOverCallRip = 0;
    operation.debugStepOverCallTargetAddress = 0;
    operation.debugStepOverReturnAddress = 0;
    operation.debugStepOverStartingRsp = operation.debugContext->rsp;
    operation.debugStepOverStartingRbp = operation.debugContext->rbp;
    operation.debugStepOverReturnTrapRip = 0;
    operation.debugStepOverFinalRsp = operation.debugContext->rsp;
    operation.debugStepOverFinalRbp = operation.debugContext->rbp;
    operation.debugStepOverOriginalReturnByte = 0;
    operation.debugStepOverOriginalReturnByteValid = 0;
    operation.debugStepOverNestedReturnCount = 0;
    operation.debugStepOverTemporaryBreakpointCount = 0;
    operation.debugStepOverInternalMachineStepCount = 0;
    operation.debugStepOverCallerFrameVerified = 0;
    operation.debugStepOverCalleeFunctionName[0] = '\0';

    const char* callError = nullptr;
    const DirectUserCallPlan call = classify_direct_user_call(operation, startingMapping,
                                                               &callError);
    if (!call.callFound) {
        if (callError) {
            operation.debugSourceStepActive = false;
            operation.debugSourceStepResult =
                GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_INVALID_SOURCE_MAP;
            set_debug_error(outSnapshot, callError);
            outSnapshot->sourceStepResult = operation.debugSourceStepResult;
            set_source_step_over_metadata(operation, outSnapshot);
            return GX_ERROR_FAILED;
        }

        // Ordinary source locations use the already-proven Phase 28C
        // controller. The command remains distinct in the ABI and the
        // returned pause is labeled Source Step Over.
        serial::puts("DEVELOPER_STUDIO_PHASE28D_STEP_OVER_FALLBACK_PASS\n");
        const gx_result result = source_step_into(outSnapshot);
        operation.debugSourceStepActive = false;
        if (outSnapshot && outSnapshot->status != GX_DEVELOPMENT_DEBUG_STATUS_REJECTED) {
            outSnapshot->pauseReason = GX_DEVELOPMENT_DEBUG_PAUSE_REASON_SOURCE_STEP_OVER;
            set_source_step_over_metadata(operation, outSnapshot);
        }
        return result;
    }
    if (!call.valid) {
        operation.debugSourceStepActive = false;
        operation.debugSourceStepResult =
            GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_INVALID_SOURCE_MAP;
        set_debug_error(outSnapshot, callError ? callError :
                        "NativeElf direct call target is not a mapped user function");
        outSnapshot->sourceStepResult = operation.debugSourceStepResult;
        set_source_step_over_metadata(operation, outSnapshot);
        return GX_ERROR_FAILED;
    }

    operation.debugStepOverActive = true;
    ++operation.debugStepOverToken;
    if (operation.debugStepOverToken == 0) operation.debugStepOverToken = 1;
    operation.debugStepOverCallRip = call.callRip;
    operation.debugStepOverCallTargetAddress = call.targetAddress;
    operation.debugStepOverReturnAddress = call.returnAddress;
    copy_text(operation.debugStepOverCalleeFunctionName,
              sizeof(operation.debugStepOverCalleeFunctionName), call.calleeFunctionName);
    serial::puts("DEVELOPER_STUDIO_PHASE28D_STEP_OVER_REQUEST_PASS start=0x");
    serial::put_hex64(operation.debugSourceStepStartRip);
    serial::puts(" call=0x"); serial::put_hex64(call.callRip);
    serial::puts(" target=0x"); serial::put_hex64(call.targetAddress);
    serial::puts(" return=0x"); serial::put_hex64(call.returnAddress);
    serial::puts(" callee="); serial::puts(operation.debugStepOverCalleeFunctionName);
    serial::putc('\n');
    serial::puts("DEVELOPER_STUDIO_PHASE28D_CALL_CLASSIFY_PASS\n");
    serial::puts("DEVELOPER_STUDIO_PHASE28D_RETURN_TARGET_PASS\n");
    if (!step_over_install_return_breakpoint(operation)) {
        operation.debugStepOverActive = false;
        operation.debugSourceStepActive = false;
        operation.debugSourceStepResult =
            GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_TARGET_FAILED;
        set_debug_error(outSnapshot,
                        "NativeElf Step Over return breakpoint could not be installed");
        outSnapshot->sourceStepResult = operation.debugSourceStepResult;
        set_source_step_over_metadata(operation, outSnapshot);
        return GX_ERROR_FAILED;
    }
    operation.state = GX_DEVELOPMENT_RUN_STEPPING;
    operation.debugStepActive = false;
    operation.debugStepTrapObserved = false;
    serial::puts("DEVELOPER_STUDIO_PHASE28D_TEMP_BREAKPOINT_PASS address=0x");
    serial::put_hex64(operation.debugStepOverReturnAddress);
    serial::putc('\n');

    if (!native_elf_scheduler_pump()) {
        operation.debugSourceStepActive = false;
        operation.debugSourceStepResult =
            GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_TARGET_FAILED;
        step_over_clear_state(operation);
        set_debug_error(outSnapshot, "NativeElf Step Over could not resume the target");
        return GX_ERROR_FAILED;
    }

    for (;;) {
        if (operation.state != GX_DEVELOPMENT_RUN_PAUSED ||
            !operation.debugStepOverReturnBreakpointHit) {
            const bool completed = operation.state == GX_DEVELOPMENT_RUN_COMPLETED;
            const bool failed = operation.state == GX_DEVELOPMENT_RUN_FAILED;
            operation.debugSourceStepActive = false;
            operation.debugSourceStepResult = completed
                ? GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_TARGET_COMPLETED
                : GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_TARGET_FAILED;
            step_over_clear_state(operation);
            set_source_step_metadata(operation, &operation.debugSnapshot);
            set_source_step_over_metadata(operation, &operation.debugSnapshot);
            if (outSnapshot) *outSnapshot = operation.debugSnapshot;
            return failed ? GX_ERROR_FAILED : (completed ? GX_OK : GX_ERROR_FAILED);
        }
        if (!step_over_caller_frame_matches(operation, operation.debugContext)) {
            ++operation.debugStepOverNestedReturnCount;
            if (operation.debugStepOverNestedReturnCount > kStepOverNestedReturnLimit) {
                operation.debugSourceStepActive = false;
                operation.debugSourceStepResult =
                    GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_TARGET_FAILED;
                step_over_clear_state(operation);
                set_debug_error(outSnapshot,
                                "NativeElf Step Over caller frame was not restored");
                outSnapshot->sourceStepResult = operation.debugSourceStepResult;
                set_source_step_over_metadata(operation, outSnapshot);
                return GX_ERROR_FAILED;
            }
            operation.debugStepOverReturnBreakpointHit = false;
            if (!step_over_install_return_breakpoint(operation)) {
                operation.debugSourceStepActive = false;
                operation.debugSourceStepResult =
                    GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_TARGET_FAILED;
                step_over_clear_state(operation);
                set_debug_error(outSnapshot,
                                "NativeElf nested Step Over return trap could not be rearmed");
                outSnapshot->sourceStepResult = operation.debugSourceStepResult;
                set_source_step_over_metadata(operation, outSnapshot);
                return GX_ERROR_FAILED;
            }
            operation.state = GX_DEVELOPMENT_RUN_STEPPING;
            operation.debugStepTrapObserved = false;
            if (!native_elf_scheduler_pump()) return GX_ERROR_FAILED;
            continue;
        }

        operation.debugStepOverCallerFrameVerified = 1;
        serial::puts("DEVELOPER_STUDIO_PHASE28D_CALLER_FRAME_PASS rbp=0x");
        serial::put_hex64(operation.debugContext ? operation.debugContext->rbp : 0);
        serial::putc('\n');
        break;
    }

    for (;;) {
        if (!begin_debug_instruction_step(operation, outSnapshot, true)) {
            operation.debugSourceStepActive = false;
            operation.debugSourceStepResult =
                GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_TARGET_FAILED;
            set_debug_error(outSnapshot, "NativeElf source caller step could not be armed");
            outSnapshot->sourceStepResult = operation.debugSourceStepResult;
            set_source_step_over_metadata(operation, outSnapshot);
            return GX_ERROR_FAILED;
        }
        ++operation.debugStepOverInternalMachineStepCount;
        if (!complete_debug_instruction_step(operation, outSnapshot)) {
            const bool completed = operation.state == GX_DEVELOPMENT_RUN_COMPLETED;
            const bool failed = operation.state == GX_DEVELOPMENT_RUN_FAILED;
            operation.debugSourceStepActive = false;
            operation.debugSourceStepResult = completed
                ? GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_TARGET_COMPLETED
                : GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_TARGET_FAILED;
            step_over_clear_state(operation);
            set_source_step_metadata(operation, &operation.debugSnapshot);
            set_source_step_over_metadata(operation, &operation.debugSnapshot);
            if (outSnapshot) *outSnapshot = operation.debugSnapshot;
            return failed ? GX_ERROR_FAILED : (completed ? GX_OK : GX_ERROR_FAILED);
        }

        operation.debugSourceStepFinalRip = operation.debugStepTrapRip;
        ++operation.debugSourceStepInstructionCount;
        operation.debugStepOverFinalRsp = operation.debugContext ? operation.debugContext->rsp : 0;
        operation.debugStepOverFinalRbp = operation.debugContext ? operation.debugContext->rbp : 0;
        if (!step_over_caller_frame_matches(operation, operation.debugContext)) {
            operation.debugSourceStepActive = false;
            operation.debugSourceStepResult =
                GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_TARGET_FAILED;
            set_debug_error(outSnapshot,
                            "NativeElf Step Over crossed the caller frame boundary");
            outSnapshot->sourceStepResult = operation.debugSourceStepResult;
            set_source_step_over_metadata(operation, outSnapshot);
            return GX_ERROR_FAILED;
        }

        const NativeAppExecutionContext* runtime = native_elf_runtime_context();
        if (!runtime || !native_app_pointer_in_range(
                operation.debugStepTrapRip, runtime->imageBase, runtime->imageSize)) {
            operation.debugSourceStepActive = false;
            operation.debugSourceStepResult =
                GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_UNSAFE_RUNTIME_BOUNDARY;
            operation.debugSnapshot.pauseReason =
                GX_DEVELOPMENT_DEBUG_PAUSE_REASON_SOURCE_STEP_OVER;
            step_over_clear_state(operation);
            set_source_step_metadata(operation, &operation.debugSnapshot);
            set_source_step_over_metadata(operation, &operation.debugSnapshot);
            if (outSnapshot) *outSnapshot = operation.debugSnapshot;
            serial::puts("DEVELOPER_STUDIO_PHASE28D_UNSAFE_RUNTIME_BOUNDARY_PASS rip=0x");
            serial::put_hex64(operation.debugStepTrapRip);
            serial::putc('\n');
            return GX_OK;
        }

        compiler::ResolvedSourceMapping currentMapping = {};
        const char* currentError = nullptr;
        const bool mapped = resolve_debug_mapping_at_address(
            operation, operation.debugStepTrapRip, &currentMapping, &currentError);
        if (!mapped && currentError &&
            !source_step_text_equal(currentError, "source-map address is unmapped")) {
            operation.debugSourceStepActive = false;
            operation.debugSourceStepResult =
                GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_INVALID_SOURCE_MAP;
            step_over_clear_state(operation);
            set_source_step_metadata(operation, &operation.debugSnapshot);
            set_source_step_over_metadata(operation, &operation.debugSnapshot);
            if (outSnapshot) *outSnapshot = operation.debugSnapshot;
            return GX_ERROR_FAILED;
        }
        const SourceStepLocation current = mapped
            ? source_step_location_from_mapping(currentMapping)
            : SourceStepLocation();
        if (mapped && !source_step_over_same_caller_function(start, current)) {
            operation.debugSourceStepActive = false;
            operation.debugSourceStepResult =
                GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_TARGET_FAILED;
            set_debug_error(outSnapshot,
                            "NativeElf Step Over crossed into a different user function");
            outSnapshot->sourceStepResult = operation.debugSourceStepResult;
            set_source_step_over_metadata(operation, outSnapshot);
            return GX_ERROR_FAILED;
        }
        uint32_t nextCount = operation.debugSourceStepInstructionCount;
        const SourceStepObservation observation = source_step_observe(
            start, current, operation.debugSourceStepInstructionCount,
            kNativeElfSourceStepInstructionLimit, true, false, &nextCount);
        operation.debugSourceStepInstructionCount = nextCount;
        if (mapped) set_debug_source_mapping(operation, &operation.debugSnapshot, currentMapping);
        else {
            operation.debugCurrentSourceMappingValid = false;
            clear_unmapped_source_identity(&operation.debugSnapshot);
        }
        set_source_step_metadata(operation, &operation.debugSnapshot);
        set_source_step_over_metadata(operation, &operation.debugSnapshot);

        if (observation == SourceStepObservation::Completed) {
            operation.debugSourceStepActive = false;
            operation.debugSourceStepResult = GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_COMPLETED;
            operation.debugSnapshot.pauseReason =
                GX_DEVELOPMENT_DEBUG_PAUSE_REASON_SOURCE_STEP_OVER;
            set_debug_source_mapping(operation, &operation.debugSnapshot, currentMapping);
            step_over_clear_state(operation);
            set_source_step_metadata(operation, &operation.debugSnapshot);
            set_source_step_over_metadata(operation, &operation.debugSnapshot);
            if (outSnapshot) *outSnapshot = operation.debugSnapshot;
            serial::puts("DEVELOPER_STUDIO_PHASE28D_NEXT_SOURCE_PASS rip=0x");
            serial::put_hex64(operation.debugStepTrapRip);
            serial::puts(" line="); serial::put_hex32(currentMapping.line);
            serial::puts(" function="); serial::puts(currentMapping.functionName);
            serial::puts(" instructions=");
            serial::put_hex32(operation.debugSourceStepInstructionCount);
            serial::putc('\n');
            return GX_OK;
        }
        if (observation == SourceStepObservation::Limit) {
            operation.debugSourceStepActive = false;
            operation.debugSourceStepResult = GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_LIMIT;
            operation.debugSnapshot.pauseReason =
                GX_DEVELOPMENT_DEBUG_PAUSE_REASON_SOURCE_STEP_OVER;
            step_over_clear_state(operation);
            set_source_step_metadata(operation, &operation.debugSnapshot);
            set_source_step_over_metadata(operation, &operation.debugSnapshot);
            if (outSnapshot) *outSnapshot = operation.debugSnapshot;
            serial::puts("DEVELOPER_STUDIO_PHASE28D_STEP_LIMIT_PASS instructions=");
            serial::put_hex32(operation.debugSourceStepInstructionCount);
            serial::putc('\n');
            return GX_OK;
        }
        serial::puts("DEVELOPER_STUDIO_PHASE28D_INTERNAL_STEP_PASS count=");
        serial::put_hex32(operation.debugSourceStepInstructionCount);
        serial::puts(" rip=0x"); serial::put_hex64(operation.debugStepTrapRip);
        serial::putc('\n');
    }
}

static void set_source_step_out_result_snapshot(
    Operation& operation, gx_development_debug_snapshot* outSnapshot,
    uint32_t result, uint64_t finalRip)
{
    operation.debugStepOutResult = result;
    operation.debugStepOutFinalRip = finalRip;
    operation.debugSourceStepFinalRip = finalRip;
    switch (result) {
    case GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_COMPLETED:
        operation.debugSourceStepResult = GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_COMPLETED;
        break;
    case GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_LIMIT:
        operation.debugSourceStepResult = GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_LIMIT;
        break;
    case GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_TARGET_COMPLETED:
        operation.debugSourceStepResult = GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_TARGET_COMPLETED;
        break;
    case GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_UNSAFE_RUNTIME_BOUNDARY:
        operation.debugSourceStepResult =
            GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_UNSAFE_RUNTIME_BOUNDARY;
        break;
    case GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_INVALID_SOURCE_MAP:
        operation.debugSourceStepResult = GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_INVALID_SOURCE_MAP;
        break;
    default:
        operation.debugSourceStepResult = GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_TARGET_FAILED;
        break;
    }
    if (!outSnapshot) return;
    if (operation.state == GX_DEVELOPMENT_RUN_PAUSED && operation.debugSnapshot.size != 0)
        *outSnapshot = operation.debugSnapshot;
    else {
        clear_debug_snapshot(outSnapshot);
        set_debug_identity(operation, outSnapshot);
    }
    outSnapshot->pauseReason = operation.state == GX_DEVELOPMENT_RUN_PAUSED
        ? GX_DEVELOPMENT_DEBUG_PAUSE_REASON_SOURCE_STEP_OUT
        : GX_DEVELOPMENT_DEBUG_PAUSE_REASON_NONE;
    set_source_step_metadata(operation, outSnapshot);
    set_source_step_out_metadata(operation, outSnapshot);
}

static gx_result source_step_out(gx_development_debug_snapshot* outSnapshot)
{
    Operation& operation = s_operation;
    const NativeAppExecutionContext* runtime = native_elf_runtime_context();
    if (operation.state != GX_DEVELOPMENT_RUN_PAUSED ||
        (!operation.debugBreakpointInstalled && !operation.debugStepTrapObserved) ||
        !operation.debugContext || operation.debugStepActive ||
        !operation.debugSourceSelected || operation.debugContext->cs != 0x08 || !runtime) {
        set_debug_error(outSnapshot,
                        "NativeElf Source Step Out requires a mapped Paused user frame");
        outSnapshot->sourceStepOutResult =
            GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_INVALID_FRAME;
        return GX_ERROR_BUSY;
    }
    if (operation.debugContext->rflags & kAmd64TrapFlag) {
        set_debug_error(outSnapshot, "NativeElf source target context already owns Trap Flag");
        outSnapshot->sourceStepOutResult =
            GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_INVALID_FRAME;
        return GX_ERROR_FAILED;
    }

    compiler::ResolvedSourceMapping currentMapping = {};
    const char* mappingError = nullptr;
    if (!resolve_debug_mapping_at_address(operation, operation.debugContext->rip,
                                          &currentMapping, &mappingError)) {
        set_debug_error(outSnapshot, mappingError ? mappingError :
                        "NativeElf Step Out requires a trustworthy source mapping");
        outSnapshot->sourceStepOutResult =
            GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_INVALID_SOURCE_MAP;
        return GX_ERROR_FAILED;
    }
    const SourceStepLocation current = source_step_location_from_mapping(currentMapping);
    if (!source_step_start_valid(current)) {
        set_debug_error(outSnapshot, "NativeElf Step Out source identity is incomplete");
        outSnapshot->sourceStepOutResult =
            GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_INVALID_SOURCE_MAP;
        return GX_ERROR_FAILED;
    }

    // gx_main is entered directly by the trusted NativeElf trampoline.  Its
    // stack return link crosses into runtime teardown rather than another
    // user-image frame, so reject the request at that ABI boundary instead
    // of interpreting trampoline state as an application caller.
    if (equal_text(current.functionName, "gx_main")) {
        set_debug_error(outSnapshot,
                        "NativeElf Step Out has no caller frame in the user image");
        outSnapshot->sourceStepOutResult =
            GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_NO_CALLER_FRAME;
        outSnapshot->sourceStepOutCurrentFrameValid = 1;
        outSnapshot->sourceStepOutStartRip = operation.debugContext->rip;
        outSnapshot->sourceStepOutStartRsp = operation.debugContext->rsp;
        outSnapshot->sourceStepOutStartRbp = operation.debugContext->rbp;
        copy_text(outSnapshot->sourceStepOutCalleeFunctionName,
                  sizeof(outSnapshot->sourceStepOutCalleeFunctionName), current.functionName);
        serial::puts("DEVELOPER_STUDIO_PHASE28E_NO_CALLER_FRAME_PASS function=gx_main\n");
        return GX_ERROR_FAILED;
    }

    const StepOutFramePlan plan = capture_step_out_frame(
        operation, currentMapping, operation.debugContext, &mappingError);
    if (!plan.valid) {
        set_debug_error(outSnapshot, mappingError ? mappingError :
                        "NativeElf Step Out current frame could not be recovered");
        outSnapshot->sourceStepOutResult = plan.failureResult != 0
            ? plan.failureResult
            : static_cast<uint32_t>(
                GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_INVALID_FRAME);
        outSnapshot->sourceStepOutCurrentFrameValid =
            plan.startRbp != 0 ? 1U : 0U;
        outSnapshot->sourceStepOutStartRip = plan.startRip;
        outSnapshot->sourceStepOutStartRsp = plan.startRsp;
        outSnapshot->sourceStepOutStartRbp = plan.startRbp;
        outSnapshot->sourceStepOutSavedCallerRbp = plan.savedCallerRbp;
        outSnapshot->sourceStepOutReturnAddress = plan.returnAddress;
        copy_text(outSnapshot->sourceStepOutCalleeFunctionName,
                  sizeof(outSnapshot->sourceStepOutCalleeFunctionName), current.functionName);
        copy_text(outSnapshot->sourceStepOutCallerFunctionName,
                  sizeof(outSnapshot->sourceStepOutCallerFunctionName),
                  plan.callerMapping.functionName);
        copy_text(outSnapshot->sourceStepOutCallerSourcePath,
                  sizeof(outSnapshot->sourceStepOutCallerSourcePath),
                  plan.callerMapping.sourcePath);
        return GX_ERROR_FAILED;
    }

    // A source breakpoint owns the loader's one physical breakpoint slot.
    // Release it before arming the independent return control point; the
    // saved pause context remains the genuine current callee frame.
    if (operation.debugBreakpointInstalled) {
        if (!restore_debug_entry_breakpoint()) {
            set_debug_error(outSnapshot,
                            "NativeElf source breakpoint could not be restored for Step Out");
            outSnapshot->sourceStepOutResult =
                GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_TARGET_FAILED;
            return GX_ERROR_FAILED;
        }
        operation.debugBreakpointInstalled = false;
        operation.debugStepTrapObserved = true;
    }

    operation.debugSourceStepActive = true;
    operation.debugSourceStepResult = GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_PENDING;
    operation.debugSourceStepInstructionCount = 0;
    operation.debugSourceStepStartRip = plan.startRip;
    operation.debugSourceStepFinalRip = plan.startRip;
    operation.debugSourceStepStartLine = current.line;
    operation.debugSourceStepStartColumn = current.column;
    copy_text(operation.debugSourceStepStartPath,
              sizeof(operation.debugSourceStepStartPath), current.sourcePath);
    copy_text(operation.debugSourceStepStartFunctionName,
              sizeof(operation.debugSourceStepStartFunctionName), current.functionName);

    step_out_clear_state(operation);
    operation.debugStepOutActive = true;
    operation.debugStepOutResult = GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_PENDING;
    operation.debugStepOutInstructionCount = 0;
    operation.debugStepOutStartRip = plan.startRip;
    operation.debugStepOutStartRsp = plan.startRsp;
    operation.debugStepOutStartRbp = plan.startRbp;
    operation.debugStepOutSavedCallerRbp = plan.savedCallerRbp;
    operation.debugStepOutReturnAddress = plan.returnAddress;
    operation.debugStepOutCallerReturnSourceLine = plan.callerMapping.line;
    operation.debugStepOutReturnTrapRip = 0;
    operation.debugStepOutFinalRip = plan.startRip;
    operation.debugStepOutFinalRsp = plan.startRsp;
    operation.debugStepOutFinalRbp = plan.startRbp;
    operation.debugStepOutTemporaryBreakpointCount = 0;
    operation.debugStepOutInternalMachineStepCount = 0;
    operation.debugStepOutCallerFrameVerified = 0;
    operation.debugStepOutRemainingCalleeExecuted = 0;
    operation.debugStepOutIntermediatePauseCount = 0;
    operation.debugStepOutCurrentFrameValid = 1;
    operation.debugStepOutToken = operation.debugStopGeneration;
    if (operation.debugStepOutToken == 0) operation.debugStepOutToken = 1;
    operation.debugStepOutOriginalReturnByte = 0;
    operation.debugStepOutOriginalReturnByteValid = 0;
    copy_text(operation.debugStepOutCalleeFunctionName,
              sizeof(operation.debugStepOutCalleeFunctionName), current.functionName);
    copy_text(operation.debugStepOutCallerFunctionName,
              sizeof(operation.debugStepOutCallerFunctionName),
              plan.callerMapping.functionName);
    copy_text(operation.debugStepOutCallerSourcePath,
              sizeof(operation.debugStepOutCallerSourcePath), plan.callerMapping.sourcePath);

    serial::puts("DEVELOPER_STUDIO_PHASE28E_STEP_OUT_REQUEST_PASS callee=");
    serial::puts(operation.debugStepOutCalleeFunctionName);
    serial::puts(" caller="); serial::puts(operation.debugStepOutCallerFunctionName);
    serial::puts(" frame_rbp=0x"); serial::put_hex64(plan.startRbp);
    serial::puts(" return=0x"); serial::put_hex64(plan.returnAddress); serial::putc('\n');
    serial::puts("DEVELOPER_STUDIO_PHASE28E_FRAME_CAPTURE_PASS\n");
    serial::puts("DEVELOPER_STUDIO_PHASE28E_RETURN_ADDRESS_PASS\n");

    if (!step_out_install_return_breakpoint(operation)) {
        operation.debugSourceStepActive = false;
        operation.debugStepOutResult =
            GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_TARGET_FAILED;
        step_out_clear_state(operation);
        set_debug_error(outSnapshot,
                        "NativeElf Step Out return breakpoint could not be installed");
        outSnapshot->sourceStepOutResult = operation.debugStepOutResult;
        set_source_step_out_metadata(operation, outSnapshot);
        return GX_ERROR_FAILED;
    }
    operation.state = GX_DEVELOPMENT_RUN_STEPPING;
    operation.debugStepActive = false;
    operation.debugStepTrapObserved = false;
    serial::puts("DEVELOPER_STUDIO_PHASE28E_TEMP_BREAKPOINT_PASS address=0x");
    serial::put_hex64(operation.debugStepOutReturnAddress); serial::putc('\n');

    if (!native_elf_scheduler_pump()) {
        const bool completed = operation.state == GX_DEVELOPMENT_RUN_COMPLETED;
        const uint32_t result = completed
            ? GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_TARGET_COMPLETED
            : GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_TARGET_FAILED;
        operation.debugSourceStepActive = false;
        if (operation.debugStepOutReturnBreakpointInstalled)
            (void)restore_debug_entry_breakpoint();
        step_out_clear_state(operation);
        set_source_step_out_result_snapshot(operation, outSnapshot, result,
                                             operation.debugStepOutFinalRip);
        if (!completed && outSnapshot) outSnapshot->status = GX_DEVELOPMENT_DEBUG_STATUS_REJECTED;
        return completed ? GX_OK : GX_ERROR_FAILED;
    }

    if (operation.state != GX_DEVELOPMENT_RUN_PAUSED ||
        !operation.debugStepOutReturnBreakpointHit) {
        const bool completed = operation.state == GX_DEVELOPMENT_RUN_COMPLETED;
        const uint32_t result = completed
            ? GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_TARGET_COMPLETED
            : GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_TARGET_FAILED;
        operation.debugSourceStepActive = false;
        step_out_clear_state(operation);
        set_source_step_out_result_snapshot(operation, outSnapshot, result,
                                             operation.debugStepOutFinalRip);
        if (!completed && outSnapshot) outSnapshot->status = GX_DEVELOPMENT_DEBUG_STATUS_REJECTED;
        return completed ? GX_OK : GX_ERROR_FAILED;
    }

    if (!step_out_caller_frame_matches(operation, operation.debugContext)) {
        operation.debugSourceStepActive = false;
        step_out_clear_state(operation);
        set_source_step_out_result_snapshot(
            operation, outSnapshot,
            GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_INVALID_FRAME,
            operation.debugStepOutReturnAddress);
        if (outSnapshot) {
            outSnapshot->status = GX_DEVELOPMENT_DEBUG_STATUS_REJECTED;
            copy_text(outSnapshot->errorMessage, sizeof(outSnapshot->errorMessage),
                      "NativeElf Step Out caller frame validation failed");
        }
        return GX_ERROR_FAILED;
    }
    operation.debugStepOutCallerFrameVerified = 1;
    serial::puts("DEVELOPER_STUDIO_PHASE28E_CALLER_FRAME_PASS rbp=0x");
    serial::put_hex64(operation.debugContext->rbp); serial::putc('\n');

    for (;;) {
        if (operation.debugStepOutInstructionCount >= kNativeElfStepOutInstructionLimit) {
            operation.debugSourceStepActive = false;
            step_out_clear_state(operation);
            set_source_step_out_result_snapshot(
                operation, outSnapshot,
                GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_LIMIT,
                operation.debugStepOutFinalRip);
            serial::puts("DEVELOPER_STUDIO_PHASE28E_STEP_OUT_LIMIT_PASS\n");
            return GX_OK;
        }
        if (!begin_debug_instruction_step(operation, outSnapshot, true)) {
            operation.debugSourceStepActive = false;
            step_out_clear_state(operation);
            set_source_step_out_result_snapshot(
                operation, outSnapshot,
                GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_TARGET_FAILED,
                operation.debugStepOutFinalRip);
            if (outSnapshot) {
                outSnapshot->status = GX_DEVELOPMENT_DEBUG_STATUS_REJECTED;
                copy_text(outSnapshot->errorMessage, sizeof(outSnapshot->errorMessage),
                          "NativeElf Step Out caller source step could not be armed");
            }
            return GX_ERROR_FAILED;
        }
        ++operation.debugStepOutInternalMachineStepCount;
        if (!complete_debug_instruction_step(operation, outSnapshot)) {
            const bool completed = operation.state == GX_DEVELOPMENT_RUN_COMPLETED;
            const uint32_t result = completed
                ? GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_TARGET_COMPLETED
                : GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_TARGET_FAILED;
            operation.debugSourceStepActive = false;
            step_out_clear_state(operation);
            set_source_step_out_result_snapshot(operation, outSnapshot, result,
                                                 operation.debugStepOutFinalRip);
            if (!completed && outSnapshot) outSnapshot->status = GX_DEVELOPMENT_DEBUG_STATUS_REJECTED;
            return completed ? GX_OK : GX_ERROR_FAILED;
        }
        operation.debugStepOutFinalRip = operation.debugStepTrapRip;
        operation.debugStepOutFinalRsp = operation.debugContext ? operation.debugContext->rsp : 0;
        operation.debugStepOutFinalRbp = operation.debugContext ? operation.debugContext->rbp : 0;
        ++operation.debugStepOutInstructionCount;
        ++operation.debugStepOutIntermediatePauseCount;
        if (!step_out_caller_frame_matches(operation, operation.debugContext)) {
            operation.debugSourceStepActive = false;
            step_out_clear_state(operation);
            set_source_step_out_result_snapshot(
                operation, outSnapshot,
                GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_INVALID_FRAME,
                operation.debugStepOutFinalRip);
            if (outSnapshot) {
                outSnapshot->status = GX_DEVELOPMENT_DEBUG_STATUS_REJECTED;
                copy_text(outSnapshot->errorMessage, sizeof(outSnapshot->errorMessage),
                          "NativeElf Step Out crossed the caller frame boundary");
            }
            return GX_ERROR_FAILED;
        }

        runtime = native_elf_runtime_context();
        if (!runtime || !native_app_pointer_in_range(operation.debugStepTrapRip,
                                                     runtime->imageBase, runtime->imageSize)) {
            operation.debugSourceStepActive = false;
            step_out_clear_state(operation);
            set_source_step_out_result_snapshot(
                operation, outSnapshot,
                GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_UNSAFE_RUNTIME_BOUNDARY,
                operation.debugStepOutFinalRip);
            serial::puts("DEVELOPER_STUDIO_PHASE28E_UNSAFE_RUNTIME_BOUNDARY_PASS\n");
            return GX_OK;
        }

        compiler::ResolvedSourceMapping callerMapping = {};
        const char* callerError = nullptr;
        const bool mapped = resolve_debug_mapping_at_address(
            operation, operation.debugStepTrapRip, &callerMapping, &callerError);
        if (!mapped && callerError &&
            !source_step_text_equal(callerError, "source-map address is unmapped")) {
            operation.debugSourceStepActive = false;
            step_out_clear_state(operation);
            set_source_step_out_result_snapshot(
                operation, outSnapshot,
                GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_INVALID_SOURCE_MAP,
                operation.debugStepOutFinalRip);
            if (outSnapshot) {
                outSnapshot->status = GX_DEVELOPMENT_DEBUG_STATUS_REJECTED;
                copy_text(outSnapshot->errorMessage, sizeof(outSnapshot->errorMessage),
                          callerError);
            }
            return GX_ERROR_FAILED;
        }
        if (mapped && (!equal_text(callerMapping.functionName,
                                   operation.debugStepOutCallerFunctionName) ||
                       !equal_text(callerMapping.sourcePath,
                                   operation.debugStepOutCallerSourcePath))) {
            operation.debugSourceStepActive = false;
            step_out_clear_state(operation);
            set_source_step_out_result_snapshot(
                operation, outSnapshot,
                GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_INVALID_FRAME,
                operation.debugStepOutFinalRip);
            if (outSnapshot) {
                outSnapshot->status = GX_DEVELOPMENT_DEBUG_STATUS_REJECTED;
                copy_text(outSnapshot->errorMessage, sizeof(outSnapshot->errorMessage),
                          "NativeElf Step Out reached an unexpected user frame");
            }
            return GX_ERROR_FAILED;
        }
        operation.debugSourceStepInstructionCount = operation.debugStepOutInstructionCount;
        operation.debugSourceStepFinalRip = operation.debugStepOutFinalRip;
        if (mapped) set_debug_source_mapping(operation, &operation.debugSnapshot, callerMapping);
        else {
            operation.debugCurrentSourceMappingValid = false;
            clear_unmapped_source_identity(&operation.debugSnapshot);
        }
        set_source_step_metadata(operation, &operation.debugSnapshot);
        set_source_step_out_metadata(operation, &operation.debugSnapshot);

        const bool reachedNewCallerSource = mapped &&
            callerMapping.line != operation.debugStepOutCallerReturnSourceLine;
        if (reachedNewCallerSource) {
            operation.debugSourceStepActive = false;
            operation.debugStepOutResult =
                GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_COMPLETED;
            operation.debugSourceStepResult = GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_COMPLETED;
            set_debug_source_mapping(operation, &operation.debugSnapshot, callerMapping);
            operation.debugSnapshot.pauseReason =
                GX_DEVELOPMENT_DEBUG_PAUSE_REASON_SOURCE_STEP_OUT;
            step_out_clear_state(operation);
            set_source_step_out_result_snapshot(
                operation, outSnapshot,
                GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_COMPLETED,
                operation.debugStepOutFinalRip);
            serial::puts("DEVELOPER_STUDIO_PHASE28E_CALLER_SOURCE_PASS path=");
            serial::puts(callerMapping.sourcePath);
            serial::puts(" line="); serial::put_hex32(callerMapping.line);
            serial::puts(" function="); serial::puts(callerMapping.functionName);
            serial::putc('\n');
            serial::puts("DEVELOPER_STUDIO_PHASE28E_STEP_OUT_PASS\n");
            return GX_OK;
        }
    }
}

static void clear_call_stack(gx_development_debug_call_stack* result)
{
    if (!result) return;
    *result = {};
    result->size = sizeof(*result);
    result->version = GX_DEVELOPMENT_DEBUG_API_VERSION;
    result->status = GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_NONE;
}

static void set_call_stack_error(gx_development_debug_call_stack* result,
                                 uint32_t status, const char* message)
{
    if (!result) return;
    result->status = status;
    copy_text(result->errorMessage, sizeof(result->errorMessage),
              message ? message : "NativeElf Call Stack request rejected");
}

static void set_call_stack_identity(const Operation& operation,
                                    gx_development_debug_call_stack* result,
                                    const NativeAppExecutionContext& runtime)
{
    if (!result) return;
    result->handle = operation.handle;
    result->processId = 0;
    result->nativeRuntimeId = operation.registrationGeneration;
    result->threadId = 1;
    result->sessionGeneration = operation.registrationGeneration;
    result->stopGeneration = operation.debugStopGeneration;
    result->stackLow = runtime.stackBase;
    result->stackHigh = runtime.stackBase <=
        ~static_cast<uint64_t>(0) - runtime.stackSize
        ? runtime.stackBase + runtime.stackSize : 0;
}

static bool set_call_stack_mapping(
    gx_development_debug_call_stack_frame* frame,
    const compiler::ResolvedSourceMapping& mapping, bool sourceLocation)
{
    if (!frame || !copy_text(frame->functionName, sizeof(frame->functionName),
                             mapping.functionName)) return false;
    frame->flags |= GX_DEVELOPMENT_DEBUG_CALL_STACK_FRAME_FUNCTION_RESOLVED;
    if (!sourceLocation || !copy_text(frame->sourcePath, sizeof(frame->sourcePath),
                                      mapping.sourcePath)) {
        frame->flags |= GX_DEVELOPMENT_DEBUG_CALL_STACK_FRAME_SOURCE_UNAVAILABLE;
        return true;
    }
    frame->sourceLine = mapping.line;
    frame->sourceColumn = mapping.column;
    frame->flags |= GX_DEVELOPMENT_DEBUG_CALL_STACK_FRAME_SOURCE_MAPPED;
    return true;
}

static void clear_debug_variables(gx_development_debug_variables* result)
{
    if (!result) return;
    *result = {};
    result->size = sizeof(*result);
    result->version = GX_DEVELOPMENT_DEBUG_API_VERSION;
    result->status = GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_NONE;
}

static void set_debug_variables_error(gx_development_debug_variables* result,
                                      uint32_t status, const char* message)
{
    if (!result) return;
    result->status = status;
    copy_text(result->errorMessage, sizeof(result->errorMessage),
              message ? message : "NativeElf variable inspection rejected");
}

static void clear_debug_expression(gx_development_debug_expression* result)
{
    if (!result) return;
    *result = {};
    result->size = sizeof(*result);
    result->version = GX_DEVELOPMENT_DEBUG_API_VERSION;
    result->status = GX_DEVELOPMENT_DEBUG_EXPRESSION_STATUS_NONE;
}

static void set_debug_expression_error(gx_development_debug_expression* result,
                                       uint32_t status, uint32_t category,
                                       const char* message, uint32_t offset = 0)
{
    if (!result) return;
    result->status = status;
    result->errorCategory = category;
    result->diagnosticOffset = offset;
    copy_text(result->errorMessage, sizeof(result->errorMessage),
              message ? message : "NativeElf watch expression rejected");
}

struct NativeDebugWatchMetadataContext {
    uint64_t address;
    char functionName[GX_DEVELOPMENT_DEBUG_MAX_FUNCTION_NAME_BYTES];
};

static NativeDebugWatchResolveStatus resolve_native_debug_watch_identifier(
    void* opaque, const char* identifier)
{
    const NativeDebugWatchMetadataContext* context =
        static_cast<const NativeDebugWatchMetadataContext*>(opaque);
    const NativeAppExecutionContext* runtime = native_elf_runtime_context();
    if (!context || !runtime || !identifier || identifier[0] == '\0' ||
        s_operation.artifactSize == 0)
        return NativeDebugWatchResolveStatus::MetadataUnavailable;
    const char* error = nullptr;
    const compiler::DebugVariableLookupStatus status =
        compiler::resolve_bootstrap_debug_variable_state(
            s_artifact, static_cast<uint32_t>(s_operation.artifactSize),
            runtime->imageBase, s_operation.debugCodeFileOffset,
            s_operation.debugCodeBytes, context->address, context->functionName,
            identifier, &error);
    (void)error;
    switch (status) {
    case compiler::DebugVariableLookupStatus::Live:
        return NativeDebugWatchResolveStatus::Available;
    case compiler::DebugVariableLookupStatus::DeclaredNotLive:
        return NativeDebugWatchResolveStatus::NotLive;
    case compiler::DebugVariableLookupStatus::UnsupportedType:
        return NativeDebugWatchResolveStatus::UnsupportedType;
    case compiler::DebugVariableLookupStatus::Unknown:
        return NativeDebugWatchResolveStatus::UnknownIdentifier;
    case compiler::DebugVariableLookupStatus::MetadataUnavailable:
        return NativeDebugWatchResolveStatus::MetadataUnavailable;
    }
    return NativeDebugWatchResolveStatus::MetadataUnavailable;
}

static bool resolve_call_stack_direct_call(
    const Operation& operation, uint64_t returnAddress, const char* expectedCallee,
    uint64_t* callRip, compiler::ResolvedSourceMapping* callerMapping)
{
    if (callRip) *callRip = 0;
    if (callerMapping) *callerMapping = {};
    if (!expectedCallee || !callRip || !callerMapping || returnAddress < kStepOverCallBytes)
        return false;
    const uint64_t candidate = returnAddress - kStepOverCallBytes;
    uint8_t opcode = 0;
    if (!debug_read_code_byte(operation, candidate, &opcode) || opcode != 0xE8) return false;
    uint8_t displacementBytes[4] = {};
    for (uint32_t index = 0; index < sizeof(displacementBytes); ++index) {
        if (!debug_read_code_byte(operation, candidate + 1U + index,
                                  &displacementBytes[index])) return false;
    }
    const uint32_t rawDisplacement = static_cast<uint32_t>(displacementBytes[0]) |
        (static_cast<uint32_t>(displacementBytes[1]) << 8) |
        (static_cast<uint32_t>(displacementBytes[2]) << 16) |
        (static_cast<uint32_t>(displacementBytes[3]) << 24);
    const int64_t displacement = static_cast<int32_t>(rawDisplacement);
    uint64_t target = 0;
    if (displacement >= 0) {
        const uint64_t amount = static_cast<uint64_t>(displacement);
        if (returnAddress > ~static_cast<uint64_t>(0) - amount) return false;
        target = returnAddress + amount;
    } else {
        const uint64_t amount = static_cast<uint64_t>(-displacement);
        if (returnAddress < amount) return false;
        target = returnAddress - amount;
    }
    compiler::ResolvedSourceMapping calleeMapping = {};
    const bool targetMapped = debug_code_address(operation, target) &&
        debug_code_address(operation, returnAddress) &&
        resolve_debug_mapping_forward_address(operation, target, &calleeMapping);
    if (!targetMapped || !equal_text(calleeMapping.functionName, expectedCallee)) return false;
    const bool callerMapped = resolve_debug_mapping_at_address(operation, candidate, callerMapping, nullptr) ||
        resolve_debug_mapping_at_address(operation, returnAddress, callerMapping, nullptr);
    if (!callerMapped) {
        return false;
    }
    *callRip = candidate;
    return true;
}

gx_result call_stack(const gx_development_debug_request& request,
                     gx_development_debug_call_stack* outResult)
{
    if (!outResult) return GX_ERROR_INVALID_ARGUMENT;
    clear_call_stack(outResult);
    if (request.size < GX_DEVELOPMENT_DEBUG_REQUEST_LEGACY_BYTES ||
        request.version != GX_DEVELOPMENT_DEBUG_API_VERSION ||
        request.command != GX_DEVELOPMENT_DEBUG_CALL_STACK) {
        set_call_stack_error(outResult, GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_REJECTED,
                             "NativeElf Call Stack request version or command is invalid");
        return GX_ERROR_INVALID_ARGUMENT;
    }
    if (!decode(request.handle)) {
        set_call_stack_error(outResult, GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_STALE,
                             "NativeElf Call Stack handle is stale");
        return GX_ERROR_FAILED;
    }
    if (!s_operation.debugControlled) {
        set_call_stack_error(outResult, GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_REJECTED,
                             "Run generation is not debugger-controlled");
        return GX_ERROR_UNSUPPORTED;
    }
    if (request.sessionGeneration == 0 || request.nativeRuntimeId == 0 ||
        request.threadId == 0 || request.stopGeneration == 0 ||
        !request.artifactSha256 || !equal_text(request.artifactSha256, s_operation.artifactSha256) ||
        !debug_request_identity_matches(s_operation, request)) {
        set_call_stack_error(outResult, (request.sessionGeneration != 0 &&
                                     request.sessionGeneration != s_operation.registrationGeneration) ||
                                 (request.stopGeneration != 0 &&
                                     request.stopGeneration != s_operation.debugStopGeneration)
                                 ? GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_STALE
                                 : GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_REJECTED,
                             "NativeElf Call Stack session identity is stale");
        return GX_ERROR_FAILED;
    }
    if (s_operation.state != GX_DEVELOPMENT_RUN_PAUSED ||
        (!s_operation.debugBreakpointHit && !s_operation.debugStepTrapObserved) ||
        s_operation.debugStepActive || s_operation.debugSourceStepActive ||
        s_operation.debugStepOverActive || s_operation.debugStepOutActive ||
        !s_operation.debugContext || s_operation.debugContext->cs != 0x08) {
        set_call_stack_error(outResult,
                             GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_NO_PAUSED_CONTEXT,
                             "NativeElf target has no complete paused user context");
        return GX_ERROR_BUSY;
    }
    const NativeAppExecutionContext* runtime = native_elf_runtime_context();
    if (!runtime || runtime->state != NativeAppExecutionState::Running ||
        runtime->stackBase == 0 || runtime->stackSize == 0 ||
        runtime->stackBase > ~static_cast<uint64_t>(0) - runtime->stackSize) {
        set_call_stack_error(outResult, GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_NO_PAUSED_CONTEXT,
                             "NativeElf runtime stack identity is unavailable");
        return GX_ERROR_FAILED;
    }
    set_call_stack_identity(s_operation, outResult, *runtime);
    const NativeElfDebugTrap::BreakpointContext& context = *s_operation.debugContext;
    if (!debug_code_address(s_operation, context.rip)) {
        set_call_stack_error(outResult,
                             GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_INVALID_RETURN_ADDRESS,
                             "NativeElf paused RIP is outside the user image");
        return GX_ERROR_FAILED;
    }
    compiler::ResolvedSourceMapping topMapping = {};
    if (!resolve_debug_mapping_at_address(s_operation, context.rip, &topMapping, nullptr) ||
        topMapping.functionName[0] == '\0') {
        set_call_stack_error(outResult,
                             GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_UNSUPPORTED_FRAME,
                             "NativeElf paused RIP has no trustworthy user function");
        return GX_ERROR_UNSUPPORTED;
    }
    const uint64_t stackHigh = runtime->stackBase + runtime->stackSize;
    if (!step_out_frame_shape_valid(context.rsp, context.rbp,
                                    runtime->stackBase, stackHigh)) {
        set_call_stack_error(outResult, GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_INVALID_FRAME,
                             "NativeElf paused frame pointer shape is invalid");
        return GX_ERROR_FAILED;
    }

    gx_development_debug_call_stack_frame& top = outResult->frames[0];
    top.depth = 0;
    top.flags = GX_DEVELOPMENT_DEBUG_CALL_STACK_FRAME_VALIDATED |
        GX_DEVELOPMENT_DEBUG_CALL_STACK_FRAME_POINTER_VALID;
    top.instructionPointer = context.rip;
    top.stackPointer = context.rsp;
    top.framePointer = context.rbp;
    if (!set_call_stack_mapping(&top, topMapping, true)) {
        set_call_stack_error(outResult,
                             GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_UNSUPPORTED_FRAME,
                             "NativeElf top frame function identity is too long");
        return GX_ERROR_UNSUPPORTED;
    }
    outResult->frameCount = 1;
    uint64_t seen[GX_DEVELOPMENT_DEBUG_MAX_CALL_STACK_FRAMES] = {};
    seen[0] = context.rbp;
    if (equal_text(topMapping.functionName, "gx_main")) {
        top.flags |= GX_DEVELOPMENT_DEBUG_CALL_STACK_FRAME_ROOT;
        outResult->status = GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_SUCCESS;
        return GX_OK;
    }

    uint64_t nextRbp = 0;
    uint64_t currentReturnAddress = 0;
    if (!debug_read_stack_u64(s_operation, context.rbp, &nextRbp) ||
        !debug_read_stack_u64(s_operation, context.rbp + sizeof(uint64_t),
                              &currentReturnAddress) ||
        !step_out_caller_link_valid(context.rbp, nextRbp, currentReturnAddress,
                                    runtime->stackBase, stackHigh,
                                    runtime->imageBase, runtime->imageSize)) {
        set_call_stack_error(outResult, currentReturnAddress == 0
                                 ? GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_INVALID_FRAME
                                 : GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_INVALID_RETURN_ADDRESS,
                             "NativeElf top frame caller link is invalid");
        return GX_ERROR_FAILED;
    }
    top.returnAddress = currentReturnAddress;
    top.flags |= GX_DEVELOPMENT_DEBUG_CALL_STACK_FRAME_RETURN_ADDRESS_VALID;
    char currentFunction[GX_DEVELOPMENT_DEBUG_MAX_FUNCTION_NAME_BYTES] = {};
    if (!copy_text(currentFunction, sizeof(currentFunction), topMapping.functionName)) {
        set_call_stack_error(outResult, GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_UNSUPPORTED_FRAME,
                             "NativeElf top frame function identity is invalid");
        return GX_ERROR_UNSUPPORTED;
    }

    for (;;) {
        compiler::ResolvedSourceMapping callerMapping = {};
        uint64_t callerInstructionPointer = currentReturnAddress;
        bool callSiteMapped = false;
        uint64_t resolvedCallRip = 0;
        if (resolve_call_stack_direct_call(s_operation, currentReturnAddress,
                                           currentFunction, &resolvedCallRip,
                                           &callerMapping)) {
            callerInstructionPointer = resolvedCallRip;
            callSiteMapped = true;
        } else if (!resolve_debug_mapping_near_address(
                       s_operation, currentReturnAddress, &callerMapping, nullptr)) {
            set_call_stack_error(outResult,
                                 GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_INVALID_RETURN_ADDRESS,
                                 "NativeElf caller return address has no user mapping");
            return GX_ERROR_FAILED;
        }
        if (callerMapping.functionName[0] == '\0') {
            set_call_stack_error(outResult,
                                 GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_INVALID_RETURN_ADDRESS,
                                 "NativeElf caller function identity is missing");
            return GX_ERROR_FAILED;
        }
        if (outResult->frameCount >= GX_DEVELOPMENT_DEBUG_MAX_CALL_STACK_FRAMES) {
            outResult->truncated = 1;
            outResult->status = GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_TRUNCATED;
            return GX_OK;
        }
        if (!native_elf_frame_pointer_valid(nextRbp, runtime->stackBase, stackHigh) ||
            native_elf_call_stack_frame_pointer_seen(seen, outResult->frameCount, nextRbp)) {
            set_call_stack_error(outResult, GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_INVALID_FRAME,
                                 "NativeElf caller frame chain is cyclic or outside the stack");
            return GX_ERROR_FAILED;
        }
        gx_development_debug_call_stack_frame& caller =
            outResult->frames[outResult->frameCount];
        caller.depth = outResult->frameCount;
        caller.flags = GX_DEVELOPMENT_DEBUG_CALL_STACK_FRAME_VALIDATED |
            GX_DEVELOPMENT_DEBUG_CALL_STACK_FRAME_POINTER_VALID |
            GX_DEVELOPMENT_DEBUG_CALL_STACK_FRAME_RETURN_ADDRESS_VALID;
        caller.instructionPointer = callerInstructionPointer;
        caller.framePointer = nextRbp;
        const bool exactSource = callSiteMapped ||
            resolve_debug_mapping_at_address(s_operation, currentReturnAddress,
                                             &callerMapping, nullptr);
        if (!set_call_stack_mapping(&caller, callerMapping, exactSource)) {
            set_call_stack_error(outResult,
                                 GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_UNSUPPORTED_FRAME,
                                 "NativeElf caller function identity is too long");
            return GX_ERROR_UNSUPPORTED;
        }
        if (callSiteMapped) caller.flags |= GX_DEVELOPMENT_DEBUG_CALL_STACK_FRAME_CALL_SITE_MAPPED;
        if (equal_text(callerMapping.functionName, "gx_main")) {
            caller.flags |= GX_DEVELOPMENT_DEBUG_CALL_STACK_FRAME_ROOT;
            caller.returnAddress = 0;
            seen[outResult->frameCount] = nextRbp;
            ++outResult->frameCount;
            outResult->status = GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_SUCCESS;
            return GX_OK;
        }

        uint64_t candidateSavedRbp = 0;
        uint64_t candidateReturnAddress = 0;
        if (!debug_read_stack_u64(s_operation, nextRbp, &candidateSavedRbp) ||
            !debug_read_stack_u64(s_operation, nextRbp + sizeof(uint64_t),
                                  &candidateReturnAddress) ||
            !step_out_caller_link_valid(nextRbp, candidateSavedRbp,
                                        candidateReturnAddress,
                                        runtime->stackBase, stackHigh,
                                        runtime->imageBase, runtime->imageSize)) {
            set_call_stack_error(outResult, candidateReturnAddress == 0
                                     ? GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_INVALID_FRAME
                                     : GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_INVALID_RETURN_ADDRESS,
                                 "NativeElf caller frame link is invalid");
            return GX_ERROR_FAILED;
        }
        caller.returnAddress = candidateReturnAddress;
        seen[outResult->frameCount] = nextRbp;
        ++outResult->frameCount;
        if (!copy_text(currentFunction, sizeof(currentFunction), callerMapping.functionName)) {
            set_call_stack_error(outResult,
                                 GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_UNSUPPORTED_FRAME,
                                 "NativeElf caller function identity is invalid");
            return GX_ERROR_UNSUPPORTED;
        }
        currentReturnAddress = candidateReturnAddress;
        nextRbp = candidateSavedRbp;
    }
}

gx_result inspect_variables(const gx_development_debug_request& request,
                            gx_development_debug_variables* outResult)
{
    if (!outResult) return GX_ERROR_INVALID_ARGUMENT;
    clear_debug_variables(outResult);
    if (request.size < GX_DEVELOPMENT_DEBUG_REQUEST_LEGACY_BYTES ||
        request.version != GX_DEVELOPMENT_DEBUG_API_VERSION ||
        request.command != GX_DEVELOPMENT_DEBUG_INSPECT_VARIABLES) {
        set_debug_variables_error(outResult, GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_REJECTED,
                                  "NativeElf variable inspection request version or command is invalid");
        return GX_ERROR_INVALID_ARGUMENT;
    }
    if (!decode(request.handle)) {
        set_debug_variables_error(outResult, GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_STALE,
                                  "NativeElf variable inspection handle is stale");
        return GX_ERROR_FAILED;
    }
    if (!s_operation.debugControlled) {
        set_debug_variables_error(outResult, GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_REJECTED,
                                  "Run generation is not debugger-controlled");
        return GX_ERROR_UNSUPPORTED;
    }
    if (request.sessionGeneration == 0 || request.nativeRuntimeId == 0 ||
        request.threadId == 0 || request.stopGeneration == 0 ||
        !request.artifactSha256 || !equal_text(request.artifactSha256, s_operation.artifactSha256) ||
        !debug_request_identity_matches(s_operation, request)) {
        set_debug_variables_error(outResult,
                                  ((request.sessionGeneration != 0 &&
                                      request.sessionGeneration != s_operation.registrationGeneration) ||
                                   (request.stopGeneration != 0 &&
                                      request.stopGeneration != s_operation.debugStopGeneration))
                                      ? GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_STALE
                                      : GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_REJECTED,
                                  "NativeElf variable inspection session identity is stale");
        return GX_ERROR_FAILED;
    }
    const uint64_t selectedFrameIndex = request.auxiliaryAddress;
    if (selectedFrameIndex >= GX_DEVELOPMENT_DEBUG_MAX_CALL_STACK_FRAMES) {
        clear_debug_variables(outResult);
        outResult->handle = request.handle;
        outResult->sessionGeneration = request.sessionGeneration;
        set_debug_variables_error(outResult, GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_INVALID_FRAME,
                                  "selected Call Stack frame index is outside the bounded capacity");
        return GX_ERROR_INVALID_ARGUMENT;
    }
    gx_development_debug_request callStackRequest = request;
    callStackRequest.command = GX_DEVELOPMENT_DEBUG_CALL_STACK;
    callStackRequest.auxiliaryAddress = 0;
    gx_development_debug_call_stack callStack = {};
    const gx_result callStackResult = call_stack(callStackRequest, &callStack);
    if (callStackResult != GX_OK ||
        (callStack.status != GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_SUCCESS &&
         callStack.status != GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_TRUNCATED)) {
        const uint32_t status = callStack.status == GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_STALE
            ? GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_STALE
            : callStack.status == GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_NO_PAUSED_CONTEXT
                ? GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_NO_PAUSED_CONTEXT
                : callStack.status == GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_INVALID_FRAME
                    ? GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_INVALID_FRAME
                    : GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_UNSUPPORTED;
        clear_debug_variables(outResult);
        outResult->handle = request.handle;
        outResult->sessionGeneration = request.sessionGeneration;
        set_debug_variables_error(outResult, status, callStack.errorMessage[0] != '\0'
            ? callStack.errorMessage : "validated Call Stack is unavailable");
        return callStackResult == GX_OK ? GX_ERROR_FAILED : callStackResult;
    }
    if (selectedFrameIndex >= callStack.frameCount) {
        clear_debug_variables(outResult);
        outResult->handle = request.handle;
        outResult->sessionGeneration = request.sessionGeneration;
        set_debug_variables_error(outResult, GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_INVALID_FRAME,
                                  "selected Call Stack frame index does not exist in the current pause");
        return GX_ERROR_INVALID_ARGUMENT;
    }
    const gx_development_debug_call_stack_frame selectedFrame =
        callStack.frames[selectedFrameIndex];
    if ((selectedFrame.flags & GX_DEVELOPMENT_DEBUG_CALL_STACK_FRAME_VALIDATED) == 0 ||
        selectedFrame.framePointer == 0 || selectedFrame.instructionPointer == 0) {
        clear_debug_variables(outResult);
        outResult->handle = request.handle;
        outResult->sessionGeneration = request.sessionGeneration;
        set_debug_variables_error(outResult, GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_INVALID_FRAME,
                                  "selected Call Stack frame is not validated");
        return GX_ERROR_FAILED;
    }
    if (s_operation.state != GX_DEVELOPMENT_RUN_PAUSED ||
        (!s_operation.debugBreakpointHit && !s_operation.debugStepTrapObserved) ||
        s_operation.debugStepActive || s_operation.debugSourceStepActive ||
        s_operation.debugStepOverActive || s_operation.debugStepOutActive ||
        !s_operation.debugContext || s_operation.debugContext->cs != 0x08) {
        set_debug_variables_error(outResult,
                                  GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_NO_PAUSED_CONTEXT,
                                  "NativeElf target has no complete paused user context");
        return GX_ERROR_BUSY;
    }
    const NativeAppExecutionContext* runtime = native_elf_runtime_context();
    if (!runtime || runtime->state != NativeAppExecutionState::Running ||
        runtime->stackBase == 0 || runtime->stackSize == 0 ||
        runtime->stackBase > ~static_cast<uint64_t>(0) - runtime->stackSize) {
        set_debug_variables_error(outResult,
                                  GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_NO_PAUSED_CONTEXT,
                                  "NativeElf runtime stack identity is unavailable");
        return GX_ERROR_FAILED;
    }
    const NativeElfDebugTrap::BreakpointContext& context = *s_operation.debugContext;
    compiler::ResolvedSourceMapping topMapping = {};
    if (!debug_code_address(s_operation, context.rip) ||
        !resolve_debug_mapping_at_address(s_operation, context.rip, &topMapping, nullptr) ||
        topMapping.functionName[0] == '\0') {
        set_debug_variables_error(outResult,
                                  GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_UNSUPPORTED,
                                  "NativeElf paused RIP has no trustworthy user function");
        return GX_ERROR_UNSUPPORTED;
    }
    const uint64_t stackHigh = runtime->stackBase + runtime->stackSize;
    if (!step_out_frame_shape_valid(context.rsp, context.rbp,
                                    runtime->stackBase, stackHigh)) {
        set_debug_variables_error(outResult, GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_INVALID_FRAME,
                                  "NativeElf paused frame pointer shape is invalid");
        return GX_ERROR_FAILED;
    }
    outResult->handle = s_operation.handle;
    outResult->nativeRuntimeId = s_operation.registrationGeneration;
    outResult->threadId = 1;
    outResult->sessionGeneration = s_operation.registrationGeneration;
    outResult->stopGeneration = s_operation.debugStopGeneration;
    const uint64_t selectedRip = selectedFrameIndex == 0
        ? context.rip : selectedFrame.instructionPointer;
    const uint64_t selectedRbp = selectedFrame.framePointer;
    if (selectedFrameIndex != 0 &&
        (selectedFrame.flags & GX_DEVELOPMENT_DEBUG_CALL_STACK_FRAME_CALL_SITE_MAPPED) == 0) {
        set_debug_variables_error(outResult, GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_UNSUPPORTED,
                                  "caller frame has no authoritative logical call-site position");
        return GX_ERROR_UNSUPPORTED;
    }
    compiler::ResolvedSourceMapping selectedMapping = {};
    if (!resolve_debug_mapping_at_address(s_operation, selectedRip, &selectedMapping, nullptr) ||
        selectedMapping.functionName[0] == '\0' ||
        !equal_text(selectedMapping.functionName, selectedFrame.functionName)) {
        set_debug_variables_error(outResult, GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_UNSUPPORTED,
                                  "selected frame function identity does not match source metadata");
        return GX_ERROR_UNSUPPORTED;
    }
    if (selectedFrameIndex == 0) {
        if (!step_out_frame_shape_valid(context.rsp, selectedRbp,
                                        runtime->stackBase, stackHigh)) {
            set_debug_variables_error(outResult,
                                      GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_INVALID_FRAME,
                                      "NativeElf paused frame pointer shape is invalid");
            return GX_ERROR_FAILED;
        }
    } else if (!native_elf_frame_pointer_valid(selectedRbp, runtime->stackBase, stackHigh)) {
        set_debug_variables_error(outResult,
                                  GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_INVALID_FRAME,
                                  "selected caller frame pointer is outside the owned stack");
        return GX_ERROR_FAILED;
    }
    outResult->instructionPointer = selectedRip;
    outResult->framePointer = selectedRbp;
    outResult->stackLow = runtime->stackBase;
    outResult->stackHigh = stackHigh;
    if (!copy_text(outResult->functionName, sizeof(outResult->functionName), selectedMapping.functionName) ||
        !copy_text(outResult->sourcePath, sizeof(outResult->sourcePath), selectedMapping.sourcePath)) {
        set_debug_variables_error(outResult, GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_UNSUPPORTED,
                                  "NativeElf top frame source identity is too long");
        return GX_ERROR_UNSUPPORTED;
    }

    compiler::ResolvedDebugVariable variables[GX_DEVELOPMENT_DEBUG_MAX_VARIABLES] = {};
    uint32_t variableCount = 0;
    uint32_t truncated = 0;
    const char* resolverError = nullptr;
    if (!compiler::resolve_bootstrap_debug_variables_at_address(
            s_artifact, static_cast<uint32_t>(s_operation.artifactSize),
            runtime->imageBase, s_operation.debugCodeFileOffset,
            s_operation.debugCodeBytes, selectedRip, selectedMapping.functionName,
            variables, GX_DEVELOPMENT_DEBUG_MAX_VARIABLES, &variableCount,
            &truncated, &resolverError)) {
        set_debug_variables_error(outResult, GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_UNSUPPORTED,
                                  resolverError ? resolverError : "no debug variables are live");
        return GX_ERROR_UNSUPPORTED;
    }
    for (uint32_t index = 0; index < variableCount; ++index) {
        const compiler::ResolvedDebugVariable& source = variables[index];
        gx_development_debug_variable& target = outResult->variables[index];
        if (!copy_text(target.name, sizeof(target.name), source.name)) {
            set_debug_variables_error(outResult, GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_UNSUPPORTED,
                                      "NativeElf debug variable name is too long");
            return GX_ERROR_UNSUPPORTED;
        }
        target.kind = source.kind == compiler::DebugVariableKind::Parameter
            ? GX_DEVELOPMENT_DEBUG_VARIABLE_KIND_ARGUMENT
            : GX_DEVELOPMENT_DEBUG_VARIABLE_KIND_LOCAL;
        target.type = source.type == compiler::DebugVariableTypeKind::Pointer
            ? GX_DEVELOPMENT_DEBUG_VARIABLE_TYPE_POINTER
            : GX_DEVELOPMENT_DEBUG_VARIABLE_TYPE_SIGNED_INT32;
        target.location = GX_DEVELOPMENT_DEBUG_VARIABLE_LOCATION_RBP_RELATIVE;
        target.flags = GX_DEVELOPMENT_DEBUG_VARIABLE_VALIDATED |
            GX_DEVELOPMENT_DEBUG_VARIABLE_LIVE |
            GX_DEVELOPMENT_DEBUG_VARIABLE_INITIALIZED |
            GX_DEVELOPMENT_DEBUG_VARIABLE_STABLE_FRAME_SLOT;
        target.sizeBytes = source.sizeBytes;
        target.declarationLine = source.declaration.line;
        target.declarationColumn = source.declaration.column;
        target.frameOffset = source.frameOffset;
        const uint64_t magnitude = static_cast<uint64_t>(
            -(static_cast<int64_t>(source.frameOffset)));
        if (source.frameOffset >= 0 || selectedRbp < magnitude ||
            !step_out_stack_range_contains(runtime->stackBase, stackHigh,
                                            selectedRbp - magnitude, source.sizeBytes)) {
            set_debug_variables_error(outResult,
                                      GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_INVALID_LOCATION,
                                      "NativeElf debug variable location is outside the owned frame");
            return GX_ERROR_FAILED;
        }
        uint8_t bytes[sizeof(uint64_t)] = {};
        if (!debug_read_stack_bytes(s_operation, selectedRbp - magnitude,
                                    source.sizeBytes, bytes)) {
            set_debug_variables_error(outResult,
                                      GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_INVALID_LOCATION,
                                      "NativeElf debug variable value could not be read");
            return GX_ERROR_FAILED;
        }
        uint64_t raw = 0;
        for (uint32_t byte = 0; byte < source.sizeBytes; ++byte)
            raw |= static_cast<uint64_t>(bytes[byte]) << (byte * 8);
        target.rawValue = raw;
        target.unsignedValue = source.type == compiler::DebugVariableTypeKind::Pointer
            ? raw : static_cast<uint64_t>(static_cast<uint32_t>(raw));
        target.signedValue = source.type == compiler::DebugVariableTypeKind::Pointer
            ? static_cast<int64_t>(raw)
            : static_cast<int64_t>(static_cast<int32_t>(static_cast<uint32_t>(raw)));
        target.availability = GX_DEVELOPMENT_DEBUG_VARIABLE_AVAILABILITY_AVAILABLE;
        target.flags |= GX_DEVELOPMENT_DEBUG_VARIABLE_VALUE_VALID;
    }
    outResult->variableCount = variableCount;
    outResult->truncated = truncated;
    outResult->status = truncated ? GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_TRUNCATED
                                  : GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_SUCCESS;
    return GX_OK;
}

gx_result evaluate_expression(const gx_development_debug_request& request,
                              gx_development_debug_expression* outResult)
{
    if (!outResult) return GX_ERROR_INVALID_ARGUMENT;
    clear_debug_expression(outResult);
    outResult->handle = request.handle;
    outResult->sessionGeneration = request.sessionGeneration;
    outResult->selectedFrameIndex = request.auxiliaryAddress;
    const size_t requestBytes =
        offsetof(gx_development_debug_request, expression) + sizeof(request.expression);
    if (request.size < requestBytes ||
        request.version != GX_DEVELOPMENT_DEBUG_API_VERSION ||
        request.command != GX_DEVELOPMENT_DEBUG_EVALUATE_EXPRESSION ||
        request.expression == nullptr) {
        set_debug_expression_error(outResult,
                                   GX_DEVELOPMENT_DEBUG_EXPRESSION_STATUS_REJECTED,
                                   GX_DEVELOPMENT_DEBUG_EXPRESSION_ERROR_INVALID_REQUEST,
                                   "NativeElf watch expression request is invalid");
        return GX_ERROR_INVALID_ARGUMENT;
    }
    if (!decode(request.handle)) {
        set_debug_expression_error(outResult,
                                   GX_DEVELOPMENT_DEBUG_EXPRESSION_STATUS_STALE,
                                   GX_DEVELOPMENT_DEBUG_EXPRESSION_ERROR_STALE_GENERATION,
                                   "NativeElf watch expression handle is stale");
        return GX_ERROR_FAILED;
    }
    if (!s_operation.debugControlled) {
        set_debug_expression_error(outResult,
                                   GX_DEVELOPMENT_DEBUG_EXPRESSION_STATUS_REJECTED,
                                   GX_DEVELOPMENT_DEBUG_EXPRESSION_ERROR_INVALID_REQUEST,
                                   "Run generation is not debugger-controlled");
        return GX_ERROR_UNSUPPORTED;
    }
    if (!request.artifactSha256 ||
        !equal_text(request.artifactSha256, s_operation.artifactSha256) ||
        !debug_request_identity_matches(s_operation, request)) {
        set_debug_expression_error(outResult,
                                   (request.sessionGeneration != 0 &&
                                    request.sessionGeneration != s_operation.registrationGeneration) ||
                                   (request.stopGeneration != 0 &&
                                    request.stopGeneration != s_operation.debugStopGeneration)
                                       ? GX_DEVELOPMENT_DEBUG_EXPRESSION_STATUS_STALE
                                       : GX_DEVELOPMENT_DEBUG_EXPRESSION_STATUS_REJECTED,
                                   (request.sessionGeneration != 0 &&
                                    request.sessionGeneration != s_operation.registrationGeneration) ||
                                   (request.stopGeneration != 0 &&
                                    request.stopGeneration != s_operation.debugStopGeneration)
                                       ? GX_DEVELOPMENT_DEBUG_EXPRESSION_ERROR_STALE_GENERATION
                                       : GX_DEVELOPMENT_DEBUG_EXPRESSION_ERROR_INVALID_REQUEST,
                                   "NativeElf watch expression session identity is stale");
        return GX_ERROR_FAILED;
    }
    if (request.auxiliaryAddress >= GX_DEVELOPMENT_DEBUG_MAX_CALL_STACK_FRAMES) {
        set_debug_expression_error(outResult,
                                   GX_DEVELOPMENT_DEBUG_EXPRESSION_STATUS_INVALID_FRAME,
                                   GX_DEVELOPMENT_DEBUG_EXPRESSION_ERROR_INVALID_FRAME,
                                   "selected Call Stack frame index is outside the bounded capacity");
        return GX_ERROR_INVALID_ARGUMENT;
    }
    if (s_operation.state != GX_DEVELOPMENT_RUN_PAUSED ||
        (!s_operation.debugBreakpointHit && !s_operation.debugStepTrapObserved) ||
        s_operation.debugStepActive || s_operation.debugSourceStepActive ||
        s_operation.debugStepOverActive || s_operation.debugStepOutActive ||
        !s_operation.debugContext || s_operation.debugContext->cs != 0x08) {
        set_debug_expression_error(outResult,
                                   GX_DEVELOPMENT_DEBUG_EXPRESSION_STATUS_NO_PAUSED_CONTEXT,
                                   GX_DEVELOPMENT_DEBUG_EXPRESSION_ERROR_TARGET_NOT_PAUSED,
                                   "NativeElf target has no complete paused user context");
        return GX_ERROR_BUSY;
    }

    gx_development_debug_request callStackRequest = request;
    callStackRequest.command = GX_DEVELOPMENT_DEBUG_CALL_STACK;
    callStackRequest.auxiliaryAddress = 0;
    gx_development_debug_call_stack callStack = {};
    const gx_result callStackCode = call_stack(callStackRequest, &callStack);
    if (callStackCode != GX_OK ||
        (callStack.status != GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_SUCCESS &&
         callStack.status != GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_TRUNCATED)) {
        const uint32_t status = callStack.status == GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_STALE
            ? GX_DEVELOPMENT_DEBUG_EXPRESSION_STATUS_STALE
            : callStack.status == GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_NO_PAUSED_CONTEXT
                ? GX_DEVELOPMENT_DEBUG_EXPRESSION_STATUS_NO_PAUSED_CONTEXT
                : callStack.status == GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_INVALID_FRAME
                    ? GX_DEVELOPMENT_DEBUG_EXPRESSION_STATUS_INVALID_FRAME
                    : GX_DEVELOPMENT_DEBUG_EXPRESSION_STATUS_FAILED;
        const uint32_t category = callStack.status == GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_STALE
            ? GX_DEVELOPMENT_DEBUG_EXPRESSION_ERROR_STALE_GENERATION
            : callStack.status == GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_NO_PAUSED_CONTEXT
                ? GX_DEVELOPMENT_DEBUG_EXPRESSION_ERROR_TARGET_NOT_PAUSED
                : callStack.status == GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_INVALID_FRAME
                    ? GX_DEVELOPMENT_DEBUG_EXPRESSION_ERROR_INVALID_FRAME
                    : GX_DEVELOPMENT_DEBUG_EXPRESSION_ERROR_VARIABLE_METADATA_UNAVAILABLE;
        set_debug_expression_error(outResult, status, category, callStack.errorMessage);
        return callStackCode == GX_OK ? GX_ERROR_FAILED : callStackCode;
    }
    if (request.auxiliaryAddress >= callStack.frameCount) {
        set_debug_expression_error(outResult,
                                   GX_DEVELOPMENT_DEBUG_EXPRESSION_STATUS_INVALID_FRAME,
                                   GX_DEVELOPMENT_DEBUG_EXPRESSION_ERROR_INVALID_FRAME,
                                   "selected Call Stack frame does not exist in the current pause");
        return GX_ERROR_INVALID_ARGUMENT;
    }
    const gx_development_debug_call_stack_frame selectedFrame =
        callStack.frames[request.auxiliaryAddress];
    if ((selectedFrame.flags & GX_DEVELOPMENT_DEBUG_CALL_STACK_FRAME_VALIDATED) == 0 ||
        selectedFrame.instructionPointer == 0 || selectedFrame.framePointer == 0) {
        set_debug_expression_error(outResult,
                                   GX_DEVELOPMENT_DEBUG_EXPRESSION_STATUS_INVALID_FRAME,
                                   GX_DEVELOPMENT_DEBUG_EXPRESSION_ERROR_INVALID_FRAME,
                                   "selected Call Stack frame is not validated");
        return GX_ERROR_FAILED;
    }

    gx_development_debug_variables variables = {};
    gx_development_debug_request variablesRequest = request;
    variablesRequest.command = GX_DEVELOPMENT_DEBUG_INSPECT_VARIABLES;
    const gx_result variablesCode = inspect_variables(variablesRequest, &variables);
    if (variablesCode != GX_OK &&
        variables.status != GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_UNSUPPORTED) {
        const uint32_t status = variables.status == GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_STALE
            ? GX_DEVELOPMENT_DEBUG_EXPRESSION_STATUS_STALE
            : variables.status == GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_NO_PAUSED_CONTEXT
                ? GX_DEVELOPMENT_DEBUG_EXPRESSION_STATUS_NO_PAUSED_CONTEXT
                : variables.status == GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_INVALID_FRAME
                    ? GX_DEVELOPMENT_DEBUG_EXPRESSION_STATUS_INVALID_FRAME
                    : GX_DEVELOPMENT_DEBUG_EXPRESSION_STATUS_FAILED;
        const uint32_t category = variables.status == GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_STALE
            ? GX_DEVELOPMENT_DEBUG_EXPRESSION_ERROR_STALE_GENERATION
            : variables.status == GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_NO_PAUSED_CONTEXT
                ? GX_DEVELOPMENT_DEBUG_EXPRESSION_ERROR_TARGET_NOT_PAUSED
                : variables.status == GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_INVALID_FRAME
                    ? GX_DEVELOPMENT_DEBUG_EXPRESSION_ERROR_INVALID_FRAME
                    : GX_DEVELOPMENT_DEBUG_EXPRESSION_ERROR_INVALID_REQUEST;
        set_debug_expression_error(outResult, status, category, variables.errorMessage);
        return variablesCode;
    }
    if (variablesCode != GX_OK) {
        variables = {};
        variables.size = sizeof(variables);
        variables.version = GX_DEVELOPMENT_DEBUG_API_VERSION;
        variables.status = GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_SUCCESS;
        variables.handle = request.handle;
        variables.nativeRuntimeId = callStack.nativeRuntimeId;
        variables.threadId = callStack.threadId;
        variables.sessionGeneration = callStack.sessionGeneration;
        variables.stopGeneration = callStack.stopGeneration;
        variables.instructionPointer = selectedFrame.instructionPointer;
        variables.framePointer = selectedFrame.framePointer;
        copy_text(variables.functionName, sizeof(variables.functionName), selectedFrame.functionName);
        copy_text(variables.sourcePath, sizeof(variables.sourcePath), selectedFrame.sourcePath);
    }

    outResult->processId = callStack.processId;
    outResult->nativeRuntimeId = callStack.nativeRuntimeId;
    outResult->threadId = callStack.threadId;
    outResult->stopGeneration = callStack.stopGeneration;
    outResult->instructionPointer = selectedFrame.instructionPointer;
    outResult->framePointer = selectedFrame.framePointer;
    copy_text(outResult->functionName, sizeof(outResult->functionName), selectedFrame.functionName);
    copy_text(outResult->sourcePath, sizeof(outResult->sourcePath), selectedFrame.sourcePath);

    NativeDebugWatchMetadataContext metadata = {};
    metadata.address = selectedFrame.instructionPointer;
    copy_text(metadata.functionName, sizeof(metadata.functionName), selectedFrame.functionName);
    NativeDebugWatchFrame frame = {};
    frame.paused = true;
    frame.frameIndex = static_cast<uint32_t>(request.auxiliaryAddress);
    frame.sessionGeneration = request.sessionGeneration;
    frame.stopGeneration = request.stopGeneration;
    frame.instructionPointer = selectedFrame.instructionPointer;
    frame.framePointer = selectedFrame.framePointer;
    frame.variables = &variables;
    frame.variableMetadataAvailable = s_operation.artifactSize != 0;
    frame.resolverContext = &metadata;
    frame.resolveIdentifier = resolve_native_debug_watch_identifier;
    NativeDebugWatchResult watch = {};
    native_debug_watch_evaluate(request.expression, frame, &watch);
    if (watch.status != NativeDebugWatchStatus::Success) {
        const uint32_t status = watch.status == NativeDebugWatchStatus::StaleGeneration
            ? GX_DEVELOPMENT_DEBUG_EXPRESSION_STATUS_STALE
            : watch.status == NativeDebugWatchStatus::InvalidSelectedFrame
                ? GX_DEVELOPMENT_DEBUG_EXPRESSION_STATUS_INVALID_FRAME
                : watch.status == NativeDebugWatchStatus::Running
                    ? GX_DEVELOPMENT_DEBUG_EXPRESSION_STATUS_NO_PAUSED_CONTEXT
                    : GX_DEVELOPMENT_DEBUG_EXPRESSION_STATUS_FAILED;
        uint32_t category = GX_DEVELOPMENT_DEBUG_EXPRESSION_ERROR_INVALID_REQUEST;
        switch (watch.status) {
        case NativeDebugWatchStatus::SyntaxError: category = GX_DEVELOPMENT_DEBUG_EXPRESSION_ERROR_SYNTAX; break;
        case NativeDebugWatchStatus::UnknownIdentifier: category = GX_DEVELOPMENT_DEBUG_EXPRESSION_ERROR_UNKNOWN_IDENTIFIER; break;
        case NativeDebugWatchStatus::NotLive: category = GX_DEVELOPMENT_DEBUG_EXPRESSION_ERROR_VARIABLE_NOT_LIVE; break;
        case NativeDebugWatchStatus::UnsupportedType: category = GX_DEVELOPMENT_DEBUG_EXPRESSION_ERROR_UNSUPPORTED_TYPE; break;
        case NativeDebugWatchStatus::UnsupportedOperator: category = GX_DEVELOPMENT_DEBUG_EXPRESSION_ERROR_UNSUPPORTED_OPERATOR; break;
        case NativeDebugWatchStatus::DivideByZero: category = GX_DEVELOPMENT_DEBUG_EXPRESSION_ERROR_DIVIDE_BY_ZERO; break;
        case NativeDebugWatchStatus::Overflow: category = GX_DEVELOPMENT_DEBUG_EXPRESSION_ERROR_OVERFLOW; break;
        case NativeDebugWatchStatus::InvalidSelectedFrame: category = GX_DEVELOPMENT_DEBUG_EXPRESSION_ERROR_INVALID_FRAME; break;
        case NativeDebugWatchStatus::StaleGeneration: category = GX_DEVELOPMENT_DEBUG_EXPRESSION_ERROR_STALE_GENERATION; break;
        case NativeDebugWatchStatus::Running: category = GX_DEVELOPMENT_DEBUG_EXPRESSION_ERROR_TARGET_NOT_PAUSED; break;
        case NativeDebugWatchStatus::TooComplex: category = GX_DEVELOPMENT_DEBUG_EXPRESSION_ERROR_COMPLEXITY_LIMIT; break;
        case NativeDebugWatchStatus::ExpressionTooLong: category = GX_DEVELOPMENT_DEBUG_EXPRESSION_ERROR_EXPRESSION_TOO_LONG; break;
        case NativeDebugWatchStatus::MetadataUnavailable: category = GX_DEVELOPMENT_DEBUG_EXPRESSION_ERROR_VARIABLE_METADATA_UNAVAILABLE; break;
        case NativeDebugWatchStatus::Success: break;
        }
        set_debug_expression_error(outResult, status, category, watch.diagnostic,
                                   watch.diagnosticOffset);
        return GX_ERROR_FAILED;
    }
    outResult->status = GX_DEVELOPMENT_DEBUG_EXPRESSION_STATUS_SUCCESS;
    outResult->resultKind = watch.type == NativeDebugWatchValueType::Pointer
        ? GX_DEVELOPMENT_DEBUG_EXPRESSION_RESULT_POINTER
        : GX_DEVELOPMENT_DEBUG_EXPRESSION_RESULT_SIGNED_INTEGER;
    outResult->signedValue = watch.signedValue;
    outResult->unsignedValue = watch.unsignedValue;
    outResult->pointerValue = watch.type == NativeDebugWatchValueType::Pointer
        ? watch.unsignedValue : 0;
    return GX_OK;
}

gx_result debug(const gx_development_debug_request& request,
                gx_development_debug_snapshot* outSnapshot)
{
    if (!outSnapshot) return GX_ERROR_INVALID_ARGUMENT;
    clear_debug_snapshot(outSnapshot);
    if (request.size < GX_DEVELOPMENT_DEBUG_REQUEST_LEGACY_BYTES ||
        request.version != GX_DEVELOPMENT_DEBUG_API_VERSION) {
        set_debug_error(outSnapshot, "NativeElf debug request version or size is invalid");
        return GX_ERROR_INVALID_ARGUMENT;
    }
    if (!decode(request.handle)) {
        set_debug_error(outSnapshot, "NativeElf debug handle is stale");
        return GX_ERROR_FAILED;
    }
    if (!s_operation.debugControlled) {
        set_debug_error(outSnapshot, "Run generation is not debugger-controlled");
        return GX_ERROR_UNSUPPORTED;
    }
    if (!debug_request_identity_matches(s_operation, request)) {
        set_debug_error(outSnapshot, "NativeElf debug session identity is stale");
        return GX_ERROR_FAILED;
    }

    const bool debugStepWasObserved = s_operation.debugStepTrapObserved;
    switch (request.command) {
    case GX_DEVELOPMENT_DEBUG_POLL:
        if (s_operation.state == GX_DEVELOPMENT_RUN_PAUSED) {
            *outSnapshot = s_operation.debugSnapshot;
        } else if (s_operation.state == GX_DEVELOPMENT_RUN_STEPPING) {
            set_debug_ready_snapshot(s_operation, outSnapshot);
            outSnapshot->status = GX_DEVELOPMENT_DEBUG_STATUS_SINGLE_STEP_PENDING;
            outSnapshot->singleStepKind = GX_DEVELOPMENT_DEBUG_SINGLE_STEP_USER_SOURCE;
            outSnapshot->targetAddress = s_operation.debugBreakpointAddress;
            outSnapshot->rflagsBeforeStep = s_operation.debugStepRflagsBefore;
            outSnapshot->rflagsWithTrapFlag = s_operation.debugStepRflagsWithTrapFlag;
            set_source_step_metadata(s_operation, outSnapshot);
            if (s_operation.debugStepOverActive)
                set_source_step_over_metadata(s_operation, outSnapshot);
            if (s_operation.debugStepOutActive || s_operation.debugStepOutReturnAddress != 0)
                set_source_step_out_metadata(s_operation, outSnapshot);
        } else if (s_operation.state == GX_DEVELOPMENT_RUN_RUNNING ||
                   s_operation.state == GX_DEVELOPMENT_RUN_LAUNCHING ||
                   s_operation.state == GX_DEVELOPMENT_RUN_REGISTERED) {
            set_debug_ready_snapshot(s_operation, outSnapshot);
        } else {
            set_debug_error(outSnapshot, "NativeElf debug session is no longer active");
            return GX_ERROR_FAILED;
        }
        return GX_OK;

    case GX_DEVELOPMENT_DEBUG_RESUME:
        if (s_operation.state != GX_DEVELOPMENT_RUN_PAUSED ||
            (!s_operation.debugBreakpointInstalled && !s_operation.debugStepTrapObserved)) {
            set_debug_error(outSnapshot, "NativeElf target is not paused at a resumable debug stop");
            return GX_ERROR_BUSY;
        }
        if (s_operation.debugBreakpointInstalled && !restore_debug_entry_breakpoint()) {
            set_debug_error(outSnapshot, "NativeElf breakpoint could not be restored");
            return GX_ERROR_FAILED;
        }
        if (s_operation.debugStepOverReturnBreakpointInstalled &&
            !restore_debug_entry_breakpoint()) {
            set_debug_error(outSnapshot,
                            "NativeElf Step Over return breakpoint could not be restored");
            return GX_ERROR_FAILED;
        }
        if (s_operation.debugStepOutReturnBreakpointInstalled &&
            !restore_debug_entry_breakpoint()) {
            set_debug_error(outSnapshot,
                            "NativeElf Step Out return breakpoint could not be restored for cancel");
            return GX_ERROR_FAILED;
        }
        if (s_operation.debugContext) s_operation.debugContext->rflags &= ~kAmd64TrapFlag;
        s_operation.debugBreakpointInstalled = false;
        s_operation.debugBreakpointHit = false;
        s_operation.debugStepActive = false;
        s_operation.debugStepTrapObserved = false;
        s_operation.debugSourceStepActive = false;
        step_over_clear_state(s_operation);
        step_out_clear_state(s_operation);
        s_operation.state = GX_DEVELOPMENT_RUN_RUNNING;
        set_debug_ready_snapshot(s_operation, outSnapshot);
        serial::puts(debugStepWasObserved
                         ? "DEVELOPER_STUDIO_PHASE28B_RESUME\n"
                         : s_operation.debugSourceSelected
                             ? "DEVELOPER_STUDIO_PHASE28A_RESUME\n"
                             : "DEVELOPER_STUDIO_PHASE27Z_RESUME\n");
        if (!native_elf_scheduler_pump()) return GX_ERROR_FAILED;
        if (s_operation.state == GX_DEVELOPMENT_RUN_PAUSED)
            *outSnapshot = s_operation.debugSnapshot;
        return GX_OK;

    case GX_DEVELOPMENT_DEBUG_STEP_SOURCE_INTO:
        return source_step_into(outSnapshot);

    case GX_DEVELOPMENT_DEBUG_STEP_OVER_CALL:
    case GX_DEVELOPMENT_DEBUG_STEP_SOURCE_OVER:
        return source_step_over(outSnapshot);

    case GX_DEVELOPMENT_DEBUG_STEP_SOURCE_OUT:
        return source_step_out(outSnapshot);

    case GX_DEVELOPMENT_DEBUG_STEP_INSTRUCTION:
        if (s_operation.state != GX_DEVELOPMENT_RUN_PAUSED ||
            (!s_operation.debugBreakpointInstalled && !s_operation.debugStepTrapObserved) ||
            !s_operation.debugContext || s_operation.debugStepActive ||
            s_operation.debugContext->cs != 0x08) {
            set_debug_error(outSnapshot, "NativeElf Step Into requires a current Paused target context");
            return GX_ERROR_BUSY;
        }
        if (s_operation.debugContext->rflags & kAmd64TrapFlag) {
            set_debug_error(outSnapshot, "NativeElf target context already owns Trap Flag");
            return GX_ERROR_FAILED;
        }
        if (!begin_debug_instruction_step(s_operation, outSnapshot, false)) {
            set_debug_error(outSnapshot, "NativeElf breakpoint could not be restored before Step Into");
            return GX_ERROR_FAILED;
        }
        serial::puts("DEVELOPER_STUDIO_PHASE28B_STEP_REQUEST_PASS\n");
        serial_debug_hex("DEVELOPER_STUDIO_PHASE28B_TF_SET_PASS before=0x",
                         s_operation.debugStepRflagsBefore);
        serial_debug_hex(" armed=0x", s_operation.debugStepRflagsWithTrapFlag);
        serial::putc('\n');
        if (!complete_debug_instruction_step(s_operation, outSnapshot)) {
            if (s_operation.debugContext) s_operation.debugContext->rflags &= ~kAmd64TrapFlag;
            s_operation.debugStepActive = false;
            set_debug_error(outSnapshot, "NativeElf Step Into could not resume the target");
            return GX_ERROR_FAILED;
        }
        serial::puts("DEVELOPER_STUDIO_PHASE28B_PAUSED_AFTER_STEP_PASS\n");
        serial::puts("DEVELOPER_STUDIO_PHASE28B_TF_CLEAR_PASS\n");
        return GX_OK;

    case GX_DEVELOPMENT_DEBUG_CANCEL_EXECUTION:
        if (s_operation.state != GX_DEVELOPMENT_RUN_PAUSED ||
            (!s_operation.debugBreakpointInstalled && !s_operation.debugStepTrapObserved)) {
            set_debug_error(outSnapshot, "NativeElf debug session is not paused");
            return GX_ERROR_BUSY;
        }
        if (s_operation.debugBreakpointInstalled && !restore_debug_entry_breakpoint()) {
            set_debug_error(outSnapshot, "NativeElf breakpoint could not be restored for cancel");
            return GX_ERROR_FAILED;
        }
        if (s_operation.debugStepOverReturnBreakpointInstalled &&
            !restore_debug_entry_breakpoint()) {
            set_debug_error(outSnapshot,
                            "NativeElf Step Over return breakpoint could not be restored for cancel");
            return GX_ERROR_FAILED;
        }
        if (s_operation.debugStepOutReturnBreakpointInstalled &&
            !restore_debug_entry_breakpoint()) {
            set_debug_error(outSnapshot,
                            "NativeElf Step Out return breakpoint could not be restored for cancel");
            return GX_ERROR_FAILED;
        }
        if (s_operation.debugContext) s_operation.debugContext->rflags &= ~kAmd64TrapFlag;
        s_operation.debugBreakpointInstalled = false;
        s_operation.debugBreakpointHit = false;
        s_operation.debugStepActive = false;
        s_operation.debugSourceStepActive = false;
        step_over_clear_state(s_operation);
        step_out_clear_state(s_operation);
        s_operation.debugCancelRequested = true;
        s_operation.cancellationRequested = true;
        s_operation.closeRequested = true;
        s_operation.state = GX_DEVELOPMENT_RUN_CLOSING;
        serial::puts(s_operation.debugStepTrapObserved
                         ? "DEVELOPER_STUDIO_PHASE28B_CANCEL_AFTER_STEP\n"
                         : s_operation.debugSourceSelected
                             ? "DEVELOPER_STUDIO_PHASE28A_CANCEL_PAUSED\n"
                             : "DEVELOPER_STUDIO_PHASE27Z_CANCEL_PAUSED\n");
#if defined(__x86_64__)
        s_targetContext = make_debug_cancel_context();
        if (!s_targetContext) {
            set_debug_error(outSnapshot, "NativeElf paused cancel context could not be created");
            return GX_ERROR_FAILED;
        }
#endif
        if (!native_elf_scheduler_pump()) return GX_ERROR_FAILED;
        if (s_operation.state == GX_DEVELOPMENT_RUN_PAUSED) {
            set_debug_error(outSnapshot, "NativeElf paused cancel did not return to the owner");
            return GX_ERROR_FAILED;
        }
        set_debug_ready_snapshot(s_operation, outSnapshot);
        return GX_OK;

    default:
        set_debug_error(outSnapshot,
                        "NativeElf supports POLL, RESUME, source stepping, and paused CANCEL");
        return GX_ERROR_UNSUPPORTED;
    }
}

gx_result release(gx_development_run_handle handle) {
    if (!decode(handle)) return GX_ERROR_FAILED;
    if (s_operation.state != GX_DEVELOPMENT_RUN_COMPLETED &&
        s_operation.state != GX_DEVELOPMENT_RUN_FAILED &&
        s_operation.state != GX_DEVELOPMENT_RUN_CANCELLED) return GX_ERROR_BUSY;
    (void)unregister_application(s_operation);
    (void)restore_debug_entry_breakpoint();
    step_over_clear_state(s_operation);
    step_out_clear_state(s_operation);
    if (s_operation.debugControlled) NativeElfDebugTrap::uninstall();
#if defined(__x86_64__)
    s_schedulerActive = false;
    s_schedulerInTarget = false;
    s_schedulerTargetComplete = false;
    s_ownerContext = nullptr;
    s_targetContext = nullptr;
#endif
    s_operation = Operation();
    return GX_OK;
}

} // namespace NativeElfRunService
} // namespace native_elf
} // namespace kernel
