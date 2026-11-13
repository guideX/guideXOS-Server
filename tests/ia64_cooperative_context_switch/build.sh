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
OUT_DIR="${IA64_COOP_CONTEXT_PROBE_OUT_DIR:-}"

if [[ -z "$OUT_DIR" ]]; then
    echo 'Set IA64_COOP_CONTEXT_PROBE_OUT_DIR to a fresh isolated directory.' >&2
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
    -fno-stack-protector -fno-builtin -nostdlib -nostdinc++ -DGXOS_BARE_METAL
    -I"$REPO_ROOT/kernel" -I"$REPO_ROOT/kernel/core/include"
    -I"$REPO_ROOT/kernel/arch/ia64/include"
    -mlittle-endian -mconstant-gp -mfixed-range=f2-f5,f16-f31 -Wa,-xexplicit
)
COMMON_AS=(-mlp64 -mle -mconstant-gp -xexplicit)

"$AS" "${COMMON_AS[@]}" "$SCRIPT_DIR/entry.s" -o "$OUT_DIR/obj/probe_entry.o"
"$AS" "${COMMON_AS[@]}" "$SCRIPT_DIR/probe_sequence.s" -o "$OUT_DIR/obj/probe_sequence.o"
"$AS" "${COMMON_AS[@]}" "$SCRIPT_DIR/context_switch.s" -o "$OUT_DIR/obj/context_switch.o"
"$AS" "${COMMON_AS[@]}" "$REPO_ROOT/kernel/arch/ia64/ski_ssc.s" \
    -o "$OUT_DIR/obj/ski_ssc.o"
"$CXX" "${COMMON_CXX[@]}" -c "$REPO_ROOT/kernel/arch/ia64/ski_console.cpp" \
    -o "$OUT_DIR/obj/ski_console.o"
"$CXX" "${COMMON_CXX[@]}" -c "$SCRIPT_DIR/probe_main.cpp" \
    -o "$OUT_DIR/obj/probe_main.o"

"$LD" -m elf64_ia64 -T "$SCRIPT_DIR/linker.ld" -o "$OUT_DIR/ia64-cooperative-context-probe.elf" \
    "$OUT_DIR/obj/probe_entry.o" "$OUT_DIR/obj/probe_sequence.o" \
    "$OUT_DIR/obj/context_switch.o" "$OUT_DIR/obj/ski_ssc.o" \
    "$OUT_DIR/obj/ski_console.o" "$OUT_DIR/obj/probe_main.o" "$LIBGCC"

"$OBJDUMP" -dr "$OUT_DIR/ia64-cooperative-context-probe.elf" > \
    "$OUT_DIR/ia64-cooperative-context-probe.disassembly.txt"
PYTHONDONTWRITEBYTECODE=1 python3 - "$OUT_DIR/ia64-cooperative-context-probe.elf" \
    "$OUT_DIR/ia64-cooperative-context-probe.manifest.json" <<'PY'
import hashlib
import json
from pathlib import Path
import sys

elf = Path(sys.argv[1]).resolve()
manifest = Path(sys.argv[2])
digest = hashlib.sha256(elf.read_bytes()).hexdigest()
manifest.write_text(json.dumps({
    "schema": "guidexos-ia64-cooperative-context-probe-v1",
    "profile": "cooperative-context-switch-probe",
    "elf": elf.name,
    "sha256": digest,
    "guest_execution_authorized": "probe-only",
    "execution_count_authorized": 1,
    "system_mode": "-forcesystem",
    "cpu_count": 1,
    "ivt_bytes": 0x8000,
    "expected_faults": 0,
    "expected_events": [
        "A_TO_B", "B_FIRST_ENTRY", "B_TO_A_FIRST", "A_RESUMED_FIRST",
        "A_TO_B_SECOND", "B_RESUMED", "B_TO_A_SECOND", "A_RESUMED_FINAL"
    ],
    "intentional_exit_ssc": 66,
    "scheduler_enabled": False,
    "external_interrupts_enabled": False,
}, indent=2) + "\n", encoding="utf-8")
print(f"IA64_COOP_CONTEXT_PROBE_SHA256={digest}")
print(f"IA64_COOP_CONTEXT_PROBE_ELF={elf}")
PY
