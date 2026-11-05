#!/usr/bin/env bash
# Reproduce and record the IA-64 GCC reload ICE and bounded FP allocation cases.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$REPO_ROOT" && pwd -P)"
KERNEL_DIR="$REPO_ROOT/kernel"
REPRO_SOURCE="$REPO_ROOT/tests/ia64_x509_modulo_repro.c"
X509_SOURCE="$REPO_ROOT/third_party/mbedtls/library/x509.c"
BIGNUM_SOURCE="$REPO_ROOT/third_party/mbedtls/tf-psa-crypto/drivers/builtin/src/bignum_core.c"

CC="${IA64_GCC:-}"
if [[ -z "$CC" ]]; then
    if command -v ia64-linux-gnu-gcc >/dev/null 2>&1; then
        CC=ia64-linux-gnu-gcc
    elif command -v ia64-linux-gcc >/dev/null 2>&1; then
        CC=ia64-linux-gcc
    else
        echo '[TOOLCHAIN_BLOCKED] Set IA64_GCC or provide ia64-linux-gcc.' >&2
        exit 2
    fi
fi

TARGET="$("$CC" -dumpmachine 2>/dev/null || true)"
VERSION="$("$CC" -dumpfullversion 2>/dev/null || true)"
if [[ "$TARGET" != ia64* || "$VERSION" != 13.5.0 ]]; then
    echo "[TOOLCHAIN_BLOCKED] Expected GCC 13.5.0 for IA-64; got $CC ($VERSION, $TARGET)." >&2
    exit 2
fi

CC_DIR="$(cd "$(dirname "$(command -v "$CC" 2>/dev/null || printf '%s' "$CC")")" && pwd)"
CC_BASE="$(basename "$CC")"
PREFIX="${CC_BASE%-gcc}"
OBJDUMP="${IA64_OBJDUMP:-$CC_DIR/$PREFIX-objdump}"
NM="${IA64_NM:-$CC_DIR/$PREFIX-nm}"
if [[ ! -x "$OBJDUMP" ]]; then OBJDUMP="${IA64_OBJDUMP:-ia64-linux-objdump}"; fi
if [[ ! -x "$NM" ]]; then NM="${IA64_NM:-ia64-linux-nm}"; fi
for tool in "$OBJDUMP" "$NM"; do
    if ! command -v "$tool" >/dev/null 2>&1 && [[ ! -x "$tool" ]]; then
        echo "[TOOLCHAIN_BLOCKED] Required target inspection tool is missing: $tool" >&2
        exit 2
    fi
done

OUT_DIR="${1:-}"
if [[ -z "$OUT_DIR" ]]; then
    OUT_DIR="$(mktemp -d "${TMPDIR:-/tmp}/guidexos-ia64-fp-repro.XXXXXX")"
