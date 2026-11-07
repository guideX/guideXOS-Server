#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if (( $# != 1 )); then
    echo "Usage: build.sh OUTPUT_DIR" >&2
    exit 2
fi

OUTPUT_DIR="$1"
if [[ -e "$OUTPUT_DIR" && ( ! -d "$OUTPUT_DIR" || -n "$(find "$OUTPUT_DIR" -mindepth 1 -print -quit)" ) ]]; then
    echo "[BUILD_BLOCKED] Output path must be absent or empty: $OUTPUT_DIR" >&2
    exit 2
fi
mkdir -p "$OUTPUT_DIR/bin"

TOOL_PREFIX="${IA64_TOOLCHAIN_PREFIX:-/home/guidex/src/ia64-2-toolchain-shim/ia64-linux}"
AS="${TOOL_PREFIX}-as"
LD="${TOOL_PREFIX}-ld"
for tool in "$AS" "$LD"; do
    if [[ ! -x "$tool" ]]; then
        echo "[TOOLCHAIN_BLOCKED] Missing target tool: $tool" >&2
        exit 2
    fi
done

OBJ="$OUTPUT_DIR/bin/ski-system-smoke.o"
ELF="$OUTPUT_DIR/bin/ski-system-smoke.elf"
"$AS" -mlp64 -mle -mconstant-gp -xexplicit -o "$OBJ" "$SCRIPT_DIR/smoke.S"
"$LD" -static -T "$SCRIPT_DIR/linker.ld" -o "$ELF" "$OBJ"

python3 - "$ELF" "$OUTPUT_DIR/ia64-ski-smoke-manifest.json" <<'PY'
import hashlib
import json
from pathlib import Path
import sys

elf = Path(sys.argv[1])
manifest = {
    "schema": "guidexos-ia64-ski-smoke-v1",
    "profile": "system-mode-smoke",
    "elf": "bin/ski-system-smoke.elf",
    "sha256": hashlib.sha256(elf.read_bytes()).hexdigest(),
    "guest_execution_authorized": "smoke-only",
    "privileged_instruction_test": "rsm psr.i; mov r8=psr",
    "console_marker": "SKI_SYSTEM_MODE_SMOKE_PASS",
    "exit_ssc": 66,
}
Path(sys.argv[2]).write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
print(f"[BUILD] Ski smoke ELF SHA-256: {manifest['sha256']}")
PY
