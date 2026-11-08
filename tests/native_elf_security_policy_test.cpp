#include "../kernel/core/include/kernel/native_elf_security_policy.h"

#include <stdio.h>
#include <string.h>

using namespace kernel::native_elf::security;

static unsigned int failures = 0;
static unsigned int nativeEntryCalls = 0;

struct ExpectedResourcePin {
    const char* path;
    uint32_t bytes;
    const char* sha256;
};

static uint8_t hex_nibble(char value)
{
    if (value >= '0' && value <= '9') return static_cast<uint8_t>(value - '0');
    if (value >= 'a' && value <= 'f') return static_cast<uint8_t>(value - 'a' + 10);
    if (value >= 'A' && value <= 'F') return static_cast<uint8_t>(value - 'A' + 10);
    return 0xffu;
}

static bool digest_matches_hex(const uint8_t* actual, const char* expected)
{
    if (!actual || !expected || strlen(expected) != 64u) return false;
    for (uint32_t i = 0; i < 32u; ++i) {
        const uint8_t high = hex_nibble(expected[i * 2u]);
        const uint8_t low = hex_nibble(expected[i * 2u + 1u]);
        if (high == 0xffu || low == 0xffu || actual[i] != static_cast<uint8_t>((high << 4) | low)) {
            return false;
        }
    }
    return expected[64] == '\0';
}

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

    static const ExpectedResourcePin expectedResources[] = {
        { "resources/level1.gximg", 888860u, "535b37e66d7159078df8a16ce059a6f7ab038588cc9875e152033d046ff47fe1" },
        { "resources/pacpics.gximg", 360476u, "57a605df8f24ac9641ff6c9d39df551a17ffbcd68466a561adf5f045de259d50" },
        { "resources/audio/EatPill.wav", 7156u, "510a52e5776573197aef14d77e393185b6c7148d1b59dac535d45c5ca0498686" },
        { "resources/audio/extralife.wav", 21248u, "767db1552130e676398c121ec605f90f20c89bbbab272305bc781adc8e91d1cd" },
        { "resources/audio/fruiteat.wav", 5120u, "82c96a997ac2e837de4e418f29f4f4fa6142dd2a0c38f764b6d989d8bd98605d" },
        { "resources/audio/ghosteat.wav", 9090u, "225168072b38671c89c1fc775d513ca86fbb899e3e82c8032445f6a6dc5e1b95" },
        { "resources/audio/killed.wav", 17008u, "1c3e91df8e19041ea5783185c5a8a9f2321993b43481c215d8caf3df590290de" },
        { "resources/audio/StartMusic.wav", 68318u, "062cdc7196c54033f8bf11878d6659fd5b0ba11cd6945640ec8fac4376919421" },
        { "resources/audio/startmusicold.wav", 46592u, "a6e1c14f6cbc59cb5244c0119c728d412252370eea48bc574c1307a774c83caf" }
    };
    bool allResourcePinsMatch = kPacManResourceCount ==
        static_cast<uint32_t>(sizeof(expectedResources) / sizeof(expectedResources[0]));
    for (uint32_t i = 0; i < kPacManResourceCount && allResourcePinsMatch; ++i) {
        allResourcePinsMatch = strcmp(kPacManResources[i].path, expectedResources[i].path) == 0 &&
            kPacManResources[i].bytes == expectedResources[i].bytes &&
            digest_matches_hex(kPacManResources[i].sha256, expectedResources[i].sha256);
    }
    expect(allResourcePinsMatch,
           "all nine PacMan resource pins match the approved package asset hashes");

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
