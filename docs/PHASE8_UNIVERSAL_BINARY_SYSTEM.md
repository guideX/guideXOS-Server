# Phase 8: Universal Binary System & Developer SDK

**Status:** ?? PLANNING  
**Previous Phase:** Phase 7 (Testing & QA)  
**Goal:** Enable "compile once, run anywhere" for guideXOS applications

---

## ?? Executive Summary

Phase 8 introduces the **Universal Binary System** - guideXOS's signature feature that allows a single application package (`.gxapp`) to contain optimized native binaries for multiple CPU architectures. Combined with a first-class **Developer SDK**, this positions guideXOS as a truly portable server operating system.

### Key Deliverables
1. **ArchitectureDetector** - Runtime CPU detection API
2. **GXAPP Container Format** - Universal application package format
3. **UniversalAppLoader** - Architecture-aware application loader
4. **gxbuild CLI Tool** - Cross-compilation and packaging tool
5. **guideXOS SDK** - Headers, libraries, and toolchain
6. **musl libc Integration** - POSIX-compatible userspace C library

### Design Decisions (Locked)
| Decision | Choice | Rationale |
|----------|--------|-----------|
| Syscall ABI | POSIX-compatible + Native | Easier porting + Full control |
| Binary Format | ELF per-architecture | Industry standard, tool support |
| gxbuild Language | C++ (native) | Consistent with kernel, no runtime deps |
| libc | musl | POSIX-complete, static-link friendly |
| Initial Architectures | x86, amd64, + 1 exotic | Start small, prove concept |

### Estimated Duration
- **Phase 8a (Foundation):** 4-6 weeks
- **Phase 8b (SDK & Toolchain):** 6-8 weeks  
- **Phase 8c (Universal Loader):** 3-4 weeks
- **Phase 8d (gxbuild Tool):** 4-6 weeks
- **Phase 8e (Package Manager):** 4-6 weeks
- **Total:** ~6-8 months (iterative)

---

## ??? Architecture Overview

```
???????????????????????????????????????????????????????????????????
?                     DEVELOPER WORKFLOW                          ?
???????????????????????????????????????????????????????????????????
?                                                                 ?
?   source.cpp ??? gxbuild ??? myapp.gxapp                       ?
?                     ?                                           ?
?         ???????????????????????                                ?
?         ?          ?          ?                                ?
?   [x86 ELF]  [amd64 ELF]  [riscv64 ELF]                       ?
?         ?          ?          ?                                ?
?         ???????????????????????                                ?
?                    ?                                           ?
?            ????????????????                                    ?
?            ?  .gxapp file ?                                    ?
?            ?  ??????????? ?                                    ?
?            ?  ? header  ? ?                                    ?
?            ?  ? meta.json? ?                                    ?
?            ?  ? bin/x86 ? ?                                    ?
?            ?  ? bin/amd64? ?                                    ?
?            ?  ? bin/riscv? ?                                    ?
?            ?  ??????????? ?                                    ?
?            ????????????????                                    ?
???????????????????????????????????????????????????????????????????

???????????????????????????????????????????????????????????????????
?                     RUNTIME LOADING                             ?
???????????????????????????????????????????????????????????????????
?                                                                 ?
?   UniversalAppLoader.run("myapp.gxapp")                        ?
?         ?                                                       ?
?         ?                                                       ?
?   ArchitectureDetector::get() ??? CpuArchitecture::AMD64       ?
?         ?                                                       ?
?         ?                                                       ?
?   GXAppContainer::open("myapp.gxapp")                          ?
?         ?                                                       ?
?         ?                                                       ?
?   Extract bin/amd64/app.elf                                    ?
?         ?                                                       ?
?         ?                                                       ?
?   ELF Loader ??? Map into memory ??? Execute                   ?
?                                                                 ?
???????????????????????????????????????????????????????????????????
```

---

## ?? Phase 8a: Foundation (ArchitectureDetector)

### Goal
Create a runtime CPU architecture detection system that works in both kernel and userspace contexts.

### Deliverables

