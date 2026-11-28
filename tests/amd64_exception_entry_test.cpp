// CPU7C1 hosted tests for the AMD64 exception entry contract.
//
// Links the production kernel/arch/amd64/exception_entry.asm object with
// tests/amd64_exception_entry_probe.asm. Every one of the 32 stubs is entered
// with a CPU-shaped frame and returns through its real IRETQ or recovery
// path. This is hosted CPL3 execution of the production entry code, not real
// exception delivery.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kernel/core/include/kernel/native_elf_fault.h"

using kernel::native_elf::ExceptionFrameStatus;
using kernel::native_elf::NativeExceptionFrame;
using kernel::native_elf::classify_exception_frame;
using kernel::native_elf::exception_vector_has_hardware_error_code;

extern "C" {
alignas(16) uint8_t gxos_exception_stack[16u * 1024u];
uint64_t gxos_native_fault_recovery_rsp = 0;
uint64_t gxos_native_fault_recovery_rip = 0;

#define DECLARE_STUB(n) void exception_stub_##n();
DECLARE_STUB(0) DECLARE_STUB(1) DECLARE_STUB(2) DECLARE_STUB(3) DECLARE_STUB(4)
DECLARE_STUB(5) DECLARE_STUB(6) DECLARE_STUB(7) DECLARE_STUB(8) DECLARE_STUB(9)
DECLARE_STUB(10) DECLARE_STUB(11) DECLARE_STUB(12) DECLARE_STUB(13) DECLARE_STUB(14)
DECLARE_STUB(15) DECLARE_STUB(16) DECLARE_STUB(17) DECLARE_STUB(18) DECLARE_STUB(19)
DECLARE_STUB(20) DECLARE_STUB(21) DECLARE_STUB(22) DECLARE_STUB(23) DECLARE_STUB(24)
DECLARE_STUB(25) DECLARE_STUB(26) DECLARE_STUB(27) DECLARE_STUB(28) DECLARE_STUB(29)
DECLARE_STUB(30) DECLARE_STUB(31)
#undef DECLARE_STUB

struct DeliveryRequest {
    uint64_t stub;
    uint64_t pushError;
    uint64_t errorCode;
    uint64_t rflagsSet;
    uint64_t gprs[15];
};

struct DeliveryResult {
    uint64_t gprs[15];
    uint64_t rflags;
    uint64_t interruptedRsp;
    uint64_t rspAfter;
    uint64_t path;
};

uint64_t gxos_test_deliver(const DeliveryRequest* request, DeliveryResult* result);
void gxos_test_recover_label();
uint64_t gxos_test_dispatch_record(const NativeExceptionFrame* frame, uint64_t entryRsp,
                                   uint64_t entryRflags);
}

static_assert(offsetof(DeliveryRequest, gprs) == 32, "probe REQ_GPRS");
static_assert(offsetof(DeliveryResult, path) == 144, "probe RES_PATH");

typedef void (*StubFn)();
static const StubFn kStubs[32] = {
    exception_stub_0,  exception_stub_1,  exception_stub_2,  exception_stub_3,
    exception_stub_4,  exception_stub_5,  exception_stub_6,  exception_stub_7,
    exception_stub_8,  exception_stub_9,  exception_stub_10, exception_stub_11,
    exception_stub_12, exception_stub_13, exception_stub_14, exception_stub_15,
    exception_stub_16, exception_stub_17, exception_stub_18, exception_stub_19,
    exception_stub_20, exception_stub_21, exception_stub_22, exception_stub_23,
    exception_stub_24, exception_stub_25, exception_stub_26, exception_stub_27,
    exception_stub_28, exception_stub_29, exception_stub_30, exception_stub_31,
};

static NativeExceptionFrame s_seen;
static const NativeExceptionFrame* s_seenAddress = nullptr;
static uint64_t s_seenEntryRsp = 0;
static uint64_t s_seenEntryRflags = 0;
static uint64_t s_disposition = 0;
static uint32_t s_dispatchCount = 0;

