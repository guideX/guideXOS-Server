//
// Opt-in Phase 27C diagnostic route.
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
#else
    serial::puts("ELF Loader: Phase 27C unavailable on non-AMD64\n");
#endif
}

} // namespace native_elf
} // namespace kernel