#### 1. CpuArchitecture Enum
```cpp
// kernel/core/include/kernel/cpu_arch.h

namespace kernel {

enum class CpuArchitecture : uint8_t {
    Unknown = 0,
    X86,            // 32-bit x86 (i386, i686)
    AMD64,          // 64-bit x86-64
    ARM,            // 32-bit ARM (ARMv7)
    ARM64,          // 64-bit ARM (AArch64)
    IA64,           // Intel Itanium
    SPARC,          // 32-bit SPARC (v8)
    SPARC64,        // 64-bit SPARC (v9)
    MIPS64,         // 64-bit MIPS
    PPC64,          // 64-bit PowerPC
    RISCV64,        // 64-bit RISC-V
    LOONGARCH64,    // 64-bit LoongArch
    
    // Sentinel for iteration
    _Count
};

// String conversion
const char* cpu_arch_to_string(CpuArchitecture arch);
CpuArchitecture cpu_arch_from_string(const char* name);

// Size information
bool cpu_arch_is_64bit(CpuArchitecture arch);
size_t cpu_arch_pointer_size(CpuArchitecture arch);

} // namespace kernel
```

#### 2. ArchitectureDetector Class
```cpp
// kernel/core/include/kernel/arch_detector.h

namespace kernel {

class ArchitectureDetector {
public:
    // Get the architecture of the currently running CPU
    // This is determined at compile-time but exposed as runtime API
    static CpuArchitecture get();
    
    // Get detailed CPU information
    static const char* get_cpu_vendor();      // e.g., "GenuineIntel", "AuthenticAMD"
    static const char* get_cpu_model();       // e.g., "Intel Core i7-9700K"
    static uint32_t get_cpu_features();       // Bitmask of features (SSE, AVX, etc.)
    
    // Check if a specific architecture binary can run on this CPU
    // (For future ABI translation support)
    static bool can_execute(CpuArchitecture arch);
    
private:
    static CpuArchitecture s_detected;
    static bool s_initialized;
};

} // namespace kernel
```

### Implementation Tasks

| Task | Description | Complexity |
|------|-------------|------------|
| 8a.1 | Create `cpu_arch.h` with enum and helpers | Low |
| 8a.2 | Create `arch_detector.h` header | Low |
| 8a.3 | Implement compile-time detection in `arch_detector.cpp` | Medium |
| 8a.4 | Add runtime CPUID for x86/amd64 feature detection | Medium |
| 8a.5 | Add device-tree parsing for ARM/RISC-V info | Medium |
| 8a.6 | Unit tests for architecture detection | Low |

### Files to Create/Modify
- `kernel/core/include/kernel/cpu_arch.h` (new)
- `kernel/core/include/kernel/arch_detector.h` (new)
- `kernel/core/arch_detector.cpp` (new)
- `kernel/core/cpu_arch.cpp` (new)

---

## ?? Phase 8b: SDK & Toolchain

### Goal
Create a complete developer SDK with cross-compilation toolchains for all target architectures.

### Deliverables

#### 1. SDK Directory Structure
```
/opt/guidexos-sdk/
??? bin/
?   ??? gxbuild                    # Universal build tool
?   ??? gxpackage                  # Package creation tool
?   ??? gxrun                      # Local runner/emulator wrapper
??? include/
?   ??? guidexos/
?   ?   ??? types.h                # Basic types
?   ?   ??? syscall.h              # System call interface
?   ?   ??? process.h              # Process management
?   ?   ??? fs.h                   # Filesystem API
?   ?   ??? net.h                  # Networking API
?   ?   ??? ipc.h                  # Inter-process communication
?   ?   ??? gui.h                  # GUI API (optional)
?   ??? posix/                     # POSIX compatibility headers
?       ??? unistd.h
?       ??? fcntl.h
?       ??? sys/
?       ??? ...
??? lib/
?   ??? x86/
?   ?   ??? libc.a                 # musl libc (static)
?   ?   ??? libguidexos.a          # guideXOS system library
?   ?   ??? crt0.o                 # C runtime startup
?   ??? amd64/
?   ?   ??? ...
?   ??? riscv64/
?       ??? ...
??? toolchain/
?   ??? x86-guidexos-gcc           # Cross-compiler symlinks/wrappers
?   ??? amd64-guidexos-gcc
?   ??? riscv64-guidexos-gcc
??? share/
    ??? templates/                 # Project templates
    ??? examples/                  # Example applications
    ??? docs/                      # Documentation
```

#### 2. System Call Interface

guideXOS will support a dual syscall ABI:

