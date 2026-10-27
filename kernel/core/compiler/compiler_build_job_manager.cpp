// Fixed-capacity bare-metal Developer Studio build-job manager.

#include "compiler_build_job_manager.h"
#include "compiler_driver.h"

namespace kernel {
namespace compiler {
namespace BuildJobManager {
namespace {

static const uint32_t kBuildApiVersion = GX_BUILD_API_VERSION;
static const uint32_t kBuildSlotId = 1U;
static const uint32_t kTestErrorCode = GX_BUILD_ERROR_INTERNAL;
static_assert(kMaximumSources == COMPILER_MAX_TRANSLATION_UNITS,
              "build-job source membership must match the compiler bound");

static uint32_t bounded_length(const char* value, uint32_t capacity)
{
    if (!value) return capacity;
    uint32_t length = 0;
    while (length < capacity && value[length] != '\0') ++length;
    return length;
}

} // namespace

Manager::Manager(uint32_t generationSeed)
    : lock_(0), shuttingDown_(false), generationExhausted_(false),
      generation_(generationSeed), job_{}
{
}

void Manager::lock()
{
    while (__atomic_test_and_set(&lock_, __ATOMIC_ACQUIRE)) {
    }
}

void Manager::unlock()
{
    __atomic_clear(&lock_, __ATOMIC_RELEASE);
}

void Manager::clear_job()
{
    const uint32_t lastGeneration = job_.generation;
    uint8_t* bytes = reinterpret_cast<uint8_t*>(&job_);
    for (uint32_t i = 0; i < sizeof(job_); ++i) bytes[i] = 0;
    job_.generation = lastGeneration;
    job_.state = State::Free;
}

bool Manager::copy_text(char* destination, uint32_t capacity,
                        const char* source)
{
    if (!destination || capacity == 0 || !source) return false;
    uint32_t i = 0;
    for (; i + 1U < capacity && source[i] != '\0'; ++i)
        destination[i] = source[i];
    if (source[i] != '\0') {
        destination[0] = '\0';
        return false;
    }
    destination[i] = '\0';
    return true;
}

bool Manager::request_slot(uint32_t index, const char** value) const
{
    if (!value || index >= 8U) return false;
    switch (index) {
    case 0: *value = job_.request.projectRoot; break;
    case 1: *value = job_.request.projectId; break;
    case 2: *value = job_.request.projectKind; break;
    case 3: *value = job_.request.targetProfile; break;
    case 4: *value = job_.request.buildSystem; break;
    case 5: *value = job_.request.buildScript; break;
    case 6: *value = job_.request.expectedArtifact; break;
    case 7: *value = job_.request.configuration; break;
    }
    return true;
}

gx_result Manager::copy_request(const gx_build_request* request)
{
    if (!request || request->size < sizeof(gx_build_request) ||
        request->version != GX_BUILD_API_VERSION) {
        return GX_ERROR_INVALID_ARGUMENT;
    }

    const char* values[8] = {
        request->projectRoot, request->projectId, request->projectKind,
        request->targetProfile, request->buildSystem, request->buildScript,
        request->expectedArtifact, request->configuration
    };
    for (uint32_t i = 0; i < 8U; ++i) {
        if (!values[i] || bounded_length(values[i], kRequestStringCapacity) >=
                              kRequestStringCapacity ||
            !copy_text(job_.requestStrings[i], kRequestStringCapacity,
                       values[i])) {
            for (uint32_t j = 0; j < 8U; ++j) job_.requestStrings[j][0] = '\0';
            return GX_ERROR_INVALID_ARGUMENT;
        }
    }

    job_.request = {};
    job_.request.size = sizeof(gx_build_request);
    job_.request.version = GX_BUILD_API_VERSION;
    job_.request.projectRoot = job_.requestStrings[0];
    job_.request.projectId = job_.requestStrings[1];
    job_.request.projectKind = job_.requestStrings[2];
    job_.request.targetProfile = job_.requestStrings[3];
    job_.request.buildSystem = job_.requestStrings[4];
    job_.request.buildScript = job_.requestStrings[5];
    job_.request.expectedArtifact = job_.requestStrings[6];
    job_.request.configuration = job_.requestStrings[7];
    return GX_OK;
}

bool Manager::handle_matches(gx_build_handle handle) const
{
    return job_.used && handle != 0 && handle == job_.handle &&
        static_cast<uint32_t>(handle & 0xFFFFFFFFULL) == kBuildSlotId &&
        static_cast<uint32_t>(handle >> 32) == job_.generation;
}

bool Manager::owner_matches(uint64_t ownerIdentity) const
{
    return ownerIdentity != 0 && job_.ownerIdentity == ownerIdentity;
}

bool Manager::terminal(State state)
{
    return state == State::Succeeded || state == State::Failed ||
        state == State::Cancelled;
}

uint32_t Manager::public_state(State state)
{
    switch (state) {
    case State::Preparing: return GX_BUILD_VALIDATING;
    case State::Queued: return GX_BUILD_PREPARING;
    case State::Running: return GX_BUILD_RUNNING;
    case State::Completing: return GX_BUILD_VALIDATING_ARTIFACT;
    case State::Succeeded: return GX_BUILD_SUCCEEDED;
    case State::Failed: return GX_BUILD_FAILED;
    case State::Cancelled: return GX_BUILD_CANCELLED;
    case State::Free: return GX_BUILD_IDLE;
    }
    return GX_BUILD_IDLE;
}

void Manager::clear_snapshot(gx_build_snapshot* snapshot,
                             gx_build_handle handle,
                             uint32_t state)
{
    if (!snapshot) return;
    uint8_t* bytes = reinterpret_cast<uint8_t*>(snapshot);
    for (uint32_t i = 0; i < sizeof(*snapshot); ++i) bytes[i] = 0;
    snapshot->size = sizeof(gx_build_snapshot);
    snapshot->version = kBuildApiVersion;
    snapshot->handle = handle;
    snapshot->state = state;
    snapshot->warningCount = GX_BUILD_COUNT_UNAVAILABLE;
}

void Manager::append_output_line(gx_build_snapshot* snapshot,
                                 const char* text)
{
    if (!snapshot || !text) return;
    if (snapshot->outputCount >= GX_BUILD_MAX_OUTPUT_LINES) {
        snapshot->outputTruncated = 1U;
        return;
    }
    gx_build_output_line& line = snapshot->output[snapshot->outputCount++];
    line.stream = 1U;
    if (!copy_text(line.text, sizeof(line.text), text))
        snapshot->outputTruncated = 1U;
}

gx_result Manager::begin_cooperative_job(uint64_t ownerIdentity,
                                         const gx_build_request* request,
                                         gx_build_handle* outHandle)
{
    if (!outHandle) return GX_ERROR_INVALID_ARGUMENT;
    *outHandle = 0;
    if (ownerIdentity == 0 || !request ||
        request->size < sizeof(gx_build_request) ||
        request->version != GX_BUILD_API_VERSION) {
        return GX_ERROR_INVALID_ARGUMENT;
    }

    lock();
    if (shuttingDown_) {
        unlock();
        return GX_ERROR_FAILED;
    }
    if (job_.used) {
        unlock();
        return GX_ERROR_BUSY;
    }
    if (generationExhausted_ || generation_ == 0xFFFFFFFFU) {
        generationExhausted_ = true;
        unlock();
        return GX_ERROR_FAILED;
    }

    clear_job();
    const gx_result copied = copy_request(request);
    if (copied != GX_OK) {
        clear_job();
        unlock();
        return copied;
    }
    ++generation_;
    job_.generation = generation_;
    job_.ownerIdentity = ownerIdentity;
    job_.handle = (static_cast<uint64_t>(generation_) << 32) | kBuildSlotId;
    job_.used = true;
    job_.cooperative = true;
    job_.state = State::Queued;
    clear_snapshot(&job_.snapshot, job_.handle, GX_BUILD_PREPARING);
    *outHandle = job_.handle;
    unlock();
    return GX_OK;
}

gx_result Manager::begin_cooperative_step(uint64_t ownerIdentity,
                                          gx_build_handle handle,
                                          const gx_build_request** outRequest,
                                          gx_build_snapshot* outSnapshot,
                                          bool* outCancelRequested)
{
    if (!outRequest || !outSnapshot || !outCancelRequested)
        return GX_ERROR_INVALID_ARGUMENT;
    *outRequest = nullptr;
    clear_snapshot(outSnapshot, handle, GX_BUILD_IDLE);
    *outCancelRequested = false;
    lock();
    if (!handle_matches(handle)) {
        unlock();
        return GX_ERROR_INVALID_ARGUMENT;
    }
    if (!owner_matches(ownerIdentity) || job_.ownerClosed) {
        unlock();
        return GX_ERROR_PERMISSION_DENIED;
    }
    if (!job_.cooperative || terminal(job_.state) || job_.pumpInProgress ||
        job_.finalizationClaimed || shuttingDown_) {
        unlock();
        return GX_ERROR_BUSY;
    }
    job_.pumpInProgress = true;
    if (job_.state == State::Queued) {
        job_.state = State::Running;
        job_.snapshot.state = GX_BUILD_RUNNING;
    }
    *outRequest = &job_.request;
    *outSnapshot = job_.snapshot;
    *outCancelRequested = job_.cancelRequested;
    unlock();
    return GX_OK;
}

gx_result Manager::finish_cooperative_step(uint64_t ownerIdentity,
                                           gx_build_handle handle,
                                           const gx_build_snapshot* result,
                                           bool terminalResult,
                                           bool* outCancelled)
{
    if (!result || !outCancelled) return GX_ERROR_INVALID_ARGUMENT;
    *outCancelled = false;
    lock();
    if (!handle_matches(handle)) {
        unlock();
        return GX_ERROR_INVALID_ARGUMENT;
    }
    if (!owner_matches(ownerIdentity)) {
        unlock();
        return GX_ERROR_PERMISSION_DENIED;
    }
    if (!job_.cooperative || !job_.pumpInProgress || terminal(job_.state)) {
        unlock();
        return GX_ERROR_FAILED;
    }

    job_.pumpInProgress = false;
    if ((job_.cancelRequested || job_.ownerClosed || shuttingDown_) &&
        !job_.finalizationClaimed) {
        char cancellationLine[GX_BUILD_MAX_OUTPUT_LINE_BYTES] = {};
        bool hadCancellationLine = false;
        for (uint32_t i = 0; i < job_.snapshot.outputCount; ++i) {
            const char* current = job_.snapshot.output[i].text;
            const char prefix[] = "Cancellation requested;";
            uint32_t j = 0;
            while (j + 1U < sizeof(prefix) && current[j] == prefix[j] &&
                   current[j] != '\0') ++j;
            if (j + 1U == sizeof(prefix)) {
                copy_text(cancellationLine, sizeof(cancellationLine), current);
                hadCancellationLine = true;
                break;
            }
        }
        job_.snapshot = *result;
        job_.snapshot.size = sizeof(gx_build_snapshot);
        job_.snapshot.version = kBuildApiVersion;
        job_.snapshot.handle = job_.handle;
        if (hadCancellationLine) {
            bool alreadyPresent = false;
            for (uint32_t i = 0; i < job_.snapshot.outputCount; ++i) {
                const char* current = job_.snapshot.output[i].text;
                uint32_t j = 0;
                while (cancellationLine[j] != '\0' &&
                       current[j] == cancellationLine[j]) ++j;
                if (cancellationLine[j] == '\0') {
                    alreadyPresent = true;
                    break;
                }
            }
            if (!alreadyPresent) append_output_line(&job_.snapshot,
                                                     cancellationLine);
        }
        set_cancelled("build cancelled at a cooperative stage boundary");
        *outCancelled = true;
    } else {
        job_.snapshot = *result;
        job_.snapshot.size = sizeof(gx_build_snapshot);
        job_.snapshot.version = kBuildApiVersion;
        job_.snapshot.handle = job_.handle;
        if (terminalResult) {
            if (result->state == GX_BUILD_SUCCEEDED &&
                (!job_.sourceMembershipReady || !result->artifactValid)) {
                clear_snapshot(&job_.snapshot, job_.handle, GX_BUILD_FAILED);
                job_.snapshot.errorCode = GX_BUILD_ERROR_INTERNAL;
                copy_text(job_.snapshot.errorMessage,
                          sizeof(job_.snapshot.errorMessage),
                          "successful build result has no validated artifact identity");
                job_.state = State::Failed;
            } else {
                job_.state = result->state == GX_BUILD_SUCCEEDED ? State::Succeeded :
                    (result->state == GX_BUILD_CANCELLED ? State::Cancelled : State::Failed);
            }
        } else {
            job_.state = result->state == GX_BUILD_VALIDATING_ARTIFACT
                ? State::Completing : State::Running;
        }
    }

    if (job_.ownerClosed || shuttingDown_) clear_job();
    unlock();
    return GX_OK;
}

gx_result Manager::claim_finalization(uint64_t ownerIdentity,
                                      gx_build_handle handle)
{
    lock();
    if (!handle_matches(handle)) {
        unlock();
        return GX_ERROR_INVALID_ARGUMENT;
    }
    if (!owner_matches(ownerIdentity) || job_.ownerClosed) {
        unlock();
        return GX_ERROR_PERMISSION_DENIED;
    }
    if (!job_.cooperative || !job_.pumpInProgress || terminal(job_.state)) {
        unlock();
        return GX_ERROR_FAILED;
    }
    if (job_.cancelRequested || shuttingDown_) {
        unlock();
        return GX_ERROR_BUSY;
    }
    job_.finalizationClaimed = true;
    job_.state = State::Completing;
    job_.snapshot.state = GX_BUILD_VALIDATING_ARTIFACT;
    unlock();
    return GX_OK;
}

gx_result Manager::request_cancel(uint64_t ownerIdentity,
                                  gx_build_handle handle)
{
    lock();
    if (!handle_matches(handle)) {
        unlock();
        return GX_ERROR_INVALID_ARGUMENT;
    }
    if (!owner_matches(ownerIdentity) || job_.ownerClosed) {
        unlock();
        return GX_ERROR_PERMISSION_DENIED;
    }
    if (!job_.cooperative) {
        unlock();
        return GX_ERROR_BUSY;
    }
    if (job_.state == State::Cancelled) {
        unlock();
        return GX_OK;
    }
    if (terminal(job_.state) || job_.finalizationClaimed) {
        unlock();
        return GX_ERROR_BUSY;
    }
    if (!job_.cancelRequested) {
        job_.cancelRequested = true;
        if (job_.state == State::Queued && !job_.pumpInProgress) {
            set_cancelled("queued build job cancelled before execution");
        } else {
            append_output_line(&job_.snapshot,
                "Cancellation requested; it will take effect at the next safe boundary.");
        }
    }
    unlock();
    return GX_OK;
}

void Manager::set_cancelled(const char* reason)
{
    job_.state = State::Cancelled;
    job_.snapshot.state = GX_BUILD_CANCELLED;
    job_.snapshot.artifactValid = 0U;
    job_.snapshot.artifactSize = 0U;
    job_.snapshot.artifactEntryPoint = 0U;
    job_.snapshot.processExitCode = -1;
    job_.snapshot.errorCode = GX_BUILD_ERROR_BUILD_CANCELLED;
    job_.snapshot.errorCount = 0U;
    copy_text(job_.snapshot.errorMessage, sizeof(job_.snapshot.errorMessage),
              reason ? reason : "build job cancelled before completion");
}

gx_result Manager::begin_synchronous_job(uint64_t ownerIdentity,
                                         const gx_build_request* request,
                                         gx_build_handle* outHandle)
{
    if (!outHandle) return GX_ERROR_INVALID_ARGUMENT;
    *outHandle = 0;
    if (ownerIdentity == 0 || !request) return GX_ERROR_INVALID_ARGUMENT;
    if (request->size < sizeof(gx_build_request) ||
        request->version != GX_BUILD_API_VERSION) return GX_ERROR_INVALID_ARGUMENT;

    lock();
    if (shuttingDown_) {
        unlock();
        return GX_ERROR_FAILED;
    }
    if (job_.used) {
        unlock();
        return GX_ERROR_BUSY;
    }
    if (generationExhausted_ || generation_ == 0xFFFFFFFFU) {
        generationExhausted_ = true;
        unlock();
        return GX_ERROR_FAILED;
    }

    clear_job();
    const gx_result copied = copy_request(request);
    if (copied != GX_OK) {
        clear_job();
        unlock();
        return copied;
    }
    ++generation_;
    job_.generation = generation_;
    job_.ownerIdentity = ownerIdentity;
    job_.handle = (static_cast<uint64_t>(generation_) << 32) | kBuildSlotId;
    job_.used = true;
    job_.callActive = true;
    job_.state = State::Preparing;
    clear_snapshot(&job_.snapshot, job_.handle, GX_BUILD_VALIDATING);
    *outHandle = job_.handle;
    unlock();
    return GX_OK;
}

const gx_build_request* Manager::request_for_execution(
    uint64_t ownerIdentity, gx_build_handle handle)
{
    lock();
    const gx_build_request* result = nullptr;
    if (handle_matches(handle) && owner_matches(ownerIdentity) &&
        job_.callActive && !job_.testOnly && !job_.ownerClosed &&
        !job_.cancelRequested && !shuttingDown_) {
        result = &job_.request;
    }
    unlock();
    return result;
}

gx_result Manager::set_source_membership(uint64_t ownerIdentity,
                                         gx_build_handle handle,
                                         const SourceSelection& selection)
{
    lock();
    if (!handle_matches(handle)) {
        unlock();
        return GX_ERROR_INVALID_ARGUMENT;
    }
    if (!owner_matches(ownerIdentity)) {
        unlock();
        return GX_ERROR_PERMISSION_DENIED;
    }
    const bool executionOwned = job_.callActive ||
        (job_.cooperative && job_.pumpInProgress);
    const bool preparing = job_.state == State::Preparing ||
        (job_.cooperative && job_.state == State::Running &&
         !job_.sourceMembershipReady);
    if (!executionOwned || job_.testOnly || job_.ownerClosed ||
        job_.cancelRequested || shuttingDown_ ||
        !preparing ||
        selection.count == 0 || selection.count > kMaximumSources) {
        unlock();
        return GX_ERROR_INVALID_ARGUMENT;
    }

    job_.sources = {};
    job_.sources.count = selection.count;
    for (uint32_t i = 0; i < selection.count; ++i) {
        if (!copy_text(job_.sources.paths[i], kPathCapacity,
                       selection.paths[i]) ||
            !copy_text(job_.sources.relatives[i], kPathCapacity,
                       selection.relatives[i]) ||
            !copy_text(job_.sources.objectPaths[i], kPathCapacity,
                       selection.objectPaths[i])) {
            job_.sources = {};
            job_.sourceMembershipReady = false;
            unlock();
            return GX_ERROR_INVALID_ARGUMENT;
        }
    }
    job_.sourceMembershipReady = true;
    unlock();
    return GX_OK;
}

gx_result Manager::queue_synchronous_job(uint64_t ownerIdentity,
                                         gx_build_handle handle)
{
    lock();
    if (!handle_matches(handle)) {
        unlock();
        return GX_ERROR_INVALID_ARGUMENT;
    }
    if (!owner_matches(ownerIdentity)) {
        unlock();
        return GX_ERROR_PERMISSION_DENIED;
    }
    if (!job_.callActive || job_.testOnly || job_.ownerClosed ||
        job_.cancelRequested || shuttingDown_ ||
        job_.state != State::Preparing ||
        !job_.sourceMembershipReady) {
        unlock();
        return GX_ERROR_FAILED;
    }
    job_.state = State::Queued;
    job_.snapshot.state = GX_BUILD_PREPARING;
    unlock();
    return GX_OK;
}

gx_result Manager::mark_running(uint64_t ownerIdentity,
                                gx_build_handle handle)
{
    lock();
    if (!handle_matches(handle)) {
        unlock();
        return GX_ERROR_INVALID_ARGUMENT;
    }
    if (!owner_matches(ownerIdentity)) {
        unlock();
        return GX_ERROR_PERMISSION_DENIED;
    }
    if (!job_.callActive || job_.testOnly || job_.ownerClosed ||
        job_.cancelRequested || shuttingDown_ ||
        job_.state != State::Queued) {
        unlock();
        return GX_ERROR_FAILED;
    }
    job_.state = State::Running;
    job_.snapshot.state = GX_BUILD_RUNNING;
    unlock();
    return GX_OK;
}

gx_result Manager::complete_synchronous_job(uint64_t ownerIdentity,
                                            gx_build_handle handle,
                                            const gx_build_snapshot* result)
{
    if (!result) return GX_ERROR_INVALID_ARGUMENT;
    lock();
    if (!handle_matches(handle)) {
        unlock();
        return GX_ERROR_INVALID_ARGUMENT;
    }
    if (!owner_matches(ownerIdentity)) {
        unlock();
        return GX_ERROR_PERMISSION_DENIED;
    }
    if (!job_.callActive || job_.testOnly || terminal(job_.state)) {
        unlock();
        return GX_ERROR_FAILED;
    }

    job_.state = State::Completing;
    const bool resultTerminal = result->state == GX_BUILD_SUCCEEDED ||
        result->state == GX_BUILD_FAILED || result->state == GX_BUILD_CANCELLED;
    if (!resultTerminal) {
        clear_snapshot(&job_.snapshot, job_.handle, GX_BUILD_FAILED);
        job_.snapshot.errorCode = GX_BUILD_ERROR_INTERNAL;
        copy_text(job_.snapshot.errorMessage, sizeof(job_.snapshot.errorMessage),
                  "build service returned without a terminal result");
        job_.state = State::Failed;
    } else if (result->state == GX_BUILD_SUCCEEDED &&
               (!job_.sourceMembershipReady || !result->artifactValid)) {
        clear_snapshot(&job_.snapshot, job_.handle, GX_BUILD_FAILED);
        job_.snapshot.errorCode = GX_BUILD_ERROR_INTERNAL;
        copy_text(job_.snapshot.errorMessage, sizeof(job_.snapshot.errorMessage),
                  "successful build result has no validated artifact identity");
        job_.state = State::Failed;
    } else {
        job_.snapshot = *result;
        job_.snapshot.size = sizeof(gx_build_snapshot);
        job_.snapshot.version = kBuildApiVersion;
        job_.snapshot.handle = job_.handle;
        job_.state = result->state == GX_BUILD_SUCCEEDED ? State::Succeeded :
            (result->state == GX_BUILD_CANCELLED ? State::Cancelled : State::Failed);
    }

    job_.callActive = false;
    if (job_.ownerClosed || shuttingDown_) {
        clear_job();
    }
    unlock();
    return GX_OK;
}

gx_result Manager::admit_test_job(uint64_t ownerIdentity,
                                  const gx_build_request* request,
                                  gx_build_handle* outHandle)
{
    if (!outHandle) return GX_ERROR_INVALID_ARGUMENT;
    *outHandle = 0;
    if (ownerIdentity == 0 || !request ||
        request->size < sizeof(gx_build_request) ||
        request->version != GX_BUILD_API_VERSION) {
        return GX_ERROR_INVALID_ARGUMENT;
    }

    lock();
    if (shuttingDown_) {
        unlock();
        return GX_ERROR_FAILED;
    }
    if (job_.used) {
        unlock();
        return GX_ERROR_BUSY;
    }
    if (generationExhausted_ || generation_ == 0xFFFFFFFFU) {
        generationExhausted_ = true;
        unlock();
        return GX_ERROR_FAILED;
    }
    clear_job();
    const gx_result copied = copy_request(request);
    if (copied != GX_OK) {
        clear_job();
        unlock();
        return copied;
    }
    ++generation_;
    job_.generation = generation_;
    job_.ownerIdentity = ownerIdentity;
    job_.handle = (static_cast<uint64_t>(generation_) << 32) | kBuildSlotId;
    job_.used = true;
    job_.state = State::Queued;
    job_.testOnly = true;
    clear_snapshot(&job_.snapshot, job_.handle, GX_BUILD_PREPARING);
    *outHandle = job_.handle;
    unlock();
    return GX_OK;
}

gx_result Manager::pump_one_test_step()
{
    uint32_t generation = 0;
    uint32_t nextStep = 0;
    lock();
    if (shuttingDown_) {
        unlock();
        return GX_ERROR_FAILED;
    }
    if (!job_.used || !job_.testOnly) {
        unlock();
        return GX_ERROR_INVALID_ARGUMENT;
    }
    if (job_.pumpInProgress) {
        unlock();
        return GX_ERROR_BUSY;
    }
    if (terminal(job_.state)) {
        unlock();
        return GX_ERROR_FAILED;
    }
    if (job_.ownerClosed || job_.cancelRequested) {
        set_cancelled("test continuation stopped at a safe step boundary");
        if (job_.ownerClosed) clear_job();
        unlock();
        return GX_OK;
    }
    job_.pumpInProgress = true;
    generation = job_.generation;
    nextStep = job_.testStep + 1U;
    unlock();

    // Exactly one bounded arithmetic continuation unit. It cannot block or
    // access VFS, compiler state, application memory, or a caller stack.
    const uint32_t completedStep = nextStep;

    lock();
    if (!job_.used || job_.generation != generation || !job_.pumpInProgress) {
        unlock();
        return GX_ERROR_FAILED;
    }
    job_.pumpInProgress = false;
    if (job_.ownerClosed || job_.cancelRequested || shuttingDown_) {
        set_cancelled("test continuation stopped at a safe step boundary");
        if (job_.ownerClosed || shuttingDown_) clear_job();
        unlock();
        return GX_OK;
    }

    job_.testStep = completedStep;
    if (job_.state == State::Queued) job_.state = State::Running;
    job_.snapshot.state = GX_BUILD_RUNNING;
    char line[GX_BUILD_MAX_OUTPUT_LINE_BYTES] = {};
    const char prefix[] = "TEST ONLY: continuation step ";
    uint32_t position = 0;
    for (; position + 1U < sizeof(line) && prefix[position] != '\0'; ++position)
        line[position] = prefix[position];
    if (position + 4U < sizeof(line)) {
        line[position++] = static_cast<char>('0' + completedStep);
        line[position++] = '/';
        line[position++] = static_cast<char>('0' + kTestContinuationSteps);
        line[position] = '\0';
    }
    append_output_line(&job_.snapshot, line);
    if (completedStep >= kTestContinuationSteps) {
        job_.state = State::Failed;
        job_.snapshot.state = GX_BUILD_FAILED;
        job_.snapshot.errorCode = kTestErrorCode;
        job_.snapshot.processExitCode = -1;
        copy_text(job_.snapshot.errorMessage, sizeof(job_.snapshot.errorMessage),
                  "test continuation completed; no compiler or artifact work ran");
        job_.snapshot.artifactValid = 0U;
        job_.snapshot.artifactSize = 0U;
        job_.snapshot.artifactEntryPoint = 0U;
    }
    unlock();
    return GX_OK;
}

gx_result Manager::pump_one_test_step_at_desktop_boundary(
    bool nativeElfTargetActive)
{
    // host_bare_poll_event can call desktop::tick while the NativeElf target
    // is active. Do not recurse into build work from that nested update.
    if (nativeElfTargetActive) return GX_ERROR_BUSY;
    return pump_one_test_step();
}

gx_result Manager::request_test_cancel(uint64_t ownerIdentity,
                                       gx_build_handle handle)
{
    lock();
    if (!handle_matches(handle)) {
        unlock();
        return GX_ERROR_INVALID_ARGUMENT;
    }
    if (!owner_matches(ownerIdentity)) {
        unlock();
        return GX_ERROR_PERMISSION_DENIED;
    }
    if (!job_.testOnly || job_.callActive || terminal(job_.state)) {
        unlock();
        return GX_ERROR_BUSY;
    }
    job_.cancelRequested = true;
    unlock();
    return GX_OK;
}

gx_result Manager::cancel_queued(uint64_t ownerIdentity,
                                 gx_build_handle handle)
{
    lock();
    if (!handle_matches(handle)) {
        unlock();
        return GX_ERROR_INVALID_ARGUMENT;
    }
    if (!owner_matches(ownerIdentity)) {
        unlock();
        return GX_ERROR_PERMISSION_DENIED;
    }
    if (job_.state != State::Queued || job_.callActive ||
        job_.pumpInProgress) {
        unlock();
        return GX_ERROR_BUSY;
    }
    job_.cancelRequested = true;
    set_cancelled("queued build job cancelled before execution");
    unlock();
    return GX_OK;
}

gx_result Manager::poll(uint64_t ownerIdentity, gx_build_handle handle,
                        gx_build_snapshot* outSnapshot)
{
    if (!outSnapshot) return GX_ERROR_INVALID_ARGUMENT;
    lock();
    if (!handle_matches(handle)) {
        unlock();
        return GX_ERROR_INVALID_ARGUMENT;
    }
    if (!owner_matches(ownerIdentity) || job_.ownerClosed) {
        unlock();
        return GX_ERROR_PERMISSION_DENIED;
    }
    *outSnapshot = job_.snapshot;
    unlock();
    return GX_OK;
}

gx_result Manager::release(uint64_t ownerIdentity, gx_build_handle handle)
{
    lock();
    if (!handle_matches(handle)) {
        unlock();
        return GX_ERROR_INVALID_ARGUMENT;
    }
    if (!owner_matches(ownerIdentity) || job_.ownerClosed) {
        unlock();
        return GX_ERROR_PERMISSION_DENIED;
    }
    if (job_.callActive || job_.pumpInProgress || !terminal(job_.state)) {
        unlock();
        return GX_ERROR_BUSY;
    }
    clear_job();
    unlock();
    return GX_OK;
}

gx_result Manager::owner_closed(uint64_t ownerIdentity)
{
    if (ownerIdentity == 0) return GX_OK;
    lock();
    if (!job_.used || !owner_matches(ownerIdentity)) {
        unlock();
        return GX_OK;
    }
    job_.ownerClosed = true;
    job_.cancelRequested = true;
    if (job_.callActive || job_.pumpInProgress) {
        unlock();
        return GX_ERROR_BUSY;
    }
    if (!terminal(job_.state))
        set_cancelled("owner closed before the build job completed");
    clear_job();
    unlock();
    return GX_OK;
}

gx_result Manager::shutdown()
{
    lock();
    shuttingDown_ = true;
    if (!job_.used) {
        unlock();
        return GX_OK;
    }
    job_.cancelRequested = true;
    if (job_.callActive || job_.pumpInProgress) {
        unlock();
        return GX_ERROR_BUSY;
    }
    if (!terminal(job_.state))
        set_cancelled("server shutdown stopped queued build work");
    clear_job();
    unlock();
    return GX_OK;
}

bool Manager::is_shutting_down()
{
    lock();
    const bool result = shuttingDown_;
    unlock();
    return result;
}

} // namespace BuildJobManager
} // namespace compiler
} // namespace kernel
