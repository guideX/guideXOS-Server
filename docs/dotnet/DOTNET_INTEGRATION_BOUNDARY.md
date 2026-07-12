# .NET Integration Boundary

This document records the architectural rules for optional .NET support in guideXOS Server. It is a boundary document only. It does not define the final ABI, the final managed SDK surface, or any porting plan.

## Core Rules

1. .NET is optional.
2. AMD64 is the initial target.
3. The normal Server build remains independent of .NET.
4. Managed applications use the existing Server App Model.
5. The Server kernel does not understand managed object layouts.
6. The Server kernel does not expose internal C++ classes directly to C#.
7. Interop uses a stable runtime-neutral C ABI or host-call table.
8. Runtime-specific code must remain isolated.
9. Native ELF remains the universal execution foundation.
10. Runtime absence is a normal supported condition.
11. No v1.0 release milestone depends on .NET.
12. .NET support may vary by CPU architecture.
13. No direct framebuffer or kernel-object access from managed applications.
14. Generic kernel improvements must be independently justified and tested without .NET.

## What This Means

The managed layer, if present, sits above the Server App Model and below managed application code. It may translate managed expectations into the host-neutral runtime boundary, but it must not become a second kernel ABI.

The current Server-side experimental Native ELF model already points in the right direction:

* `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\native_app_runtime.h` defines a runtime-neutral `guidexos-c-abi-v1` host-call table.
* `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\native_elf_executor.cpp` and `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\native_elf_launch_pipeline.cpp` keep execution gated by architecture, image format, and runtime state.
* `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\README.md:152-154` already states that Native ELF support in the hosted runtime is experimental and amd64-only.

The future managed adapter should fit into that existing shape:

* Managed C# application.
* Managed guideXOS SDK.
* .NET-specific runtime adapter.
* Stable guideXOS Server App Model ABI.
* guideXOS Server services.

## Boundary Principles

* Managed object references never cross into the kernel or the App Model ABI.
* Managed heap layout never becomes a Server contract.
* Raw pointers are allowed only inside the runtime adapter, not as the public application contract.
* Handles must remain opaque and runtime-neutral.
* Strings, arrays, and structs must cross only through explicit ABI-owned representations.
* Function pointers are acceptable only inside the runtime adapter and host-call table.
* Direct framebuffer, port I/O, and kernel-object access stay out of managed application code.
* The managed layer may call host services, but it does not own the kernel.

## Compatibility Policy

* The normal Server build must continue to work when .NET is absent.
* Adding .NET support must not add dependencies to the normal Server build.
* A managed runtime failure must degrade cleanly, not destabilize the base Server platform.
* Each CPU architecture must be justified independently.
* A feature that exists in the managed adapter must still make sense when .NET is disabled.

## Scope Controls

* Do not invent the final managed ABI during the audit pass.
* Do not move guideXOS C# runtime code into the Server repository as a baseline dependency.
* Do not modernize or upgrade the legacy runtime as part of the boundary definition.
* Do not couple managed application support to GUI, file system, or networking behavior unless a later experiment proves a clean host-call path.
* Do not let experimental runtime code leak into the default Server build graph.

## Reference Points

The following files capture the existing direction and should be treated as reference points, not as a transplant plan:

* `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\native_app_runtime.h`
* `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\native_app_runtime.cpp`
* `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\native_elf_executor.cpp`
* `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\native_elf_launch_pipeline.cpp`
* `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\native_elf_image_loader.cpp`
* `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\elf_validator.cpp`
* `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\app_launch_resolver.cpp`
* `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\README.md`

## Non-Goals

* No kernel changes for this boundary definition.
* No compositor changes for this boundary definition.
* No VFS changes for this boundary definition.
* No ELF loader changes for this boundary definition.
* No App Model dispatch changes for this boundary definition.
* No default image or inventory changes for this boundary definition.
* No managed object layout exposure to Server services.

The boundary is meant to keep the platform flexible: guideXOS Server stays the universal base, and .NET remains an optional, architecture-scoped layer above it.
