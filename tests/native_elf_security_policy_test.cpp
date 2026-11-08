#include "../kernel/core/include/kernel/native_elf_security_policy.h"

#include <stdio.h>
#include <string.h>

using namespace kernel::native_elf::security;

static unsigned int failures = 0;
static unsigned int nativeEntryCalls = 0;

static void expect(bool condition, const char* name)
{
    if (condition) {
        printf("PASS %s\n", name);
    } else {
        printf("FAIL %s\n", name);
        ++failures;
    }
}

static SourceState approved_source()
{
    SourceState source{};
    source.mountPresent = true;
    source.rootMount = true;
    source.supportedFilesystem = true;
    source.vfsReadOnly = true;
    source.alias = false;
    source.nestedAppsMount = false;
    source.blockDevicePresent = true;
    source.blockDeviceReadOnly = true;
    source.releaseRamdisk = true;
    source.generationMatches = true;
    source.mountIndex = 1;
    source.blockDeviceIndex = 2;
    source.volumeIndex = 0;
    source.deviceGeneration = 17;
    return source;
}

static PackageIdentity approved_package()
{
    PackageIdentity package{};
    package.directory = "PacMan";
    package.id = "com.guidexos.pacman";
    package.displayName = "Nexgen PacMan";
    package.kind = "NativeElf";
    package.runtime = "native-elf";
    package.executable = "bin/amd64/pacman.elf";
    package.architecture = "amd64";
    package.entryPoint = "gx_main";
    package.abi = "guidexos-c-abi-v1";
    package.minimumServerVersion = "0.1.0";
    package.manifestBytes = kPacManManifestBytes;
    package.executableBytes = kPacManExecutableBytes;
    package.manifestSha256 = kPacManManifestSha256;
    package.executableSha256 = kPacManExecutableSha256;
    return package;
}

static bool try_native_entry_between(const SourceState& sourceBefore,
                                     const SourceState& sourceNow,
                                     const PackageIdentity& package)
{
    if (!same_source(sourceBefore, sourceNow) ||
        !native_entry_allowed(sourceNow, package)) return false;
    ++nativeEntryCalls;
    return true;
}

static bool try_native_entry(const SourceState& source, const PackageIdentity& package)
{
    return try_native_entry_between(source, source, package);
}