**Native guideXOS Syscalls** (preferred for new apps):
```cpp
// include/guidexos/syscall.h

namespace gxos {

// Process management
[[nodiscard]] pid_t sys_spawn(const char* path, const char** argv);
[[nodiscard]] int sys_exit(int code);
[[nodiscard]] pid_t sys_getpid();
[[nodiscard]] int sys_wait(pid_t pid, int* status);

// Filesystem
[[nodiscard]] fd_t sys_open(const char* path, uint32_t flags);
[[nodiscard]] ssize_t sys_read(fd_t fd, void* buf, size_t count);
[[nodiscard]] ssize_t sys_write(fd_t fd, const void* buf, size_t count);
[[nodiscard]] int sys_close(fd_t fd);

// Memory
[[nodiscard]] void* sys_mmap(void* addr, size_t len, uint32_t prot, uint32_t flags);
[[nodiscard]] int sys_munmap(void* addr, size_t len);

// Networking
[[nodiscard]] fd_t sys_socket(int domain, int type, int protocol);
[[nodiscard]] int sys_bind(fd_t sock, const sockaddr* addr, socklen_t len);
[[nodiscard]] int sys_listen(fd_t sock, int backlog);
[[nodiscard]] fd_t sys_accept(fd_t sock, sockaddr* addr, socklen_t* len);
[[nodiscard]] int sys_connect(fd_t sock, const sockaddr* addr, socklen_t len);

// IPC
[[nodiscard]] int sys_pipe(fd_t fds[2]);
[[nodiscard]] key_t sys_shmget(size_t size, int flags);
[[nodiscard]] void* sys_shmat(key_t key);

} // namespace gxos
```

**POSIX Compatibility Layer** (for porting existing software):
```cpp
// Implemented in musl, wraps native syscalls
// Standard POSIX functions: open(), read(), write(), fork(), exec(), etc.
```

#### 3. musl libc Integration

musl will be cross-compiled for each target architecture and statically linked into applications.

**Build Process:**
```bash
# For each architecture
./configure --target=x86-guidexos \
            --prefix=/opt/guidexos-sdk \
            --syslibdir=/opt/guidexos-sdk/lib/x86 \
            --disable-shared \
            CFLAGS="-O2 -fPIC"
make
make install
```

**Syscall Backend:**
musl's `src/internal/syscall.h` will be modified to call guideXOS native syscalls instead of Linux syscalls.

### Implementation Tasks

| Task | Description | Complexity |
|------|-------------|------------|
| 8b.1 | Define syscall numbers and ABI | Medium |
| 8b.2 | Implement syscall dispatch in kernel | High |
| 8b.3 | Port musl libc for x86 target | High |
| 8b.4 | Port musl libc for amd64 target | High |
| 8b.5 | Port musl libc for riscv64 target | High |
| 8b.6 | Create libguidexos (native API wrapper) | Medium |
| 8b.7 | Create crt0.o startup files per arch | Medium |
| 8b.8 | Create SDK directory structure | Low |
| 8b.9 | Create cross-compiler wrapper scripts | Low |
| 8b.10 | Write "Hello World" test application | Low |

---

## ?? Phase 8c: GXAPP Container Format

### Goal
Define and implement the `.gxapp` universal application container format.

### File Format Specification

#### Overview
A `.gxapp` file is a ZIP-like archive with a specific structure:

```
myapp.gxapp (ZIP archive)
?
??? GXAPP                          # Magic file (identifies as gxapp)
??? metadata.json                  # Application metadata
??? signature.sig                  # Optional: Ed25519 signature
?
??? bin/                           # Architecture-specific binaries
    ??? x86/
    ?   ??? app.elf                # 32-bit x86 ELF binary
    ??? amd64/
    ?   ??? app.elf                # 64-bit x86-64 ELF binary
    ??? riscv64/
        ??? app.elf                # 64-bit RISC-V ELF binary
```

#### GXAPP Magic File
```
GXAPP\n
VERSION: 1\n
```

#### metadata.json Schema
```json
{
  "$schema": "https://guidexos.org/schemas/gxapp-v1.json",
  "name": "MyApplication",
  "version": "1.0.0",
  "description": "A sample guideXOS application",
  "author": "Developer Name",
  "license": "MIT",
  "homepage": "https://example.com",
  
  "guidexos": {
    "min_version": "1.0.0",
    "max_version": null
  },
  
  "architectures": {
    "x86": {
      "entry": "bin/x86/app.elf",
      "size": 45056,
      "sha256": "abc123..."
    },
    "amd64": {
      "entry": "bin/amd64/app.elf",
      "size": 52224,
      "sha256": "def456..."
    },
    "riscv64": {
      "entry": "bin/riscv64/app.elf",
      "size": 48128,
      "sha256": "789ghi..."
    }
  },
  
  "dependencies": [],
  
  "permissions": [
    "filesystem.read",
    "filesystem.write",
    "network.client"
  ]
}
```