else
    case "$OUT_DIR" in
        /*) ;;
        *) OUT_DIR="$REPO_ROOT/$OUT_DIR" ;;
    esac
    if [[ -e "$OUT_DIR" && ! -d "$OUT_DIR" ]]; then
        echo "[ERROR] Output path exists and is not a directory: $OUT_DIR" >&2
        exit 2
    fi
    if [[ -d "$OUT_DIR" ]]; then
        shopt -s nullglob dotglob
        existing_items=("$OUT_DIR"/*)
        shopt -u nullglob dotglob
        if (( ${#existing_items[@]} > 0 )); then
            echo "[ERROR] Output directory must be empty: $OUT_DIR" >&2
            exit 2
        fi
    else
        mkdir -p "$OUT_DIR"
    fi
fi

BASE_FLAGS=(
    -std=c11 -ffreestanding -O2 -Wall -Wextra -nostdlib -fno-builtin
    -DGXOS_BARE_METAL -I. -I.. -Icore/include -Iarch/ia64/include
    -I"$REPO_ROOT" -I"$REPO_ROOT/third_party/mbedtls/include"
    -I"$REPO_ROOT/third_party/mbedtls/library"
    -I"$REPO_ROOT/third_party/mbedtls/tf-psa-crypto"
    -I"$REPO_ROOT/third_party/mbedtls/tf-psa-crypto/core"
    -I"$REPO_ROOT/third_party/mbedtls/tf-psa-crypto/include"
    -I"$REPO_ROOT/third_party/mbedtls/tf-psa-crypto/drivers/builtin/include"
    -I"$REPO_ROOT/third_party/mbedtls/tf-psa-crypto/drivers/builtin/src"
    -I"$REPO_ROOT/third_party/mbedtls/tf-psa-crypto/drivers/p256-m/p256-m/include"
    -I"$REPO_ROOT/third_party/mbedtls/tf-psa-crypto/drivers/p256-m/p256-m/include/p256-m"
    -I"$REPO_ROOT/third_party/mbedtls/tf-psa-crypto/drivers/p256-m/p256-m_driver_interface"
    -I"$REPO_ROOT/third_party/mbedtls/tf-psa-crypto/dispatch"
    -I"$REPO_ROOT/third_party/mbedtls/tf-psa-crypto/extras"
    -I"$REPO_ROOT/third_party/mbedtls/tf-psa-crypto/platform"
    -I"$REPO_ROOT/third_party/mbedtls/tf-psa-crypto/utilities"
    -D_WIN32_WINNT=0x0500 -DEFIX64 -DMBEDTLS_TEST_SW_INET_PTON
    '-DMBEDTLS_CONFIG_FILE="guidexos/mbedtls_config.h"'
    '-DTF_PSA_CRYPTO_CONFIG_FILE="guidexos/crypto_config.h"'
    -Wa,-xexplicit
)
if [[ -f "$KERNEL_DIR/mbedtls-guidexos-defines.h" ]]; then
    BASE_FLAGS+=( -imacros "$KERNEL_DIR/mbedtls-guidexos-defines.h" )
fi

REPORT="$OUT_DIR/report.md"
printf '# IA-64 GCC 13.5.0 FP code-generation regression\n\n' > "$REPORT"
printf '%s\n\n' "- Compiler: $CC ($VERSION, $TARGET)" "- Binutils: $($OBJDUMP --version | head -n 1)" "- Evidence directory: $OUT_DIR" >> "$REPORT"
printf '%s\n\n' 'All compilations use freestanding flags and `-Wa,-xexplicit`. The standalone case models the integer `% 400` and leap-year expression in pinned Mbed TLS `x509.c`.' >> "$REPORT"

declare -a CASES=(
    'runtime-reserved|-mfixed-range=f2-f127'
    'runtime-reserved-no-inline|-mfixed-range=f2-f127 -mno-inline-int-divide'
    'compile-only-scratch|-mfixed-range=f2-f5,f8-f127'
    'compile-only-scratch-no-inline|-mfixed-range=f2-f5,f8-f127 -mno-inline-int-divide'
    'compile-only-abi-scratch|-mfixed-range=f2-f5,f16-f31'
    'compile-only-abi-scratch-no-inline|-mfixed-range=f2-f5,f16-f31 -mno-inline-int-divide'
    'unrestricted|-O2'
)
failures=0

for case_item in "${CASES[@]}"; do
    name="${case_item%%|*}"
    flag_text="${case_item#*|}"
    read -r -a case_flags <<< "$flag_text"
    if [[ "$name" == unrestricted ]]; then
        case_flags=()
    fi
    expect_failure=0
    if [[ "$name" == runtime-reserved* ]]; then
        expect_failure=1
    fi
    printf '\n## %s\n\n' "$name" >> "$REPORT"
    printf '| Source | Result | FP registers | External symbols |\n|---|---|---|---|\n' >> "$REPORT"

    for source_kind in repro x509 bignum; do
        case "$source_kind" in
            repro) source="$REPRO_SOURCE" ;;
            x509) source="$X509_SOURCE" ;;
            bignum) source="$BIGNUM_SOURCE" ;;
        esac
        source_flags=(-x c)
        candidate_expects_failure="$expect_failure"
        if [[ "$name" == compile-only-scratch* && "$source_kind" == bignum ]]; then
            candidate_expects_failure=1
        fi
        stem="$OUT_DIR/$name-$source_kind"
        set +e
        (cd "$KERNEL_DIR" && "$CC" "${BASE_FLAGS[@]}" "${source_flags[@]}" "${case_flags[@]}" \
            -save-temps=obj -c "$source" -o "$stem.o") >"$stem.stdout" 2>"$stem.stderr"
        status=$?
        set -e

        if (( status == 0 )); then
            "$OBJDUMP" -dr "$stem.o" > "$stem.disassembly.txt"
            "$NM" -u "$stem.o" > "$stem.undefined.txt" || true
            fp_regs="$(grep -Eo '\bf[0-9]{1,3}\b' "$stem.s" | sort -Vu | paste -sd ',' - || true)"
            external="$(awk 'NF {printf "%s%s", sep, $NF; sep=","} END {if (!NR) print "none"}' "$stem.undefined.txt")"
            [[ -n "$fp_regs" ]] || fp_regs=none
            if (( candidate_expects_failure )); then
                echo "[REGRESSION_FAIL] $name/$source_kind unexpectedly compiled." >&2
                failures=$((failures + 1))
            fi
            if [[ "$source_kind" == repro && "$name" == compile-only-scratch* ]]; then
                unexpected="$(printf '%s\n' "$fp_regs" | tr ',' '\n' | grep -Ev '^(f6|f7)$' | tr '\n' ' ' || true)"
                if [[ -n "$unexpected" || "$external" != none ]]; then
                    echo "[REGRESSION_FAIL] $name unexpectedly used FP registers/helpers: $fp_regs / $external" >&2
                    failures=$((failures + 1))
                fi
                if ! grep -q 'xmpy.hu' "$stem.s" || ! grep -q 'setf.sig' "$stem.s" || ! grep -q 'getf.sig' "$stem.s"; then
                    echo "[REGRESSION_FAIL] $name did not expose the expected integer high-multiply FP sequence." >&2
                    failures=$((failures + 1))
                fi
            fi
            if [[ "$name" == compile-only-abi-scratch* ]]; then
                preserved_regs="$(printf '%s\n' "$fp_regs" | tr ',' '\n' | grep -E '^f([2-5]|1[6-9]|2[0-9]|3[01])$' | tr '\n' ' ' || true)"
                if [[ -n "$preserved_regs" ]]; then
                    echo "[REGRESSION_FAIL] $name used ABI-preserved FP registers: $preserved_regs" >&2
                    failures=$((failures + 1))
                fi
            fi
            printf '| %s | PASS (exit 0) | %s | %s |\n' "$source_kind" "$fp_regs" "$external" >> "$REPORT"
        else
            printf '| %s | FAIL (exit %s) | n/a | n/a |\n' "$source_kind" "$status" >> "$REPORT"
            if (( candidate_expects_failure )); then
                if [[ "$name" == compile-only-scratch* && "$source_kind" == bignum ]] &&
                   ! grep -q "unable to find a register to spill in class 'FR_REGS'" "$stem.stderr"; then
                    echo "[COMPILE_FAIL] $name/$source_kind failed for an unexpected reason; see $stem.stderr" >&2
                    failures=$((failures + 1))
                elif [[ "$name" == runtime-reserved* ]] &&
                     ! grep -Eq 'internal compiler error: in find_reloads|unable to find a register to spill' "$stem.stderr"; then
                    echo "[COMPILE_FAIL] $name/$source_kind failed for an unexpected reason; see $stem.stderr" >&2
                    failures=$((failures + 1))
                fi
                printf '\n`%s` failure reproduced; diagnostic saved in `%s.stderr`.\n' \
                    "$source_kind" "$stem" >> "$REPORT"
            else
                echo "[COMPILE_FAIL] $name/$source_kind failed for a reason other than the expected reload ICE; see $stem.stderr" >&2
                failures=$((failures + 1))
            fi
        fi
    done

    printf '\nCompiler outputs are saved as `%s-*.s`, `%s-*.o`, and disassembly/diagnostic files.\n' \
        "$OUT_DIR/$name" "$OUT_DIR/$name" >> "$REPORT"
done

cat >> "$REPORT" <<'EOF'

## Interpretation

- The runtime-reserved candidates keep the existing no-FP compiler policy. For `% 400`, GCC lowers the operation to `umuldi3_highpart`; it still needs an FP-based high multiply and reports a reload ICE. `-mno-inline-int-divide` does not change this constant-modulo expansion.
- `compile-only-scratch` leaves only `f6-f7` available. It compiles `x509.c`, but the wider Mbed TLS bignum multiply reproduces a separate `FR_REGS` allocation failure.
- `compile-only-abi-scratch` reserves ABI-preserved `f2-f5` and `f16-f31`, leaving caller-saved `f6-f15` and `f32-f127`. The `% 400` sequence uses `setf.sig`, `xmpy.hu`, and `getf.sig`; these are FP-register instructions for integer arithmetic, with no external helper in the standalone object. The bignum source is also compiled under this profile to qualify the larger integer multiply allocation.
- This narrow allocation respects procedure-call scratch/preserved allocation, but asynchronous interrupts and preemptive switching can occur while scratch or other FP state is live. No interrupt, exception, voluntary context-switch, or scheduler FP state path is implemented here. Compile-only ELF output is blocked from execution.
- The unrestricted candidate is a code-generation comparison only; it is never a build or run profile.
EOF

if (( failures > 0 )); then
    echo "[FP_REGRESSION] FAIL count=$failures report=$REPORT" >&2
    exit 1
fi
echo "[FP_REGRESSION] PASS report=$REPORT"
