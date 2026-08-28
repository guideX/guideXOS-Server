//
// Opt-in Phase 27C/27D diagnostic route.
//

#include "native_elf_smoke.h"

#include "native_elf_contract.h"
#include "native_elf_loader.h"
#include "../compiler/compiler_driver.h"
#include "kernel/serial_debug.h"
#include "kernel/vfs.h"

namespace kernel {
namespace native_elf {
namespace {

static uint8_t s_invalidImage[guidexos::native_elf::MAX_ELF_FILE_BYTES];
#if defined(GXOS_PHASE27G_SMOKE)
static uint8_t s_compareImage[guidexos::native_elf::MAX_ELF_FILE_BYTES];
#endif

static void put_u64(uint8_t* bytes, uint32_t offset, uint64_t value)
{
    for (uint32_t i = 0; i < 8; ++i) {
        bytes[offset + i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFFULL);
    }
}

static void print_marker(const char* name, bool pass)
{
    serial::puts(name);
    serial::puts(pass ? "=PASS\n" : "=FAIL\n");
}

static bool run_expected(const char* path, int32_t expected)
{
    int32_t actual = 0;
    NativeElfRunReport report = {};
    return run_file(path, &actual, &report) && actual == expected;
}

static bool run_expected_with_report(const char* path,
                                     int32_t expected,
                                     NativeElfRunReport* report)
{
    int32_t actual = 0;
    return report && run_file(path, &actual, report) && actual == expected;
}

static void print_hash_pair(const char* name, uint64_t sourceHash, uint64_t outputHash,
                            uint64_t dataHash)
{
    serial::puts("NativeElf: ");
    serial::puts(name);
    serial::puts(" source_hash=fnv1a64:");
    serial::put_hex64(sourceHash);
    serial::puts(" elf_hash=fnv1a64:");
    serial::put_hex64(outputHash);
    serial::puts(" data_hash=fnv1a64:");
    serial::put_hex64(dataHash);
    serial::putc('\n');
}

static bool emit_serial_artifact(const char* path, const char* name)
{
    if (!path || !name) return false;

    vfs::FileInfo info = {};
    if (vfs::stat(path, &info) != vfs::VFS_OK ||
        info.size == 0 || info.size > sizeof(s_invalidImage)) {
        return false;
    }

    const uint32_t bytes = static_cast<uint32_t>(info.size);
    const int32_t readBytes = vfs::read_file(path, s_invalidImage, bytes);
    if (readBytes < 0 || static_cast<uint32_t>(readBytes) != bytes) return false;

    serial::puts("NativeElf: artifact_begin=");
    serial::puts(name);
    serial::puts(" bytes=");
    serial::put_hex32(bytes);
    serial::puts("\nNativeElf: artifact_hex=");
    for (uint32_t i = 0; i < bytes; ++i) serial::put_hex8(s_invalidImage[i]);
    serial::puts("\nNativeElf: artifact_end=");
    serial::puts(name);
    serial::puts("\n");
    return true;
}

#if defined(GXOS_PHASE27G_SMOKE)
static bool same_vfs_file_bytes(const char* leftPath, const char* rightPath)
{
    vfs::FileInfo left = {};
    vfs::FileInfo right = {};
    if (!leftPath || !rightPath || vfs::stat(leftPath, &left) != vfs::VFS_OK ||
        vfs::stat(rightPath, &right) != vfs::VFS_OK || left.type != vfs::FILE_TYPE_REGULAR ||
        right.type != vfs::FILE_TYPE_REGULAR || left.size != right.size ||
        left.size == 0 || left.size > sizeof(s_invalidImage)) return false;
    const uint32_t bytes = static_cast<uint32_t>(left.size);
    return vfs::read_file(leftPath, s_invalidImage, bytes) == static_cast<int32_t>(bytes) &&
        vfs::read_file(rightPath, s_compareImage, bytes) == static_cast<int32_t>(bytes) &&
        [&]() {
            for (uint32_t i = 0; i < bytes; ++i) if (s_invalidImage[i] != s_compareImage[i]) return false;
            return true;
        }();
}
#endif

static bool reject_variant(const char* basePath,
                           const char* variantPath,
                           uint32_t bytes,
                           uint32_t mutationOffset,
                           uint64_t mutationValue,
                           bool mutate64,
                           const char* marker,
                           uint32_t secondMutationOffset = 0xFFFFFFFFU,
                           uint64_t secondMutationValue = 0)
{
    vfs::FileInfo info = {};
    if (vfs::stat(basePath, &info) != vfs::VFS_OK ||
        info.size == 0 || info.size > sizeof(s_invalidImage)) return false;
    const uint32_t sourceBytes = static_cast<uint32_t>(info.size);
    const int32_t readBytes = vfs::read_file(basePath, s_invalidImage, sourceBytes);
    if (readBytes < 0 || static_cast<uint32_t>(readBytes) != sourceBytes) return false;
    if (bytes == 0) bytes = sourceBytes;
    if (bytes > sourceBytes) return false;
    // UINT32_MAX is the explicit "truncate without another mutation" marker.
    if (mutationOffset != 0xFFFFFFFFU) {
        if (mutate64) put_u64(s_invalidImage, mutationOffset, mutationValue);
        else s_invalidImage[mutationOffset] = static_cast<uint8_t>(mutationValue & 0xFFULL);
    }
    if (secondMutationOffset != 0xFFFFFFFFU) {
        put_u64(s_invalidImage, secondMutationOffset, secondMutationValue);
    }

    if (vfs::exists(variantPath)) (void)vfs::unlink(variantPath);
    if (vfs::write_file(variantPath, s_invalidImage, bytes) < 0) return false;
    int32_t ignored = 0;
    NativeElfRunReport report = {};
    const bool rejected = !run_file(variantPath, &ignored, &report);
    (void)vfs::unlink(variantPath);
    print_marker(marker, rejected);
    return rejected;
}

} // namespace

void run_bootstrap_execution_smoke()
{
#if defined(__x86_64__)
    serial::puts("ELF Loader: Phase 27C bare-metal smoke begin\n");

    bool compile42 = false;
    bool execute42 = false;
    bool compile41 = false;
    bool execute41 = false;

    compiler::CompileSummary summary = {};
    compile42 = compiler::compile("/r42.c", "/r42.elf", &summary) &&
                summary.returnConstant == 42;
    print_marker("phase27c_compile42", compile42);
    execute42 = compile42 && run_expected("/r42.elf", 42);
    print_marker("phase27c_execute42", execute42);

    compile41 = compiler::compile("/r41.c", "/r41.elf", &summary) &&
                summary.returnConstant == 41;
    print_marker("phase27c_compile41", compile41);
    execute41 = compile41 && run_expected("/r41.elf", 41);
    print_marker("phase27c_execute41", execute41);

    bool repeated = true;
    for (uint32_t i = 0; i < 3; ++i) {
        if (!run_expected("/r42.elf", 42)) repeated = false;
    }
    print_marker("phase27c_repeat_execution", repeated);

    bool invalid = true;
    invalid = reject_variant("/r42.elf", "/p27magic.elf", 0,
                             0, 0, false, "phase27c_invalid_bad_magic") && invalid;
    invalid = reject_variant("/r42.elf", "/p27arch.elf", 0,
                             18, 3, false, "phase27c_invalid_wrong_arch") && invalid;
    invalid = reject_variant("/r42.elf", "/p27entry.elf", 0,
                             24, 0, true, "phase27c_invalid_bad_entry") && invalid;
    invalid = reject_variant("/r42.elf", "/p27out.elf", 0,
                             24, guidexos::native_elf::IMAGE_BASE +
                                 guidexos::native_elf::REGION_SIZE,
                             true, "phase27c_invalid_entry_outside") && invalid;
    invalid = reject_variant("/r42.elf", "/p27trunc.elf", 64,
                             0xFFFFFFFFU, 0, false,
                             "phase27c_invalid_truncated_phdr") && invalid;
    invalid = reject_variant("/r42.elf", "/p27bnd.elf", 0,
                             96, ~0ULL, true, "phase27c_invalid_file_bounds") && invalid;
    invalid = reject_variant("/r42.elf", "/p27addr.elf", 0,
                             80, 0x00100000ULL, true,
                             "phase27c_invalid_dangerous_address", 88,
                             0x00100000ULL) && invalid;
    print_marker("phase27c_invalid_elf", invalid);

    // Alternate builds deliberately reuse the public compile and run routes
    // so the observed value is tied to the current newly-written artifact.
    const bool alternate42a = compiler::compile("/r42.c", "/r42.elf", &summary) &&
                              run_expected("/r42.elf", 42);
    const bool alternate41 = compiler::compile("/r41.c", "/r41.elf", &summary) &&
                             run_expected("/r41.elf", 41);
    const bool alternate42b = compiler::compile("/r42.c", "/r42.elf", &summary) &&
                              run_expected("/r42.elf", 42);
    print_marker("phase27c_alternate_build_run", alternate42a && alternate41 && alternate42b);

    vfs::FileInfo survivalInfo = {};
    uint8_t survivalByte = 0;
    const bool kernelSurvival =
        vfs::stat("/r42.elf", &survivalInfo) == vfs::VFS_OK &&
        vfs::read_file("/r42.elf", &survivalByte, 1) == 1 &&
        survivalByte == 0x7F;
    print_marker("phase27c_kernel_survival", kernelSurvival);

    const bool allPassed = compile42 && execute42 && compile41 && execute41 &&
                           repeated && invalid && alternate42a && alternate41 &&
                           alternate42b && kernelSurvival;
    print_marker("phase27c", allPassed);
    serial::puts(allPassed ? "ELF Loader: Phase 27C smoke PASS\n"
                            : "ELF Loader: Phase 27C smoke FAIL\n");

    serial::puts("ELF Loader: Phase 27D NativeElf application runtime smoke begin\n");
    compiler::CompileSummary buildA = {};
    compiler::CompileSummary buildB = {};
    compiler::CompileSummary buildC = {};
    compiler::CompileSummary buildAAgain = {};
    NativeElfRunReport reportA = {};
    NativeElfRunReport reportB = {};
    NativeElfRunReport reportC = {};
    NativeElfRunReport reportAAgain = {};

    const bool buildProofA = compiler::compile("/d27a.c", "/d27a.elf", &buildA) &&
                             buildA.hasHostLog && buildA.returnConstant == 42;
    const bool runProofA = buildProofA &&
                           run_expected_with_report("/d27a.elf", 42, &reportA);
    const bool buildProofB = compiler::compile("/d27b.c", "/d27b.elf", &buildB) &&
                             buildB.hasHostLog && buildB.returnConstant == 42;
    const bool runProofB = buildProofB &&
                           run_expected_with_report("/d27b.elf", 42, &reportB);
    const bool buildProofC = compiler::compile("/d27c.c", "/d27c.elf", &buildC) &&
                             buildC.hasHostLog && buildC.returnConstant == 41;
    const bool runProofC = buildProofC &&
                           run_expected_with_report("/d27c.elf", 41, &reportC);
    const bool sourceDriven = buildProofA && buildProofB &&
                              buildA.sourceHash != buildB.sourceHash &&
                              buildA.outputHash != buildB.outputHash &&
                              buildA.dataHash != buildB.dataHash &&
                              buildA.dataBytes != 0 && buildB.dataBytes != 0;
    print_hash_pair("source_a", buildA.sourceHash, buildA.outputHash, buildA.dataHash);
    print_hash_pair("source_b", buildB.sourceHash, buildB.outputHash, buildB.dataHash);
    print_marker("phase27d_source_driven_host_call", sourceDriven);

    const bool returnValueProof = runProofA && runProofC &&
                                  reportA.hostLogObserved && reportC.hostLogObserved &&
                                  reportA.returnValue == 42 && reportC.returnValue == 41;
    print_marker("phase27d_return_value", returnValueProof);

    const bool dedicatedStackProof = runProofA && reportA.dedicatedStackUsed &&
        reportA.applicationStackBase == APPLICATION_STACK_BASE &&
        reportA.applicationStackTop == APPLICATION_STACK_BASE + APPLICATION_STACK_SIZE &&
        reportA.applicationRsp > reportA.applicationStackBase &&
        reportA.applicationRsp < reportA.applicationStackTop &&
        reportA.kernelRspBefore == reportA.kernelRspAfter;
    print_marker("phase27d_dedicated_stack", dedicatedStackProof);
    const bool appContextProof = runProofA && reportA.appContextValid;
    print_marker("phase27d_app_context", appContextProof);
    const bool hostLogProof = runProofA && reportA.hostLogObserved && reportA.hostLogBytes != 0;
    print_marker("phase27d_host_log", hostLogProof);

    const bool repeatLifecycle = compiler::compile("/d27a.c", "/d27a.elf", &buildAAgain) &&
        run_expected_with_report("/d27a.elf", 42, &reportAAgain) &&
        reportA.teardownComplete && reportB.teardownComplete &&
        reportC.teardownComplete && reportAAgain.teardownComplete &&
        reportA.finalState == NativeAppExecutionState::Cleaned &&
        reportB.finalState == NativeAppExecutionState::Cleaned &&
        reportC.finalState == NativeAppExecutionState::Cleaned &&
        reportAAgain.finalState == NativeAppExecutionState::Cleaned &&
        reportA.applicationStackBase == reportB.applicationStackBase &&
        reportA.applicationStackBase == reportAAgain.applicationStackBase &&
        reportA.readOnlyDataBase == reportAAgain.readOnlyDataBase;
    print_marker("phase27d_repeat_lifecycle", repeatLifecycle);

    const bool hostCallValidation = native_elf_host_call_validation_smoke();
    print_marker("phase27d_host_call_validation", hostCallValidation);

    vfs::FileInfo phase27dInfo = {};
    uint8_t phase27dMagic[4] = {};
    const bool kernelSurvival27d =
        vfs::stat("/d27a.elf", &phase27dInfo) == vfs::VFS_OK &&
        phase27dInfo.type == vfs::FILE_TYPE_REGULAR &&
        phase27dInfo.size != 0 &&
        vfs::read_file("/d27a.elf", phase27dMagic, sizeof(phase27dMagic)) == sizeof(phase27dMagic) &&
        phase27dMagic[0] == 0x7F && phase27dMagic[1] == 'E' &&
        phase27dMagic[2] == 'L' && phase27dMagic[3] == 'F';
    print_marker("phase27d_kernel_survival", kernelSurvival27d);

    // The compiler writes into the guest VFS, which is memory-backed during
    // this boot harness.  Emit exact generated bytes over the serial proof
    // channel so the host harness can perform an independent ELF audit after
    // QEMU exits without confusing host persistence with guest VFS survival.
    const bool artifactEvidence =
        emit_serial_artifact("/r42.elf", "r42") &&
        emit_serial_artifact("/d27a.elf", "d27a") &&
        emit_serial_artifact("/d27b.elf", "d27b") &&
        emit_serial_artifact("/d27c.elf", "d27c");

    const bool allPassed27d = buildProofA && runProofA && buildProofB && runProofB &&
                               buildProofC && runProofC && sourceDriven && returnValueProof &&
                               dedicatedStackProof && appContextProof && hostLogProof &&
                               repeatLifecycle && hostCallValidation && kernelSurvival27d &&
                               artifactEvidence;
    print_marker("phase27d", allPassed27d);
    serial::puts(allPassed27d ? "ELF Loader: Phase 27D smoke PASS\n"
                              : "ELF Loader: Phase 27D smoke FAIL\n");

#if defined(GXOS_PHASE27E_SMOKE)
    serial::puts("ELF Loader: Phase 27E Developer Studio build integration smoke begin\n");
    int32_t developerStudioReturn = 1;
    NativeElfRunReport developerStudioReport = {};
    const bool developerStudioLaunched = allPassed27d &&
        run_file("/Apps/DS27E/bin/amd64/p27e.elf",
                 &developerStudioReturn, &developerStudioReport) &&
        developerStudioReturn == 0 && developerStudioReport.teardownComplete;
    print_marker("phase27e_app_launch", developerStudioLaunched);
    print_marker("phase27e_kernel_survival", developerStudioLaunched &&
        developerStudioReport.finalState == NativeAppExecutionState::Cleaned);
    serial::puts(developerStudioLaunched ? "ELF Loader: Phase 27E smoke PASS\n"
                                          : "ELF Loader: Phase 27E smoke FAIL\n");
#endif
#if defined(GXOS_PHASE27F_SMOKE)
    serial::puts("ELF Loader: Phase 27F Developer Studio Build-before-Run smoke begin\n");
    int32_t developerStudio27fReturn = 1;
    NativeElfRunReport developerStudio27fReport = {};
    const bool developerStudio27fLaunched = allPassed27d &&
        run_file("/Apps/DS27F/bin/amd64/p27f.elf",
                 &developerStudio27fReturn, &developerStudio27fReport) &&
        developerStudio27fReturn == 0 && developerStudio27fReport.teardownComplete;
    print_marker("phase27f_app_launch", developerStudio27fLaunched);
    print_marker("phase27f_kernel_survival", developerStudio27fLaunched &&
        developerStudio27fReport.finalState == NativeAppExecutionState::Cleaned);
    const bool phase27fPassed = developerStudio27fLaunched &&
        developerStudio27fReport.finalState == NativeAppExecutionState::Cleaned;
    serial::puts(phase27fPassed ? "ELF Loader: Phase 27F smoke PASS\n"
                                : "ELF Loader: Phase 27F smoke FAIL\n");
#endif
#if defined(GXOS_PHASE27G_SMOKE)
    serial::puts("ELF Loader: Phase 27G bootstrap language smoke begin\n");
    compiler::CompileSummary expression = {};
    compiler::CompileSummary locals = {};
    compiler::CompileSummary assignment = {};
    compiler::CompileSummary precedenceA = {};
    compiler::CompileSummary precedenceB = {};
    compiler::CompileSummary unary = {};
    compiler::CompileSummary logs = {};
    compiler::CompileSummary deterministic = {};
    compiler::CompileSummary deterministicAgain = {};
    compiler::CompileSummary unknown = {};
    compiler::CompileSummary duplicate = {};

    const bool expressionProof = compiler::compile("/g27expr.c", "/g27expr.elf", &expression) &&
        expression.returnConstantValid && expression.returnConstant == 42 &&
        run_expected("/g27expr.elf", 42);
    print_marker("phase27g_expression", expressionProof);

    const bool localsProof = compiler::compile("/g27local.c", "/g27local.elf", &locals) &&
        !locals.returnConstantValid && run_expected("/g27local.elf", 42);
    print_marker("phase27g_locals", localsProof);

    const bool assignmentProof = compiler::compile("/g27assn.c", "/g27assn.elf", &assignment) &&
        run_expected("/g27assn.elf", 42);
    print_marker("phase27g_assignment", assignmentProof);

    const bool precedenceProof = compiler::compile("/g27preca.c", "/g27preca.elf", &precedenceA) &&
        compiler::compile("/g27precb.c", "/g27precb.elf", &precedenceB) &&
        precedenceA.returnConstantValid && precedenceA.returnConstant == 42 &&
        precedenceB.returnConstantValid && precedenceB.returnConstant == 42 &&
        run_expected("/g27preca.elf", 42) && run_expected("/g27precb.elf", 42);
    print_marker("phase27g_precedence", precedenceProof);

    const bool unaryProof = compiler::compile("/g27unary.c", "/g27unary.elf", &unary) &&
        run_expected("/g27unary.elf", 42);
    print_marker("phase27g_unary", unaryProof);

    NativeElfRunReport logReport = {};
    const bool multipleLogs = compiler::compile("/g27logs.c", "/g27logs.elf", &logs) &&
        logs.hasHostLog && logs.dataBytes > 0 && run_expected_with_report("/g27logs.elf", 42, &logReport) &&
        logReport.hostLogCount == 3 &&
        logReport.hostLog[0][0] == 'F' && logReport.hostLog[1][0] == 'S' &&
        logReport.hostLog[2][0] == 'T';
    print_marker("phase27g_multiple_host_calls", multipleLogs);

    const bool deterministicProof = compiler::compile("/g27logs.c", "/g27deta.elf", &deterministic) &&
        compiler::compile("/g27logs.c", "/g27detb.elf", &deterministicAgain) &&
        deterministic.sourceHash == deterministicAgain.sourceHash &&
        deterministic.outputHash == deterministicAgain.outputHash &&
        deterministic.outputBytes == deterministicAgain.outputBytes &&
        same_vfs_file_bytes("/g27deta.elf", "/g27detb.elf");
    print_marker("phase27g_deterministic", deterministicProof);

    if (vfs::exists("/g27unknown.elf")) (void)vfs::unlink("/g27unknown.elf");
    const bool unknownIdentifier = !compiler::compile("/g27unknown.c", "/g27unknown.elf", &unknown) &&
        !vfs::exists("/g27unknown.elf") && unknown.diagnosticCount != 0 &&
        unknown.diagnostics[0].message[0] != '\0';
    print_marker("phase27g_unknown_identifier", unknownIdentifier);

    if (vfs::exists("/g27duplicate.elf")) (void)vfs::unlink("/g27duplicate.elf");
    const bool duplicateLocal = !compiler::compile("/g27duplicate.c", "/g27duplicate.elf", &duplicate) &&
        !vfs::exists("/g27duplicate.elf") && duplicate.diagnosticCount != 0;
    print_marker("phase27g_duplicate_local", duplicateLocal);

    const bool failureRecovery = expressionProof && unknownIdentifier &&
        compiler::compile("/g27expr.c", "/g27reco.elf", &expression) &&
        run_expected("/g27reco.elf", 42);
    print_marker("phase27g_failure_recovery", failureRecovery);

    const bool artifactEvidence27g = emit_serial_artifact("/g27local.elf", "g27local");
    print_marker("phase27g_artifact", artifactEvidence27g);

    int32_t developerStudio27gReturn = 1;
    NativeElfRunReport developerStudio27gReport = {};
    const bool ideProgram = allPassed27d &&
        run_file("/Apps/DS27G/bin/amd64/p27g.elf", &developerStudio27gReturn,
                 &developerStudio27gReport) && developerStudio27gReturn == 0 &&
        developerStudio27gReport.teardownComplete;
    print_marker("phase27g_ide_program", ideProgram);
    print_marker("phase27g_source_edit", ideProgram);
    print_marker("phase27g_kernel_survival", ideProgram && developerStudio27gReport.finalState == NativeAppExecutionState::Cleaned);
    const bool phase27gPassed = expressionProof && localsProof && assignmentProof && precedenceProof &&
        unaryProof && multipleLogs && deterministicProof && unknownIdentifier && duplicateLocal &&
        failureRecovery && artifactEvidence27g && ideProgram;
    print_marker("phase27g", phase27gPassed);
    serial::puts(phase27gPassed ? "ELF Loader: Phase 27G bootstrap language smoke PASS\n"
                                : "ELF Loader: Phase 27G bootstrap language smoke FAIL\n");
#endif
#else
    serial::puts("ELF Loader: Phase 27C unavailable on non-AMD64\n");
#endif
}

} // namespace native_elf
} // namespace kernel