### C++ API

```cpp
// kernel/core/include/kernel/gxapp.h

namespace kernel {

// Result type for operations that can fail
template<typename T>
struct Result {
    bool ok;
    T value;
    const char* error;
};

// Architecture entry in a gxapp
struct GXAppArchEntry {
    CpuArchitecture arch;
    const char* entry_path;
    size_t size;
    uint8_t sha256[32];
};

// Metadata from a gxapp
struct GXAppMetadata {
    char name[64];
    char version[16];
    char description[256];
    char author[64];
    
    struct {
        char min_version[16];
        char max_version[16];
    } guidexos_compat;
    
    size_t arch_count;
    GXAppArchEntry architectures[16];  // Max 16 architectures
};

// GXAPP Container reader
class GXAppContainer {
public:
    // Open a gxapp file
    static Result<GXAppContainer*> open(const char* path);
    
    // Close and free resources
    void close();
    
    // Get metadata
    const GXAppMetadata& metadata() const;
    
    // Check if architecture is supported
    bool supports_architecture(CpuArchitecture arch) const;
    
    // Get the list of supported architectures
    void get_supported_architectures(CpuArchitecture* out, size_t* count) const;
    
    // Extract binary for a specific architecture
    // Returns pointer to ELF data (caller must free)
    Result<uint8_t*> extract_binary(CpuArchitecture arch, size_t* out_size);
    
    // Verify integrity (check SHA256 hashes)
    bool verify_integrity();
    
    // Verify signature (if present)
    bool verify_signature(const uint8_t* public_key);
    
private:
    GXAppContainer();
    ~GXAppContainer();
    
    struct Impl;
    Impl* m_impl;
};

// GXAPP Container writer (for gxbuild tool)
class GXAppBuilder {
public:
    GXAppBuilder();
    ~GXAppBuilder();
    
    // Set metadata
    void set_name(const char* name);
    void set_version(const char* version);
    void set_description(const char* description);
    void set_author(const char* author);
    void set_min_guidexos_version(const char* version);
    
    // Add architecture binary
    bool add_binary(CpuArchitecture arch, const uint8_t* elf_data, size_t size);
    bool add_binary_from_file(CpuArchitecture arch, const char* elf_path);
    
    // Build the gxapp file
    Result<void> build(const char* output_path);
    
    // Sign the package
    Result<void> sign(const uint8_t* private_key);
    
private:
    struct Impl;
    Impl* m_impl;
};

} // namespace kernel
```

### Implementation Tasks

| Task | Description | Complexity |
|------|-------------|------------|
| 8c.1 | Define metadata.json schema | Low |
| 8c.2 | Implement minimal ZIP reader (for kernel) | High |
| 8c.3 | Implement GXAppContainer class | Medium |
| 8c.4 | Implement GXAppBuilder class | Medium |
| 8c.5 | Implement SHA256 verification | Medium |
| 8c.6 | Implement Ed25519 signature verification | Medium |
| 8c.7 | JSON parser for metadata.json | Medium |
| 8c.8 | Integration tests | Medium |

---

## ?? Phase 8d: Universal Application Loader

### Goal
Create the runtime component that loads and executes applications from `.gxapp` packages.

### C++ API

```cpp
// kernel/core/include/kernel/universal_loader.h

namespace kernel {

// Loader configuration
struct LoaderConfig {
    bool allow_unsigned;           // Allow unsigned packages
    bool verify_hashes;            // Verify SHA256 hashes
    const char* download_url;      // Repository URL for missing archs (future)
};

// Load result
struct LoadResult {
    bool success;
    pid_t pid;                     // Process ID if successful
    const char* error;             // Error message if failed
    CpuArchitecture loaded_arch;   // Which architecture was loaded
};

class UniversalAppLoader {
public:
    // Initialize with default configuration
    static void init();
    
    // Initialize with custom configuration
    static void init(const LoaderConfig& config);
    
    // Load and execute a gxapp
    static LoadResult run(const char* gxapp_path, 
                          const char** argv = nullptr,
                          const char** envp = nullptr);
    
    // Load but don't execute (for inspection)
    static Result<GXAppContainer*> load(const char* gxapp_path);
    
    // Get information about what would be loaded
    static Result<CpuArchitecture> probe(const char* gxapp_path);
    
private:
    static LoaderConfig s_config;
};

} // namespace kernel
```