extern "C" uint64_t gxos_test_dispatch_record(const NativeExceptionFrame* frame,
                                              uint64_t entryRsp, uint64_t entryRflags) {
    s_seenAddress = frame;
    memcpy(&s_seen, frame, sizeof(s_seen));
    s_seenEntryRsp = entryRsp;
    s_seenEntryRflags = entryRflags;
    ++s_dispatchCount;
    return s_disposition;
}

static int s_failures = 0;
static int s_checks = 0;
#define CHECK(cond, ...) do { ++s_checks; if (!(cond)) { ++s_failures; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static constexpr uint64_t kDF = 1ull << 10;

// Architectural table, written independently of the production mask.
static bool architectural_error_code(uint32_t v) {
    switch (v) {
    case 8: case 10: case 11: case 12: case 13: case 14: case 17: case 21: case 29: case 30:
        return true;
    default:
        return false;
    }
}

static void make_request(DeliveryRequest& req, uint32_t vector, uint64_t error, uint64_t rflagsSet) {
    memset(&req, 0, sizeof(req));
    req.stub = reinterpret_cast<uint64_t>(kStubs[vector]);
    req.pushError = architectural_error_code(vector) ? 1u : 0u;
    req.errorCode = error;
    req.rflagsSet = rflagsSet;
    for (uint32_t i = 0; i < 15; ++i) {
        req.gprs[i] = 0x1100000000000000ull * (i + 1u) + (static_cast<uint64_t>(vector) << 16) + i;
    }
}

static void group_classification() {
    for (uint32_t v = 0; v < 32; ++v) {
        CHECK(exception_vector_has_hardware_error_code(v) == architectural_error_code(v),
              "vector %u classification", v);
    }
    CHECK(!exception_vector_has_hardware_error_code(16), "#MF has no error code");
    CHECK(exception_vector_has_hardware_error_code(17), "#AC has an error code");
    CHECK(exception_vector_has_hardware_error_code(21), "#CP has an error code");
    CHECK(!exception_vector_has_hardware_error_code(32), "vector 32 is not an exception");
    CHECK(!exception_vector_has_hardware_error_code(~0ull), "huge vector");
}

static void group_resume_all_vectors() {
    for (uint32_t v = 0; v < 32; ++v) {
        for (int df = 0; df < 2; ++df) {
            DeliveryRequest req;
            DeliveryResult res;
            const uint64_t hwError = architectural_error_code(v) ? (0x0000BEE0u + v) : 0u;
            make_request(req, v, hwError, df ? kDF : 0u);
            memset(&res, 0, sizeof(res));
            s_disposition = kernel::native_elf::kExceptionDispositionResume;
            const uint32_t before = s_dispatchCount;
            gxos_test_deliver(&req, &res);

            CHECK(s_dispatchCount == before + 1u, "v%u dispatched once", v);
            CHECK(res.path == 1u, "v%u resumed via IRETQ (path=%llu)", v, (unsigned long long)res.path);
            // Normalized frame contents.
            CHECK(s_seen.vector == v, "v%u frame vector %llu", v, (unsigned long long)s_seen.vector);
            CHECK(s_seen.errorCode == hwError, "v%u error %llx expected %llx", v,
                  (unsigned long long)s_seen.errorCode, (unsigned long long)hwError);
            const uint64_t* saved = &s_seen.rax;
            for (uint32_t i = 0; i < 15; ++i) {
                CHECK(saved[i] == req.gprs[i], "v%u saved gpr %u", v, i);
            }
            CHECK(s_seen.rsp == res.interruptedRsp, "v%u saved RSP", v);
            CHECK(((s_seen.rflags & kDF) != 0) == (df != 0), "v%u saved DF", v);
            // CPU frame is 16-aligned below the interrupted RSP, so the frame
            // address is that aligned top minus the 22-word normalized frame.
            const uint64_t alignedTop = res.interruptedRsp & ~0xFull;
            CHECK(reinterpret_cast<uint64_t>(s_seenAddress) == alignedTop - sizeof(NativeExceptionFrame),
                  "v%u frame address", v);
            // Dispatcher ABI: DF clear, RSP 8 mod 16 at entry, on the exception
            // stack with 32 bytes of shadow space below its top.
            CHECK((s_seenEntryRflags & kDF) == 0, "v%u DF clear in dispatcher", v);
            CHECK((s_seenEntryRsp & 0xFu) == 8u, "v%u dispatcher entry alignment", v);
            const uint64_t top = (reinterpret_cast<uint64_t>(gxos_exception_stack) +
                                  sizeof(gxos_exception_stack)) & ~0xFull;
            CHECK(s_seenEntryRsp == top - 32u - 8u, "v%u dispatcher stack/shadow placement", v);
            // Return: exact interrupted RSP, all GPRs, interrupted DF restored.
            CHECK(res.rspAfter == res.interruptedRsp, "v%u stack cleanup (rsp %llx vs %llx)", v,
                  (unsigned long long)res.rspAfter, (unsigned long long)res.interruptedRsp);
            for (uint32_t i = 0; i < 15; ++i) {
                CHECK(res.gprs[i] == req.gprs[i], "v%u restored gpr %u", v, i);
            }
            CHECK(((res.rflags & kDF) != 0) == (df != 0), "v%u DF restored by IRETQ", v);
        }
    }
}

