#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
if (( $# != 1 )); then
    echo "Usage: run.sh OUTPUT_DIR" >&2
    exit 2
fi
OUT_DIR="$1"
if [[ -e "$OUT_DIR" && ( ! -d "$OUT_DIR" || -n "$(find "$OUT_DIR" -mindepth 1 -print -quit)" ) ]]; then
    echo "[OUTPUT_BLOCKED] Output path must be absent or empty: $OUT_DIR" >&2
    exit 2
fi
mkdir -p "$OUT_DIR"
printf 'Q' > "$OUT_DIR/ssc-getchar.stdin"

TOOL_PREFIX="${IA64_TOOLCHAIN_PREFIX:-/home/guidex/src/ia64-2-toolchain-shim/ia64-linux}"
SKI_PREFIX="${SKI_PREFIX:-/home/guidex/opt/ski-dfc2902}"
CXX="${TOOL_PREFIX}-g++"
AS="${TOOL_PREFIX}-as"
LD="${TOOL_PREFIX}-ld"
OBJDUMP="${TOOL_PREFIX}-objdump"
SKI="$SKI_PREFIX/bin/bskinc"
for tool in "$CXX" "$AS" "$LD" "$OBJDUMP" "$SKI"; do
    if [[ ! -x "$tool" ]]; then
        echo "[TOOLCHAIN_BLOCKED] Required tool is unavailable: $tool" >&2
        exit 2
    fi
done

BASELINE_REF="${IA64_DIAGNOSTIC_BASELINE_REF:-dff405f55b5beb4bbb345e9eeb04fb290b76e9d1}"
git -C "$REPO_ROOT" cat-file -e "$BASELINE_REF^{commit}"
git -C "$REPO_ROOT" show \
    "$BASELINE_REF:kernel/arch/ia64/ski_console.cpp" > "$OUT_DIR/legacy_ski_console.cpp"

COMMON=(-std=c++14 -ffreestanding -O2 -Wall -Wextra -fno-exceptions -fno-rtti
        -nostdlib -nostdinc++ -fno-builtin -DGXOS_BARE_METAL
        -mlittle-endian -mconstant-gp -mfixed-range=f2-f5,f16-f31 -Wa,-xexplicit
        -I"$REPO_ROOT" -I"$REPO_ROOT/kernel/core/include"
        -I"$REPO_ROOT/kernel/arch/ia64/include"
        -I"$REPO_ROOT/kernel/arch/ia64")
mkdir -p "$OUT_DIR/obj/legacy" "$OUT_DIR/obj/fixed"

"$AS" -mlp64 -mle -mconstant-gp -xexplicit \
    -o "$OUT_DIR/obj/entry.o" "$SCRIPT_DIR/entry.s"
"$CXX" "${COMMON[@]}" -c "$SCRIPT_DIR/diagnostic.cpp" -o "$OUT_DIR/obj/diagnostic.o"
"$CXX" "${COMMON[@]}" -c "$REPO_ROOT/kernel/arch/ia64/arch.cpp" -o "$OUT_DIR/obj/arch.o"
"$CXX" "${COMMON[@]}" -c "$OUT_DIR/legacy_ski_console.cpp" -o "$OUT_DIR/obj/legacy/console.o"
"$CXX" "${COMMON[@]}" -c "$REPO_ROOT/kernel/arch/ia64/ski_console.cpp" -o "$OUT_DIR/obj/fixed/console.o"
"$AS" -mlp64 -mle -mconstant-gp -xexplicit \
    -o "$OUT_DIR/obj/fixed/ski_ssc.o" "$REPO_ROOT/kernel/arch/ia64/ski_ssc.s"

"$LD" -static -T "$SCRIPT_DIR/linker.ld" -o "$OUT_DIR/legacy-diagnostic.elf" \
    "$OUT_DIR/obj/entry.o" "$OUT_DIR/obj/diagnostic.o" "$OUT_DIR/obj/arch.o" \
    "$OUT_DIR/obj/legacy/console.o"
"$LD" -static -T "$SCRIPT_DIR/linker.ld" -o "$OUT_DIR/fixed-diagnostic.elf" \
    "$OUT_DIR/obj/entry.o" "$OUT_DIR/obj/diagnostic.o" "$OUT_DIR/obj/arch.o" \
    "$OUT_DIR/obj/fixed/console.o" "$OUT_DIR/obj/fixed/ski_ssc.o"

"$OBJDUMP" -dr -C "$OUT_DIR/legacy-diagnostic.elf" > "$OUT_DIR/legacy-disassembly.txt"
"$OBJDUMP" -dr -C "$OUT_DIR/fixed-diagnostic.elf" > "$OUT_DIR/fixed-disassembly.txt"

for variant in legacy fixed; do
    elf="$OUT_DIR/${variant}-diagnostic.elf"
    capture="$OUT_DIR/$variant"
    mkdir -p "$capture"
    set +e
    prlimit --as=2147483648 --cpu=30 -- \
        timeout --signal=TERM --kill-after=2s 10s \
        "$SKI" -stats -icnt "$capture/instruction-counts.txt" -forcesystem "$elf" \
        < "$OUT_DIR/ssc-getchar.stdin" \
        > "$capture/stdout.log" 2> "$capture/stderr.log"
    status=$?
    set -e
    printf '%s\n' "$status" > "$capture/exit-status.txt"
    if (( status != 0 )) && [[ "$variant" == fixed ]]; then
        echo "[SIMULATOR_FAIL] $variant diagnostic exited $status" >&2
        exit 1
    fi
    if (( status != 0 )); then
        echo "[LEGACY_EXPECTED_FAILURE] legacy diagnostic exited $status after producing comparison output"
    fi
done

python3 - "$OUT_DIR" "$BASELINE_REF" <<'PY'
from pathlib import Path
import hashlib
import json
import sys

out = Path(sys.argv[1])
baseline = sys.argv[2]
expected = (
    "HEX[0]=0x0\r\n"
    "HEX[1]=0x77\r\n"
    "HEX[2]=0x100000\r\n"
    "HEX[3]=0x4C6FE00\r\n"
    "HEX[4]=0x4C7FE00\r\n"
    "HEX[5]=0x123456789ABCDEF0\r\n"
    "HEX[6]=0xFFFFFFFFFFFFFFFF\r\n"
    "REPEAT=0x77|0x123456789ABCDEF0|0x77\r\n"
    "LIVE-HELD:!|LIVE-RETURN=0x123456789ABCDEF0\r\n"
    "FORMAT-END\r\n"
).encode("ascii")
legacy_expected = (
    "HEX[0]=0x77\r\n"
    "HEX[1]=0x77\r\n"
    "HEX[2]=0x77\r\n"
    "HEX[3]=0x77\r\n"
    "HEX[4]=0x77\r\n"
    "HEX[5]=0x77\r\n"
    "HEX[6]=0x77\r\n"
    "REPEAT=0x77|0x77|0x77\r\n"
    "LIVE-HELD:!|LIVE-RETURN=0x77\r\n"
    "FORMAT-END\r\n"
).encode("ascii")

def section(path: Path, start: bytes, end: bytes) -> bytes:
    data = path.read_bytes()
    a = data.index(start) + len(start)
    b = data.index(end, a) + len(end)
    return data[a:b]

old = section(out / "legacy/stdout.log", b"FORMAT-BEGIN\r\n", b"FORMAT-END\r\n")
new = section(out / "fixed/stdout.log", b"FORMAT-BEGIN\r\n", b"FORMAT-END\r\n")
if old != legacy_expected:
    raise SystemExit(
        "[REGRESSION_FAIL] Legacy inline wrapper output changed from the known r32-clobber failure:\n"
        f"expected={legacy_expected!r}\nactual={old!r}"
    )
if new != expected:
    raise SystemExit(
        "[REGRESSION_FAIL] Fixed ABI wrapper output differs from expected bytes:\n"
        f"expected={expected!r}\nactual={new!r}"
    )

fixed_output = (out / "fixed/stdout.log").read_bytes()
legacy_output = (out / "legacy/stdout.log").read_bytes()
if b"REGISTER_PROBE=PASS\r\n" not in fixed_output or b"REGISTER_PROBE=PASS\r\n" not in legacy_output:
    raise SystemExit("[REGISTER_PROBE_FAIL] known-value register checks failed")
if b"FPSR_READBACK=PASS\r\n" not in fixed_output or b"FPSR_READBACK=PASS\r\n" not in legacy_output:
    raise SystemExit("[FPSR_READBACK_FAIL] known AR.FPSR value did not read back in both probes")
if b"SSC_GETCHAR_API=PASS\r\n" not in fixed_output or b"SSC_GETCHAR_API=PASS\r\n" not in legacy_output:
    raise SystemExit("[SSC_RETURN_FAIL] GETCHAR byte Q did not arrive in C through r8 in both builds")
if b"SSC_GETCHAR=0x51\r\n" not in fixed_output:
    raise SystemExit("[SSC_RETURN_FAIL] SSC GETCHAR result did not return through r8 as expected")
if b"SSC_GETCHAR=0x77\r\n" not in legacy_output:
    raise SystemExit("[LEGACY_DISPLAY_FAIL] expected legacy formatter corruption was not reproduced")

fixed_disassembly = (out / "fixed-disassembly.txt").read_text(encoding="utf-8")
required_bridge = (
    "alloc r34=ar.pfs,3,3,0",
    "mov r15=r32",
    "mov r32=r33",
    "break.i 0x80000",
    "mov.i ar.pfs=r34",
    "br.ret.sptk.many b0",
)
if not all(instruction in fixed_disassembly for instruction in required_bridge):
    raise SystemExit("[SSC_ABI_FAIL] fixed target wrapper does not allocate inputs, map SSC registers, and restore ar.pfs")

manifest = {
    "schema": "guidexos-ia64-ski-register-diagnostic-v1",
    "baseline_source_commit": baseline,
    "guest_kernel_execution_authorized": False,
    "getchar_test_byte": "Q",
    "programs": {},
}
for variant in ("legacy", "fixed"):
    elf = out / f"{variant}-diagnostic.elf"
    stdout = out / variant / "stdout.log"
    stderr = out / variant / "stderr.log"
    manifest["programs"][variant] = {
        "elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest(),
        "exit_status": int((out / variant / "exit-status.txt").read_text().strip()),
        "stdout_sha256": hashlib.sha256(stdout.read_bytes()).hexdigest(),
        "stderr_sha256": hashlib.sha256(stderr.read_bytes()).hexdigest(),
        "hex_regression": "expected-legacy-failure" if variant == "legacy" and old == legacy_expected else
                          "correct-output-pass" if variant == "fixed" and new == expected else "FAIL",
        "register_probe": "PASS" if b"REGISTER_PROBE=PASS\r\n" in stdout.read_bytes() else "FAIL",
        "fpsr_readback": "PASS" if b"FPSR_READBACK=PASS\r\n" in stdout.read_bytes() else "FAIL",
        "getchar_api_result": "PASS (input Q returned through r8)" if b"SSC_GETCHAR_API=PASS\r\n" in stdout.read_bytes() else "FAIL",
        "getchar_display": "0x77 (expected legacy formatter corruption)" if variant == "legacy" and b"SSC_GETCHAR=0x77\r\n" in stdout.read_bytes() else
                           "0x51" if variant == "fixed" and b"SSC_GETCHAR=0x51\r\n" in stdout.read_bytes() else "FAIL",
    }
(out / "diagnostic-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
print("[REGRESSION] Legacy implementation fails byte-exact hexadecimal output as expected.")
print("[REGRESSION] Corrected ABI wrapper passes byte-exact hexadecimal output.")
print("[REGISTER_PROBE] Known IVA/BSPSTORE/RSC/PSR checks passed; RNAT remains UNVERIFIED.")
print("[FPSR_READBACK] Known AR.FPSR value wrote and read back in both probes.")
for variant in ("legacy", "fixed"):
    print(f"[{variant.upper()}_ELF_SHA256] {manifest['programs'][variant]['elf_sha256']}")
PY
