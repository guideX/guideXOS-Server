# Stellar Navigator Native ELF sample

This sample is the first native guideXOS application slice for Stellar Navigator. It uses the existing bare-metal Native ELF package loader and the `guidexos-c-abi-v1` host-call table. It does not depend on the Windows Forms host or the managed .NET assemblies.

The application evaluates Earth (NAIF target 399) at ET/TDB `0` seconds past J2000. It reads the existing `ephemeris_de440s_chebyshev.sn4bin` asset, verifies its full SHA-256 against the managed provider's trusted digest, follows the Earth to Earth-Moon-barycenter center chain, and evaluates the Type-2 Chebyshev position in J2000 barycentric kilometres. The UI presents a fixed read-only state and a controlled data error when the asset cannot be read, authenticated, or evaluated.

## Build the guest package

From the repository root, run:

```powershell
powershell -ExecutionPolicy Bypass -File sdk/samples/stellarnavigator/build.ps1
```

The build uses the installed LLVM `clang++` and `ld.lld`, targets `x86_64-unknown-elf`, and writes:

- `Apps/StellarNavigator/app.json`
- `Apps/StellarNavigator/bin/amd64/stellarnavigator.elf`

The existing data asset remains at its original project path. When staging this package for a guest, preserve that path relative to the package root:
`Astronomy.Ephemeris/Data/ephemeris_de440s_chebyshev.sn4bin`.

The resulting program is a static AMD64 ELF `ET_EXEC` with the `gx_main` entry point. It uses bounded caller-owned storage and has no STL, C++ exceptions, RTTI, or dynamic allocation.

## Run focused portable tests

```powershell
powershell -ExecutionPolicy Bypass -File sdk/samples/stellarnavigator/tests/run-tests.ps1
```

The hosted test harness reads the same authenticated asset and checks the fixed-epoch result against the existing hosted SN4 output, invalid epochs, coverage, unsupported targets, short reads, asset corruption, deterministic repetition, and initialize/shutdown behavior. Test build output stays under the system temporary directory.

## Guest qualification

When the AMD64 proof kernel builds and the exclusive QEMU lane is clear, run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/run-stellar-navigator-sn10-qemu.ps1
```

The runner stages an isolated writable FAT ESP, boots it through OVMF and the guideXOS UEFI bootloader, and uses the test-only `GXOS_STELLAR_NAVIGATOR_SN10_GUEST_PROOF` hook to launch the package through Desktop/App Model dispatch. It records guest serial and QEMU logs under `%USERPROFILE%\.codex\artifacts\SN10_<date>\qemu\`. A system-wide QEMU census makes it stop without touching a pre-existing emulator. A QAC coordinator, when available, must own the lane instead.