static void group_recovery() {
    alignas(16) static uint8_t recoveryStack[256];
    const uint64_t recoveryRsp = reinterpret_cast<uint64_t>(recoveryStack + sizeof(recoveryStack) - 64);
    const uint32_t vectors[] = {0u, 6u, 13u, 14u, 16u, 17u, 21u};
    for (uint32_t v : vectors) {
        DeliveryRequest req;
        DeliveryResult res;
        make_request(req, v, architectural_error_code(v) ? 0x10u : 0u, kDF);
        memset(&res, 0, sizeof(res));
        gxos_native_fault_recovery_rsp = recoveryRsp;
        gxos_native_fault_recovery_rip = reinterpret_cast<uint64_t>(gxos_test_recover_label);
        s_disposition = kernel::native_elf::kExceptionDispositionRecover;
        gxos_test_deliver(&req, &res);
        CHECK(res.path == 2u, "v%u recovery path", v);
        CHECK(res.rspAfter == recoveryRsp, "v%u recovery RSP", v);
        CHECK((res.rflags & kDF) == 0, "v%u recovery continues with DF clear", v);
        CHECK(s_seen.vector == v, "v%u recovery frame vector", v);
    }
    gxos_native_fault_recovery_rsp = 0;
    gxos_native_fault_recovery_rip = 0;
    s_disposition = 0;
}

static NativeExceptionFrame valid_kernel_frame(uint64_t vector, uint64_t error) {
    NativeExceptionFrame f;
    memset(&f, 0, sizeof(f));
    f.vector = vector;
    f.errorCode = error;
    f.rip = 0x0000000000245000ull;
    f.cs = 0x08;
    f.rflags = 0x10046;
    f.rsp = 0x0000000022B88A10ull;
    f.ss = 0x10;
    return f;
}

