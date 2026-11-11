#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
TOOLCHAIN_PREFIX="${IA64_TOOLCHAIN_PREFIX:-/home/guidex/opt/ia64-gcc-13.5.0-cxx/bin/ia64-linux}"
AS="${IA64_AS:-/home/guidex/src/ia64-2-toolchain-shim/ia64-linux-as}"
CXX="${IA64_CXX:-${TOOLCHAIN_PREFIX}-g++}"
LD="${IA64_LD:-/home/guidex/src/ia64-2-toolchain-shim/ia64-linux-ld}"
OBJDUMP="${IA64_OBJDUMP:-/home/guidex/src/ia64-2-toolchain-shim/ia64-linux-objdump}"
LIBGCC="${IA64_RUNTIME_LIBGCC:-/home/guidex/src/gcc-13.5.0-build/ia64-linux-gnu/libgcc/libgcc.a}"
OUT_DIR="${IA64_UNALIGNED_REFERENCE_PROBE_OUT_DIR:-}"

if [[ -z "$OUT_DIR" ]]; then
    echo 'Set IA64_UNALIGNED_REFERENCE_PROBE_OUT_DIR to a fresh isolated directory.' >&2
    exit 2
fi
if [[ -e "$OUT_DIR" && ! -d "$OUT_DIR" ]]; then
    echo "Probe output path exists and is not a directory: $OUT_DIR" >&2
    exit 2
fi
if [[ -d "$OUT_DIR" ]]; then
    shopt -s nullglob dotglob
    existing_items=("$OUT_DIR"/*)
    shopt -u nullglob dotglob
    if (( ${#existing_items[@]} > 0 )); then
        echo "Probe output directory must be empty: $OUT_DIR" >&2
        exit 2
    fi
else
    mkdir -p "$OUT_DIR"
fi

for tool in "$AS" "$CXX" "$LD" "$OBJDUMP"; do
    if [[ ! -x "$tool" ]]; then
        echo "Required IA-64 tool is unavailable: $tool" >&2
        exit 2
    fi
done
if [[ ! -f "$LIBGCC" ]]; then
    echo "Target-compatible IA-64 libgcc archive is unavailable: $LIBGCC" >&2
    exit 2
fi

mkdir -p "$OUT_DIR/obj"
COMMON_CXX=(
    -std=c++14 -ffreestanding -O2 -Wall -Wextra -fno-exceptions -fno-rtti
    -nostdlib -nostdinc++ -fno-builtin -DGXOS_BARE_METAL
    -I"$REPO_ROOT/kernel" -I"$REPO_ROOT/kernel/core/include"
    -I"$REPO_ROOT/kernel/arch/ia64/include"
    -mlittle-endian -mconstant-gp -mfixed-range=f2-f5,f16-f31 -Wa,-xexplicit
)
COMMON_AS=(-mlp64 -mle -mconstant-gp -xexplicit)

"$AS" "${COMMON_AS[@]}" "$SCRIPT_DIR/entry.s" -o "$OUT_DIR/obj/entry.o"
"$AS" "${COMMON_AS[@]}" "$REPO_ROOT/kernel/arch/ia64/unaligned_reference_entry.s" \
    -o "$OUT_DIR/obj/unaligned_reference_entry.o"
"$AS" "${COMMON_AS[@]}" "$REPO_ROOT/kernel/arch/ia64/ski_ssc.s" \
    -o "$OUT_DIR/obj/ski_ssc.o"
"$CXX" "${COMMON_CXX[@]}" -c "$REPO_ROOT/kernel/arch/ia64/unaligned_reference.cpp" \
    -o "$OUT_DIR/obj/unaligned_reference.o"
"$CXX" "${COMMON_CXX[@]}" -c "$REPO_ROOT/kernel/arch/ia64/ski_console.cpp" \
    -o "$OUT_DIR/obj/ski_console.o"
"$CXX" "${COMMON_CXX[@]}" -c "$SCRIPT_DIR/probe_main.cpp" \
    -o "$OUT_DIR/obj/probe_main.o"

"$LD" -m elf64_ia64 -T "$SCRIPT_DIR/linker.ld" -o "$OUT_DIR/unaligned-reference-probe.elf" \
    "$OUT_DIR/obj/entry.o" "$OUT_DIR/obj/unaligned_reference_entry.o" \
    "$OUT_DIR/obj/ski_ssc.o" "$OUT_DIR/obj/unaligned_reference.o" \
    "$OUT_DIR/obj/ski_console.o" "$OUT_DIR/obj/probe_main.o" "$LIBGCC"

"$OBJDUMP" -dr "$OUT_DIR/unaligned-reference-probe.elf" > \
    "$OUT_DIR/unaligned-reference-probe.disassembly.txt"
python3 - "$OUT_DIR/unaligned-reference-probe.elf" "$OUT_DIR/unaligned-reference-probe.manifest.json" <<'PY'
import hashlib
import json
from pathlib import Path
import sys

elf = Path(sys.argv[1]).resolve()
manifest = Path(sys.argv[2])
digest = hashlib.sha256(elf.read_bytes()).hexdigest()
manifest.write_text(json.dumps({
    "schema": "guidexos-ia64-unaligned-reference-probe-v1",
    "profile": "unaligned-reference-probe",
    "elf": elf.name,
    "sha256": digest,
    "guest_execution_authorized": "probe-only",
    "system_mode": "-forcesystem",
    "cpu_count": 1,
    "ivt_vector_offset": 0x5a00,
    "fault_subtype": "unaligned-data-reference",
    "faulting_instruction": "ld8 r15=[r14]",
    "access_width": 8,
    "buffer_address_offset": 1,
    "psr_ac_enabled_for_faulting_access": True,
    "expected_faults": 1,
    "expected_continuations": 1,
    "post_rfi_capture_required": True,
    "all_other_vectors": "fail-stop",
}, indent=2) + "\n", encoding="utf-8")
print(f"UNALIGNED_REFERENCE_PROBE_SHA256={digest}")
print(f"UNALIGNED_REFERENCE_PROBE_ELF={elf}")
PY