### Loading Process

```
UniversalAppLoader::run("calculator.gxapp")
    ?
    ?
???????????????????????????????????????
? 1. Open gxapp container             ?
?    GXAppContainer::open()           ?
???????????????????????????????????????
    ?
    ?
???????????????????????????????????????
? 2. Detect CPU architecture          ?
?    ArchitectureDetector::get()      ?
?    Result: CpuArchitecture::AMD64   ?
???????????????????????????????????????
    ?
    ?
???????????????????????????????????????
? 3. Check architecture support       ?
?    container.supports_architecture()?
?    If no: return error              ?
???????????????????????????????????????
    ?
    ?
???????????????????????????????????????
? 4. Verify integrity (optional)      ?
?    container.verify_integrity()     ?
?    container.verify_signature()     ?
???????????????????????????????????????
    ?
    ?
???????????????????????????????????????
? 5. Extract ELF binary               ?
?    container.extract_binary(AMD64)  ?
???????????????????????????????????????
    ?
    ?
???????????????????????????????????????
? 6. Load ELF into memory             ?
?    - Parse ELF headers              ?
?    - Allocate memory regions        ?
?    - Map PT_LOAD segments           ?
?    - Resolve relocations            ?
???????????????????????????????????????
    ?
    ?
???????????????????????????????????????
? 7. Create process                   ?
?    process::create()                ?
?    - Setup stack                    ?
?    - Setup argc/argv/envp           ?
?    - Set entry point                ?
???????????????????????????????????????
    ?
    ?
???????????????????????????????????????
? 8. Execute                          ?
?    process::schedule()              ?
?    Return LoadResult with pid       ?
???????????????????????????????????????
```

### Implementation Tasks

| Task | Description | Complexity |
|------|-------------|------------|
| 8d.1 | Create UniversalAppLoader header | Low |
| 8d.2 | Implement ELF loader for userspace | High |
| 8d.3 | Implement process creation with args | Medium |
| 8d.4 | Integrate with GXAppContainer | Medium |
| 8d.5 | Error handling and logging | Low |
| 8d.6 | Test with simple ELF binaries | Medium |

---

## ?? Phase 8e: gxbuild CLI Tool

### Goal
Create the developer-facing build tool that compiles applications for multiple architectures and packages them into `.gxapp` files.

### Usage

```bash
# Basic build (current architecture only)
gxbuild build myapp.cpp

# Multi-architecture build
gxbuild build myapp.cpp --targets x86,amd64,riscv64

# Build from project file
gxbuild build project.gxproj

# Create package
gxbuild package myapp --output myapp.gxapp

# Full pipeline
gxbuild build myapp.cpp --targets x86,amd64 --package --output myapp.gxapp

# Sign package
gxbuild sign myapp.gxapp --key developer.key

# Inspect package
gxbuild info myapp.gxapp

# Run locally (in QEMU if different arch)
gxbuild run myapp.gxapp
```

### Project File Format (gxproj)

```json
{
  "name": "MyApplication",
  "version": "1.0.0",
  "type": "executable",
  
  "sources": [
    "src/*.cpp"
  ],
  
  "include_dirs": [
    "include"
  ],
  
  "targets": ["x86", "amd64", "riscv64"],
  
  "dependencies": [
    "libpng",
    "zlib"
  ],
  
  "cflags": "-O2 -Wall",
  "ldflags": ""
}
```

### Implementation (C++)

```cpp
// tools/gxbuild/main.cpp

#include <cstdio>
#include <cstring>
#include "args.h"
#include "builder.h"
#include "packager.h"

void print_usage() {
    printf("gxbuild - guideXOS Universal Build Tool\n\n");
    printf("Usage:\n");
    printf("  gxbuild build <source> [options]\n");
    printf("  gxbuild package <name> [options]\n");
    printf("  gxbuild sign <package> --key <keyfile>\n");
    printf("  gxbuild info <package>\n");
    printf("  gxbuild run <package>\n");
    printf("\nOptions:\n");
    printf("  --targets <arch,arch,...>  Target architectures\n");
    printf("  --output <file>            Output filename\n");
    printf("  --package                  Create gxapp after build\n");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }
    
    const char* command = argv[1];
    
    if (strcmp(command, "build") == 0) {
        return cmd_build(argc - 2, argv + 2);
    } else if (strcmp(command, "package") == 0) {
        return cmd_package(argc - 2, argv + 2);
    } else if (strcmp(command, "sign") == 0) {
        return cmd_sign(argc - 2, argv + 2);
    } else if (strcmp(command, "info") == 0) {
        return cmd_info(argc - 2, argv + 2);
    } else if (strcmp(command, "run") == 0) {
        return cmd_run(argc - 2, argv + 2);
    } else {
        printf("Unknown command: %s\n", command);
        print_usage();
        return 1;
    }
}
```

