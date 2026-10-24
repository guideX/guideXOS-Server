// Fixed-capacity ownership and lifecycle contract for bare-metal build jobs.
#pragma once

#include "../../../sdk/include/guidexos/build.h"

namespace kernel {
namespace compiler {
namespace BuildJobManager {

static const uint32_t kRequestStringCapacity = 768U;
static const uint32_t kPathCapacity = 256U;
static const uint32_t kMaximumSources = 16U;
static const uint32_t kTestContinuationSteps = 3U;
static const uint64_t kKernelOwnerIdentity = ~static_cast<uint64_t>(0);

struct SourceSelection {
    uint32_t count;
    char paths[kMaximumSources][kPathCapacity];
    char relatives[kMaximumSources][kPathCapacity];
    char objectPaths[kMaximumSources][kPathCapacity];
};

enum class State : uint8_t {
    Free,
    Preparing,
    Queued,
    Running,
    Completing,
    Succeeded,
    Failed,
    Cancelled
};

// One fixed slot. All public operations are bounded and protected by a short
// atomic lock. The compiler and VFS are never called while that lock is held.
class Manager {
public:
    explicit Manager(uint32_t generationSeed = 0U);

    gx_result begin_synchronous_job(uint64_t ownerIdentity,
                                    const gx_build_request* request,
                                    gx_build_handle* outHandle);
    const gx_build_request* request_for_execution(uint64_t ownerIdentity,
                                                  gx_build_handle handle);
    gx_result set_source_membership(uint64_t ownerIdentity,
                                    gx_build_handle handle,
                                    const SourceSelection& selection);
    gx_result queue_synchronous_job(uint64_t ownerIdentity,
                                    gx_build_handle handle);
    gx_result mark_running(uint64_t ownerIdentity, gx_build_handle handle);
    gx_result complete_synchronous_job(uint64_t ownerIdentity,
                                       gx_build_handle handle,
                                       const gx_build_snapshot* result);

    // This test-owned continuation is used only by hosted state-machine tests.
    // The desktop update loop may advance it through the guarded internal
    // pump below. It is never admitted through an application callback.
    gx_result admit_test_job(uint64_t ownerIdentity,
                             const gx_build_request* request,
                             gx_build_handle* outHandle);
    gx_result pump_one_test_step_at_desktop_boundary(
        bool nativeElfTargetActive);
    gx_result request_test_cancel(uint64_t ownerIdentity,
                                  gx_build_handle handle);

    gx_result cancel_queued(uint64_t ownerIdentity, gx_build_handle handle);
    gx_result poll(uint64_t ownerIdentity, gx_build_handle handle,
                   gx_build_snapshot* outSnapshot);
    gx_result release(uint64_t ownerIdentity, gx_build_handle handle);
    gx_result owner_closed(uint64_t ownerIdentity);
    gx_result shutdown();
    bool is_shutting_down();

private:
    struct Job {
        bool used;
        bool callActive;
        bool pumpInProgress;
        bool ownerClosed;
        bool cancelRequested;
        bool sourceMembershipReady;
        bool testOnly;
        State state;
        uint32_t generation;
        uint32_t testStep;
        uint64_t ownerIdentity;
        gx_build_handle handle;
        char requestStrings[8][kRequestStringCapacity];
        gx_build_request request;
        SourceSelection sources;
        gx_build_snapshot snapshot;
    };

    void lock();
    void unlock();
    void clear_job();
    gx_result copy_request(const gx_build_request* request);
    bool handle_matches(gx_build_handle handle) const;
    bool owner_matches(uint64_t ownerIdentity) const;
    bool request_slot(uint32_t index, const char** value) const;
    static bool terminal(State state);
    static uint32_t public_state(State state);
    static bool copy_text(char* destination, uint32_t capacity,
                          const char* source);
    static void clear_snapshot(gx_build_snapshot* snapshot,
                               gx_build_handle handle,
                               uint32_t publicState);
    static void append_test_output(gx_build_snapshot* snapshot,
                                   const char* text);
    void set_cancelled(const char* reason);
    gx_result pump_one_test_step();

    volatile uint8_t lock_;
    bool shuttingDown_;
    bool generationExhausted_;
    uint32_t generation_;
    Job job_;
};

} // namespace BuildJobManager
} // namespace compiler
} // namespace kernel
