#!/usr/bin/env bash
# Build guideXOS Server for IA-64 / Itanium without touching shared outputs.
# Run from any directory: scripts/build-ia64.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

printf '%s\n' '============================================' '  guideXOS Itanium (IA-64) Kernel Build' '============================================'
printf '\n[INFO] Repository: %s\n' "$REPO_ROOT"

# A supplied output directory must be new or empty; never clean or reuse prior
# artifacts. Relative paths are resolved from the repository root.
REQUESTED_BUILD_DIR="${IA64_BUILD_DIR:-}"
if [[ -n "$REQUESTED_BUILD_DIR" ]]; then
    case "$REQUESTED_BUILD_DIR" in
        /*) BUILD_DIR="$REQUESTED_BUILD_DIR" ;;
        *)  BUILD_DIR="$REPO_ROOT/$REQUESTED_BUILD_DIR" ;;
    esac
    if [[ -e "$BUILD_DIR" && ! -d "$BUILD_DIR" ]]; then
        echo "[ERROR] IA64_BUILD_DIR exists and is not a directory: $BUILD_DIR" >&2
        exit 2
    fi
    if [[ -d "$BUILD_DIR" ]]; then
        shopt -s nullglob dotglob
        existing_items=("$BUILD_DIR"/*)
        shopt -u nullglob dotglob
        if (( ${#existing_items[@]} > 0 )); then
            echo "[ERROR] IA64_BUILD_DIR must be empty to protect existing artifacts: $BUILD_DIR" >&2
            exit 2
        fi
    fi
fi

if [[ -n "${IA64_TOOLCHAIN_PREFIX:-}" ]]; then
    CROSS="$IA64_TOOLCHAIN_PREFIX"
elif command -v ia64-linux-gnu-g++ >/dev/null 2>&1; then
    CROSS="ia64-linux-gnu"
elif command -v ia64-elf-g++ >/dev/null 2>&1; then
    CROSS="ia64-elf"
else
    echo '[TOOLCHAIN_BLOCKED] No IA-64 C++ cross-compiler found.' >&2
    echo 'Install a GNU IA-64 toolchain with g++, as, ld, and readelf; see docs/IA64_SUPPORT.md.' >&2
    echo 'Expected compiler names: ia64-linux-gnu-g++ or ia64-elf-g++.' >&2
    exit 2
fi

CXX="${CROSS}-g++"
AS="${CROSS}-as"
LD="${CROSS}-ld"
for tool in "$CXX" "$AS" "$LD"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "[TOOLCHAIN_BLOCKED] Required target tool is missing: $tool" >&2
        exit 2
    fi
done

TARGET_TRIPLE="$("$CXX" -dumpmachine 2>/dev/null || true)"
case "$TARGET_TRIPLE" in
    ia64|ia64-*) ;;
    *)
        echo "[TOOLCHAIN_BLOCKED] Compiler target is incompatible: '$TARGET_TRIPLE' (expected ia64-*)." >&2
        exit 2
        ;;
esac

AS_VERSION="$("$AS" --version 2>&1 || true)"
if ! grep -qi ia64 <<<"$AS_VERSION"; then
    echo '[TOOLCHAIN_BLOCKED] Assembler does not identify an IA-64 target.' >&2
    echo "$AS_VERSION" >&2
    exit 2
fi
LD_EMULATIONS="$("$LD" -V 2>&1 || true)"
if ! grep -qi ia64 <<<"$LD_EMULATIONS"; then
    echo '[TOOLCHAIN_BLOCKED] Linker does not advertise an IA-64 emulation.' >&2
    echo "$LD_EMULATIONS" >&2
    exit 2
fi

if ! command -v make >/dev/null 2>&1; then
    echo '[TOOLCHAIN_BLOCKED] GNU make is required to build the kernel.' >&2
    exit 2
fi
if command -v "${CROSS}-readelf" >/dev/null 2>&1; then
    READELF="${CROSS}-readelf"
elif command -v readelf >/dev/null 2>&1; then
    READELF=readelf
else
    echo '[TOOLCHAIN_BLOCKED] readelf is required to verify the target ELF.' >&2
    exit 2
fi

if [[ -z "$REQUESTED_BUILD_DIR" ]]; then
    BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/guidexos-ia64-1.XXXXXX")"
else
    mkdir -p "$BUILD_DIR"
fi

printf '[INFO] Toolchain prefix: %s-\n' "$CROSS"
printf '[INFO] Compiler target: %s\n' "$TARGET_TRIPLE"
printf '[INFO] Compiler: %s\n' "$("$CXX" --version | head -n 1)"
printf '[INFO] Compiler sysroot: %s\n' "$("$CXX" -print-sysroot 2>/dev/null || true)"
printf '[INFO] Assembler: %s\n' "$("$AS" --version | head -n 1)"
printf '[INFO] Linker: %s\n' "$("$LD" --version | head -n 1)"
printf '[INFO] Linker emulations:\n%s\n' "$LD_EMULATIONS"
printf '[INFO] Build directory: %s\n' "$BUILD_DIR"
printf '[INFO] IA-64 flags: -mlp64 -mlittle-endian -mconstant-gp -mfixed-range=f2-f127; assembler: -mlp64 -mle -mconstant-gp; linker script: -T arch/ia64/linker.ld\n'

JOBS="${IA64_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}"
echo '[BUILD] Compiling IA-64 kernel with kernel/Makefile...'
make -C "$REPO_ROOT/kernel" ARCH=ia64 BUILD_DIR="$BUILD_DIR" CXX="$CXX" AS="$AS" LD="$LD" -j"$JOBS"

KERNEL="$BUILD_DIR/bin/kernel.elf"
if [[ ! -f "$KERNEL" ]]; then
    echo "[ERROR] Build completed without expected ELF: $KERNEL" >&2
    exit 1
fi

ELF_HEADER="$("$READELF" -h "$KERNEL")"
printf '\n%s\n' "$ELF_HEADER"
if ! grep -qi 'Class:.*ELF64' <<<"$ELF_HEADER" ||
   ! grep -qi 'Data:.*little endian' <<<"$ELF_HEADER" ||
   ! grep -qi 'Machine:.*IA-64' <<<"$ELF_HEADER"; then
    echo '[ERROR] Output is not a little-endian ELF64 IA-64 image.' >&2
    exit 1
fi
ENTRY_POINT="$(awk '/Entry point address:/ { print $4; exit }' <<<"$ELF_HEADER")"
if [[ -z "$ENTRY_POINT" || "$ENTRY_POINT" == 0x0 || "$ENTRY_POINT" == 0x0000000000000000 ]]; then
    echo "[ERROR] ELF entry point is absent or zero: '$ENTRY_POINT'" >&2
    exit 1
fi
printf '[PASS] ELF is ELF64, little-endian, EM_IA_64; entry=%s\n' "$ENTRY_POINT"
printf '\n[INFO] Section, segment, and relocation inspection:\n'
"$READELF" -S -l -r "$KERNEL"
printf '\n[BUILD] Kernel ELF: %s\n' "$KERNEL"