### Implementation Tasks

| Task | Description | Complexity |
|------|-------------|------------|
| 8e.1 | Create argument parser | Low |
| 8e.2 | Implement `build` command | High |
| 8e.3 | Implement cross-compiler invocation | Medium |
| 8e.4 | Implement `package` command | Medium |
| 8e.5 | Implement `sign` command | Low |
| 8e.6 | Implement `info` command | Low |
| 8e.7 | Implement `run` command (QEMU wrapper) | Medium |
| 8e.8 | Project file parser | Medium |
| 8e.9 | Dependency resolution | High |

---

## ?? Technical Dependencies

### External Libraries Needed

| Library | Purpose | License |
|---------|---------|---------|
| musl | C standard library | MIT |
| miniz | ZIP compression (for gxapp) | MIT |
| cJSON | JSON parsing | MIT |
| TweetNaCl | Ed25519 signatures | Public Domain |

### Cross-Compilation Toolchains

For each target architecture, we need:
- GCC or Clang cross-compiler
- binutils (as, ld, objcopy)
- Architecture-specific headers

**Recommended Approach:**
1. Use LLVM/Clang as primary (single toolchain, multiple targets)
2. Fall back to GCC cross-compilers where Clang lacks support

---

## ?? Implementation Roadmap

### Iteration 1: Foundation (Current)
- [x] Document architecture decisions
- [ ] Create cpu_arch.h with enum
- [ ] Create arch_detector.h header
- [ ] Implement ArchitectureDetector for x86/amd64

### Iteration 2: Basic Container
- [ ] Define metadata.json schema
- [ ] Implement minimal ZIP reader
- [ ] Implement GXAppContainer (read-only)
- [ ] Test with hand-crafted gxapp

### Iteration 3: ELF Loading
- [ ] Port ELF loader from bootloader to userspace context
- [ ] Implement process creation with ELF entry
- [ ] Test loading simple ELF binaries

### Iteration 4: Integration
- [ ] Connect GXAppContainer to ELF loader
- [ ] Implement UniversalAppLoader
- [ ] End-to-end test: gxapp ? execution

### Iteration 5: SDK Bootstrap
- [ ] Port musl for amd64
- [ ] Create minimal crt0.o
- [ ] Create libguidexos.a
- [ ] Compile "Hello World" for guideXOS

### Iteration 6: gxbuild Tool
- [ ] Implement basic gxbuild
- [ ] Single-arch build working
- [ ] Multi-arch build working
- [ ] Package creation working

### Iteration 7+: Expansion
- [ ] Add more architectures (riscv64, etc.)
- [ ] Package manager integration
- [ ] Repository system
- [ ] Signature verification

---

## ? Open Questions (For Future Discussion)

1. **Dynamic Linking**: Should we support shared libraries (.so), or static-only?
   - Recommendation: Static-only initially, simpler universal binaries

2. **GUI Applications**: How do GUI apps work with this system?
   - Recommendation: Same system, just link against libguidexos_gui.a

3. **Kernel Modules**: Should kernel modules use .gxapp format?
   - Recommendation: Separate format (.gxmod) with kernel-specific metadata

4. **Auto-Update**: Should gxapp packages support delta updates?
   - Recommendation: Future feature, not MVP

5. **Sandboxing**: Permission model for applications?
   - Recommendation: Document in permissions field, enforce later

---

## ?? References

- [musl libc](https://musl.libc.org/)
- [ELF Specification](https://refspecs.linuxfoundation.org/elf/elf.pdf)
- [Apple Universal Binary](https://developer.apple.com/documentation/apple-silicon/building-a-universal-macos-binary)
- [FatELF Proposal](https://icculus.org/fatelf/)
- [Ed25519 Signatures](https://ed25519.cr.yp.to/)
