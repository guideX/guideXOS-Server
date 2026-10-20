# S22R4 validation record

## Executed

Ran a no-output syntax check of the existing Server test source:

    C:\mingw64\bin\g++.exe -std=c++17 -Wall -Wextra -O2 -idirafter . -Isdk/include -fsyntax-only tests/native_abi_layout_test.cpp

Result: exit 1 at tests/native_abi_layout_test.cpp:82. The assertion expects sizeof(gx_host_calls) == 248; the current SDK structure evaluates to 256 because get_document_activation_path was appended at offset 248. The compile check wrote no object or executable. No test or source was edited.

## Existing evidence reviewed, not rerun

- S22R3 recorded 18 passing assertions for scripts/test-release-build-integrity.ps1, including minimum-version rejection and exact package identity.
- DeveloperStudio tests/appmodel_activation_test.cpp contains hosted mock-filesystem checks for projectless file activation, document editing, write failure, saving, and repeated document lifecycles. This test source is not production v0.3 guest evidence.
- Server docs/SETTINGS_S22_V03_PACKAGE_QUALIFICATION.md records the Settings runtime suite at 1,426/1,426 after the default persistence repair. It also records that exact-package guest Apply/readback/reopen acceptance remains outstanding.

## Not run

No DeveloperStudio build or unit suite was started. No Server production build, release-integrity harness, ISO assembly, QEMU run, or guest interaction was started. No package preflight was modified or bypassed.
