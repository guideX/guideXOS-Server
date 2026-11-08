#ifndef KERNEL_NATIVE_ELF_SECURITY_POLICY_H
#define KERNEL_NATIVE_ELF_SECURITY_POLICY_H

#include <stdint.h>

namespace kernel {
namespace native_elf {
namespace security {

// S22R18 public-preview trust anchor. These values identify the exact
// first-party PacMan manifest and executable selected for Server 0.3.0.
// Update them only with a separately reviewed and requalified candidate.
static const uint32_t kPacManManifestBytes = 956u;
static const uint32_t kPacManExecutableBytes = 63640u;
static const uint32_t kMaxPacManResourceBytes = 1024u * 1024u;
static const char kTargetServerVersion[] = "0.3.0";

static const uint8_t kPacManManifestSha256[32] = {
    0x3f, 0x35, 0x92, 0xe6, 0xc7, 0x60, 0x53, 0xe3,
    0x23, 0xf1, 0x6d, 0x48, 0x07, 0x3e, 0x98, 0xc6,
    0x7d, 0x7f, 0x57, 0x02, 0xb8, 0xe4, 0x7f, 0x0b,
    0x50, 0x7d, 0x6b, 0xfb, 0x3d, 0x12, 0x64, 0x49
};

static const uint8_t kPacManExecutableSha256[32] = {
    0xee, 0xb5, 0x44, 0xbb, 0xb2, 0xe2, 0x57, 0x4e,
    0xea, 0xed, 0x7b, 0xb7, 0xf0, 0xba, 0xd6, 0x1e,
    0xa2, 0x4f, 0x69, 0x45, 0x23, 0x1b, 0x77, 0x57,
    0x22, 0x3a, 0x22, 0x5b, 0x30, 0xb8, 0xe1, 0xcc
};

struct PackageFileIdentity {
    const char* path;
    uint32_t bytes;
    uint8_t sha256[32];
};

// All package-relative data files consumed by the approved PacMan binary.
// Each is checked from the same read-only release-image mount before entry.
static const PackageFileIdentity kPacManResources[] = {
    { "resources/level1.gximg", 888860u, {
        0x53,0x5b,0x37,0xe6,0xd6,0x71,0x59,0x07,0x8d,0xf8,0xa1,0x6c,0xe0,0x59,0xa6,0xf7,
        0xab,0x03,0x85,0x88,0xcc,0x98,0x75,0xe1,0x52,0x03,0x3d,0x04,0x6f,0xf4,0x7f,0xe1 } },
    { "resources/pacpics.gximg", 360476u, {
        0x57,0xa6,0x05,0xdf,0x8f,0x24,0xac,0x96,0x41,0xff,0x6c,0x9d,0x39,0xdf,0x55,0x1a,
        0x17,0xff,0xbc,0xd6,0x84,0x66,0xa5,0x61,0xad,0xf5,0xf0,0x45,0xde,0x25,0x9d,0x50 } },
    { "resources/audio/EatPill.wav", 7156u, {
        0x51,0x0a,0x52,0xe5,0x77,0x65,0x73,0x19,0x7a,0xef,0x14,0xd7,0x7e,0x39,0x31,0x85,
        0xb6,0xc7,0x14,0x8d,0x1b,0x59,0xda,0xc5,0x35,0xd4,0x5c,0x5c,0xa0,0x49,0x86,0x86 } },
    { "resources/audio/extralife.wav", 21248u, {
        0x76,0x7d,0xb1,0x55,0x21,0x30,0xe6,0x76,0x39,0x8c,0x12,0x1e,0xc6,0x05,0xf9,0x0f,
        0x20,0xc8,0x9b,0xbb,0xab,0x27,0x23,0x05,0xbc,0x78,0x1a,0xdc,0x8e,0x91,0xd1,0xcd } },
    { "resources/audio/fruiteat.wav", 5120u, {
        0x82,0xc9,0x6a,0x99,0x76,0x52,0xac,0xe8,0x2e,0x4e,0x41,0x84,0xf2,0xf4,0xf4,0xfa,
        0x61,0x42,0xdd,0x2a,0x0c,0x38,0xf7,0x64,0xb6,0xd9,0x89,0xd8,0xbd,0x98,0x60,0x5d } },
    { "resources/audio/ghosteat.wav", 9090u, {
        0x22,0x51,0x68,0x07,0x2b,0x38,0x67,0x1c,0x89,0xc1,0xfc,0x77,0x5d,0x51,0x3c,0xa8,
        0x6f,0xbb,0x89,0x9e,0x3e,0x82,0xc8,0x03,0x24,0x45,0xf6,0xa6,0xdc,0x5e,0x1b,0x95 } },
    { "resources/audio/killed.wav", 17008u, {
        0x1c,0x3e,0x91,0xdf,0x8e,0x19,0x04,0x1e,0xa5,0x78,0x31,0x8c,0x5f,0xa8,0xa9,0xf2,
        0x32,0x19,0x93,0xb4,0x34,0x81,0xc2,0x15,0xd8,0xca,0xf3,0xdf,0x59,0x02,0x90,0xde } },
    { "resources/audio/StartMusic.wav", 68318u, {
        0x06,0x2c,0xdc,0x71,0x96,0xc5,0x40,0x33,0xf8,0xbf,0x11,0x8f,0x78,0x66,0x59,0xfd,
        0x5b,0x0b,0xa1,0x1c,0xd6,0x94,0x56,0x40,0xec,0x8f,0xac,0x43,0x76,0x91,0x94,0x21 } },
    { "resources/audio/startmusicold.wav", 46592u, {
        0xa6,0xe1,0x4c,0x1f,0x6c,0xb9,0x59,0x89,0x70,0xf8,0x1a,0x07,0x74,0xc8,0x3c,0xaf,
        0x6c,0x13,0x85,0x30,0xa7,0x74,0x8c,0xb5,0x74,0xc1,0x30,0x7a,0x77,0x4c,0x83,0xaf } }
};
static const uint32_t kPacManResourceCount =
    static_cast<uint32_t>(sizeof(kPacManResources) / sizeof(kPacManResources[0]));

struct SourceState {
    bool mountPresent;
    bool rootMount;
    bool supportedFilesystem;
    bool vfsReadOnly;
    bool alias;
    bool nestedAppsMount;
    bool blockDevicePresent;
    bool blockDeviceReadOnly;
    bool releaseRamdisk;
    bool generationMatches;
    uint8_t mountIndex;
    uint8_t blockDeviceIndex;
    uint8_t volumeIndex;
    uint64_t deviceGeneration;
};

inline bool source_allowed(const SourceState& source) {
    return source.mountPresent && source.rootMount && source.supportedFilesystem &&
        source.vfsReadOnly && !source.alias && !source.nestedAppsMount &&
        source.blockDevicePresent && source.blockDeviceReadOnly &&
        source.releaseRamdisk && source.generationMatches &&
        source.deviceGeneration != 0;
}

inline bool same_source(const SourceState& before, const SourceState& after) {
    return source_allowed(before) && source_allowed(after) &&
        before.mountIndex == after.mountIndex &&
        before.blockDeviceIndex == after.blockDeviceIndex &&
        before.volumeIndex == after.volumeIndex &&
        before.deviceGeneration == after.deviceGeneration;
}

inline bool package_inventory_allowed(uint32_t directoryCount,
                                      bool unexpectedDirectory,
                                      bool approvedPackageFound) {
    return directoryCount == 1u && !unexpectedDirectory && approvedPackageFound;
}

struct PackageIdentity {
    const char* directory;
    const char* id;
    const char* displayName;
    const char* kind;
    const char* runtime;
    const char* executable;
    const char* architecture;
    const char* entryPoint;
    const char* abi;
    const char* minimumServerVersion;
    uint32_t manifestBytes;
    uint32_t executableBytes;
    const uint8_t* manifestSha256;
    const uint8_t* executableSha256;
};

inline bool exact_text(const char* actual, const char* expected) {
    if (!actual || !expected) return false;
    while (*actual && *expected && *actual == *expected) {
        ++actual;
        ++expected;
    }
    return *actual == '\0' && *expected == '\0';
}

inline bool digest_matches(const uint8_t* actual, const uint8_t* expected) {
    if (!actual || !expected) return false;
    uint8_t difference = 0;
    for (uint32_t i = 0; i < 32u; ++i) difference |= actual[i] ^ expected[i];
    return difference == 0;
}

inline bool manifest_allowed(const PackageIdentity& package) {
    return exact_text(package.directory, "PacMan") &&
        exact_text(package.id, "com.guidexos.pacman") &&
        exact_text(package.displayName, "Nexgen PacMan") &&
        exact_text(package.kind, "NativeElf") &&
        exact_text(package.runtime, "native-elf") &&
        exact_text(package.executable, "bin/amd64/pacman.elf") &&
        exact_text(package.architecture, "amd64") &&
        exact_text(package.entryPoint, "gx_main") &&
        exact_text(package.abi, "guidexos-c-abi-v1") &&
        exact_text(package.minimumServerVersion, "0.1.0") &&
        package.manifestBytes == kPacManManifestBytes &&
        package.executableBytes == kPacManExecutableBytes &&
        digest_matches(package.manifestSha256, kPacManManifestSha256);
}

inline bool package_allowed(const PackageIdentity& package) {
    return manifest_allowed(package) &&
        digest_matches(package.executableSha256, kPacManExecutableSha256);
}

inline bool native_entry_allowed(const SourceState& source,
                                 const PackageIdentity& package) {
    return source_allowed(source) && package_allowed(package);
}

} // namespace security
} // namespace native_elf
} // namespace kernel

#endif // KERNEL_NATIVE_ELF_SECURITY_POLICY_H