int main()
{
    SourceState source = approved_source();
    PackageIdentity package = approved_package();
    expect(native_entry_allowed(source, package), "approved PacMan on read-only release image is allowed");
    expect(try_native_entry(source, package) && nativeEntryCalls == 1,
           "approved policy reaches the synthetic native-entry sentinel");

    SourceState writable = source;
    writable.vfsReadOnly = false;
    expect(!try_native_entry(writable, package) && nativeEntryCalls == 1,
           "writable package storage is rejected before entry");
    writable = source;
    writable.blockDeviceReadOnly = false;
    expect(!try_native_entry(writable, package) && nativeEntryCalls == 1,
           "writable backing device is rejected despite read-only mount metadata");

    uint8_t alteredExecutableDigest[32];
    memcpy(alteredExecutableDigest, kPacManExecutableSha256, sizeof(alteredExecutableDigest));
    alteredExecutableDigest[0] ^= 0x80;
    PackageIdentity altered = package;
    altered.executableSha256 = alteredExecutableDigest;
    expect(!try_native_entry(source, altered) && nativeEntryCalls == 1,
           "altered executable identity is rejected before entry");

    PackageIdentity unexpected = package;
    unexpected.directory = "OtherNativeApp";
    expect(!try_native_entry(source, unexpected) && nativeEntryCalls == 1,
           "unexpected Native ELF package is rejected before entry");
    expect(!package_inventory_allowed(2, true, true),
           "ambiguous package inventory fails closed");
    expect(package_inventory_allowed(1, false, true),
           "single approved package inventory is accepted");

    PackageIdentity unsupportedServer = package;
    unsupportedServer.minimumServerVersion = "0.4.0";
    expect(!try_native_entry(source, unsupportedServer) && nativeEntryCalls == 1,
           "unsupported Server version contract is rejected before entry");
    PackageIdentity incompatibleAbi = package;
    incompatibleAbi.abi = "guidexos-c-abi-v2";
    expect(!try_native_entry(source, incompatibleAbi) && nativeEntryCalls == 1,
           "incompatible host ABI layout is rejected before entry");
    PackageIdentity unsupportedArchitecture = package;
    unsupportedArchitecture.architecture = "arm64";
    expect(!try_native_entry(source, unsupportedArchitecture) && nativeEntryCalls == 1,
           "unsupported CPU architecture is rejected before entry");

    PackageIdentity missingExecutable = package;
    missingExecutable.executableBytes = 0;
    missingExecutable.executableSha256 = nullptr;
    expect(!try_native_entry(source, missingExecutable) && nativeEntryCalls == 1,
           "missing executable is rejected before entry");
    PackageIdentity missingIntegrity = package;
    missingIntegrity.manifestBytes = 0;
    missingIntegrity.manifestSha256 = nullptr;
    expect(!try_native_entry(source, missingIntegrity) && nativeEntryCalls == 1,
           "missing or invalid manifest integrity metadata is rejected before entry");

    SourceState writableAlias = source;
    writableAlias.alias = true;
    expect(!try_native_entry(writableAlias, package) && nativeEntryCalls == 1,
           "writable alias is rejected before entry");
    SourceState overlay = source;
    overlay.nestedAppsMount = true;
    expect(!try_native_entry(overlay, package) && nativeEntryCalls == 1,
           "alternate Apps mount or overlay is rejected before entry");
    SourceState ambiguousOwner = source;
    ambiguousOwner.mountPresent = false;
    expect(!try_native_entry(ambiguousOwner, package) && nativeEntryCalls == 1,
           "unresolvable mount ownership is rejected before entry");
    ambiguousOwner = source;
    ambiguousOwner.blockDevicePresent = false;
    expect(!try_native_entry(ambiguousOwner, package) && nativeEntryCalls == 1,
           "missing backing-device ownership is rejected before entry");

    SourceState changedGeneration = source;
    ++changedGeneration.deviceGeneration;
    expect(!same_source(source, changedGeneration),
           "storage generation change invalidates verification");
    expect(!try_native_entry_between(source, changedGeneration, package) && nativeEntryCalls == 1,
           "storage replacement between validation and execution is rejected");

    expect(kPacManResourceCount == 9u,
           "all nine PacMan runtime resources have pinned integrity identities");
    bool allResourcePinsBounded = true;
    for (uint32_t i = 0; i < kPacManResourceCount; ++i) {
        if (!kPacManResources[i].path || kPacManResources[i].bytes == 0 ||
            kPacManResources[i].bytes > kMaxPacManResourceBytes) {
            allResourcePinsBounded = false;
        }
    }
    expect(allResourcePinsBounded, "runtime resource pins are complete and bounded");

    static const uint8_t expectedLevel1Sha256[32] = {
        0x53,0x5b,0x37,0xe6,0x6d,0x71,0x59,0x07,
        0x8d,0xf8,0xa1,0x6c,0xe0,0x59,0xa6,0xf7,
        0xab,0x03,0x85,0x88,0xcc,0x98,0x75,0xe1,
        0x52,0x03,0x3d,0x04,0x6f,0xf4,0x7f,0xe1
    };
    expect(kPacManResourceCount > 0u &&
           strcmp(kPacManResources[0].path, "resources/level1.gximg") == 0 &&
           kPacManResources[0].bytes == 888860u &&
           digest_matches(kPacManResources[0].sha256, expectedLevel1Sha256),
           "level1 image trust pin matches the approved package asset");

    static const uint8_t expectedExtralifeSha256[32] = {
        0x76,0x7d,0xb1,0x55,0x21,0x30,0xe6,0x76,
        0x39,0x8c,0x12,0x1e,0xc6,0x05,0xf9,0x0f,
        0x20,0xc8,0x9b,0xbb,0xab,0x27,0x23,0x05,
        0xbc,0x78,0x1a,0xdc,0x8e,0x91,0xd1,0xcd
    };
    expect(kPacManResourceCount > 3u &&
           strcmp(kPacManResources[3].path, "resources/audio/extralife.wav") == 0 &&
           kPacManResources[3].bytes == 21248u &&
           digest_matches(kPacManResources[3].sha256, expectedExtralifeSha256),
           "extralife WAV trust pin matches the approved package asset");

    expect(pacman_guest_smoke_allowed(true, true, false, 1u, 2u),
           "guest smoke pass requires approved PacMan, a clean exit, a frame, and key input");
    expect(!pacman_guest_smoke_allowed(false, true, false, 1u, 2u) &&
           !pacman_guest_smoke_allowed(true, false, false, 1u, 2u) &&
           !pacman_guest_smoke_allowed(true, true, true, 1u, 2u) &&
           !pacman_guest_smoke_allowed(true, true, false, 0u, 2u) &&
           !pacman_guest_smoke_allowed(true, true, false, 1u, 1u),
           "guest smoke pass fails closed when any qualification proof is missing");

    unsigned int registryCount = 1;
    PackageIdentity rejected = package;
    rejected.id = "com.example.unauthorized";
    if (!try_native_entry(source, rejected)) {
        // Discovery's documented fail-closed behavior drops the entire
        // unsupported inventory; the synthetic registry remains coherent.
        registryCount = 0;
    }
    expect(registryCount == 0 && nativeEntryCalls == 1,
           "rejected package does not corrupt the registry or reach entry");

    PackageIdentity repeat = approved_package();
    unsigned int repeatedSuccesses = 0;
    for (unsigned int i = 0; i < 3; ++i) {
        if (try_native_entry(source, repeat)) ++repeatedSuccesses;
    }
    expect(repeatedSuccesses == 3 && nativeEntryCalls == 4,
           "repeated authorized launches pass the same policy gate");

    printf("Security policy regression result: %s failures=%u nativeEntryCalls=%u\n",
           failures == 0 ? "PASS" : "FAIL", failures, nativeEntryCalls);
    return failures == 0 ? 0 : 1;
}