static void group_frame_validation() {
    for (uint32_t v = 0; v < 32; ++v) {
        const uint64_t e = (architectural_error_code(v) && v != 8 && v != 17) ? 0x18u : 0u;
        CHECK(classify_exception_frame(valid_kernel_frame(v, e)) == ExceptionFrameStatus::Valid,
              "v%u valid kernel frame", v);
    }
    NativeExceptionFrame f = valid_kernel_frame(13, 0);
    f.ss = 0;
    CHECK(classify_exception_frame(f) == ExceptionFrameStatus::Valid, "CPL0 null SS");
    f = valid_kernel_frame(14, 6);
    f.cs = 0x23; f.ss = 0x1B; f.rsp = 0x00007FFFFFFFE000ull; f.rflags = 0x202;
    CHECK(classify_exception_frame(f) == ExceptionFrameStatus::Valid, "CPL3 frame");
    f.ss = 0x10;
    CHECK(classify_exception_frame(f) == ExceptionFrameStatus::StackSelectorMismatch, "CPL3 kernel SS");

    f = valid_kernel_frame(32, 0);
    CHECK(classify_exception_frame(f) == ExceptionFrameStatus::VectorOutOfRange, "vector 32");
    f = valid_kernel_frame(16, 1);
    CHECK(classify_exception_frame(f) == ExceptionFrameStatus::SyntheticErrorCodeNonzero, "#MF synthetic");
    f = valid_kernel_frame(13, 0x100000000ull);
    CHECK(classify_exception_frame(f) == ExceptionFrameStatus::ErrorCodeHighBits, "error high bits");
    f = valid_kernel_frame(8, 4);
    CHECK(classify_exception_frame(f) == ExceptionFrameStatus::ErrorCodeMustBeZero, "#DF nonzero");
    f = valid_kernel_frame(17, 1);
    CHECK(classify_exception_frame(f) == ExceptionFrameStatus::ErrorCodeMustBeZero, "#AC nonzero");
    f = valid_kernel_frame(13, 0); f.cs = 0x33;
    CHECK(classify_exception_frame(f) == ExceptionFrameStatus::UnknownCodeSelector, "unknown CS");
    f = valid_kernel_frame(13, 0); f.ss = 0x1B;
    CHECK(classify_exception_frame(f) == ExceptionFrameStatus::StackSelectorMismatch, "kernel CS user SS");
    f = valid_kernel_frame(13, 0); f.rflags = 0x46;
    f.rflags &= ~0x2ull;
    CHECK(classify_exception_frame(f) == ExceptionFrameStatus::RflagsReservedBits, "rflags bit1");
    f = valid_kernel_frame(13, 0); f.rflags |= 1ull << 17;
    CHECK(classify_exception_frame(f) == ExceptionFrameStatus::RflagsReservedBits, "rflags VM");
    f = valid_kernel_frame(13, 0); f.rflags |= 1ull << 40;
    CHECK(classify_exception_frame(f) == ExceptionFrameStatus::RflagsReservedBits, "rflags high");
    f = valid_kernel_frame(13, 0); f.rip = 0x0000800000000000ull;
    CHECK(classify_exception_frame(f) == ExceptionFrameStatus::NonCanonicalRip, "rip");
    f = valid_kernel_frame(13, 0); f.rip = 0xFFFF800000000000ull;
    CHECK(classify_exception_frame(f) == ExceptionFrameStatus::Valid, "upper-half rip");
    f = valid_kernel_frame(13, 0); f.rsp = 0x0001000000000000ull;
    CHECK(classify_exception_frame(f) == ExceptionFrameStatus::NonCanonicalRsp, "rsp");

    // The pre-CPU7C1 misclassification shape: a #MF frame entered through a
    // stub that assumed an error word sees RIP in the error slot, CS in the
    // RIP slot, and so on. A real #AC through a no-error stub is shifted the
    // other way. Both must be rejected, never recovered.
    const NativeExceptionFrame real = valid_kernel_frame(16, 0);
    NativeExceptionFrame shiftedUp = real;
    shiftedUp.errorCode = real.rip; shiftedUp.rip = real.cs; shiftedUp.cs = real.rflags;
    shiftedUp.rflags = real.rsp; shiftedUp.rsp = real.ss; shiftedUp.ss = 0;
    CHECK(classify_exception_frame(shiftedUp) != ExceptionFrameStatus::Valid, "#MF shifted frame rejected");
    NativeExceptionFrame ac = valid_kernel_frame(17, 0);
    NativeExceptionFrame shiftedDown = ac;
    shiftedDown.errorCode = 0; shiftedDown.rip = 0 /* hardware #AC error */; shiftedDown.cs = ac.rip;
    shiftedDown.rflags = ac.cs; shiftedDown.rsp = ac.rflags; shiftedDown.ss = ac.rsp;
    CHECK(classify_exception_frame(shiftedDown) != ExceptionFrameStatus::Valid, "#AC shifted frame rejected");
    NativeExceptionFrame cp = valid_kernel_frame(21, 3);
    NativeExceptionFrame cpShift = cp;
    cpShift.errorCode = 0; cpShift.rip = 3; cpShift.cs = cp.rip; cpShift.rflags = cp.cs;
    cpShift.rsp = cp.rflags; cpShift.ss = cp.rsp;
    CHECK(classify_exception_frame(cpShift) != ExceptionFrameStatus::Valid, "#CP shifted frame rejected");
}

int main() {
    group_classification();
    group_frame_validation();
    group_resume_all_vectors();
    group_recovery();
    printf("checks=%d failures=%d\n", s_checks, s_failures);
    return s_failures == 0 ? 0 : 1;
}
