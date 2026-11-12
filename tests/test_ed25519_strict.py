#!/usr/bin/env python3
"""Build hosted and freestanding libsodium objects and run strict vectors."""

import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PROFILE = ROOT / "guidexos" / "libsodium_ed25519_sources.mk"
WYCH = ROOT / "tests" / "vectors" / "wycheproof_ed25519_test.json"
INDEPENDENT = ROOT / "tests" / "vectors" / "ed25519_independent.json"
VENDOR_HASHES = ROOT / "guidexos" / "libsodium_ed25519_sources.sha256"
VECTOR_HASHES = ROOT / "tests" / "vectors" / "ed25519_vectors.sha256"
RUNNER = ROOT / "tests" / "ed25519" / "strict_runner.cpp"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def source_paths():
    expected = {}
    for row in VENDOR_HASHES.read_text(encoding="ascii").splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  (third_party/libsodium/.+)", row)
        require(match is not None, f"malformed pinned source hash row: {row}")
        digest, relative = match.groups()
        path = ROOT / Path(relative)
        require(path.is_file(), f"missing pinned source file: {relative}")
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        require(actual == digest, f"pinned source digest mismatch: {relative}")
        expected[relative] = digest
    actual_files = {path.relative_to(ROOT).as_posix()
                    for path in (ROOT / "third_party" / "libsodium").rglob("*")
                    if path.is_file()}
    require(actual_files == set(expected), "libsodium vendor tree differs from pinned inventory")

    text = PROFILE.read_text(encoding="utf-8")
    block = text.split("GUIDEXOS_ED25519_C_SOURCES :=", 1)[1].split("\n\n", 1)[0]
    matches = re.findall(r"\$\(GUIDEXOS_ED25519_ROOT\)/([^\s\\]+\.c)", block)
    require(len(matches) == 9, f"expected 9 pinned C sources in profile, found {len(matches)}")
    return [ROOT / "third_party" / "libsodium" / "src" / name for name in matches]


def verify_vector_hashes():
    expected = set()
    for row in VECTOR_HASHES.read_text(encoding="ascii").splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  (tests/vectors/.+)", row)
        require(match is not None, f"malformed Ed25519 vector hash row: {row}")
        digest, relative = match.groups()
        require(relative not in expected, f"duplicate Ed25519 vector hash path: {relative}")
        path = ROOT / Path(relative)
        require(path.is_file(), f"missing Ed25519 vector file: {relative}")
        require(hashlib.sha256(path.read_bytes()).hexdigest() == digest,
                f"Ed25519 vector digest mismatch: {relative}")
        expected.add(relative)
    require({"tests/vectors/wycheproof_ed25519_test.json",
             "tests/vectors/ed25519_independent.json"}.issubset(expected),
            "pinned Ed25519 vector inventory is incomplete")


def encode_case(pk, sig, msg, pk_len=None, sig_len=None, msg_len=None,
                null_pk=False, null_sig=False, null_msg=False):
    if pk_len is None:
        pk_len = 0 if pk is None else len(pk)
    if sig_len is None:
        sig_len = 0 if sig is None else len(sig)
    if msg_len is None:
        msg_len = len(msg)
    return "|".join((
        pk.hex() if pk is not None else "-",
        sig.hex() if sig is not None else "-",
        msg.hex() if msg is not None else "-",
        str(pk_len), str(sig_len), str(msg_len),
        "1" if null_pk else "0",
        "1" if null_sig else "0",
        "1" if null_msg else "0",
    ))


def max_call_chain_stack(graph_paths, entrypoint):
    frames = {}
    calls = defaultdict(set)
    for report in graph_paths:
        for line in report.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("node:"):
                match = re.search(r'title: "([^"]+)"\s+label: "([^"]*)"', line)
                if match:
                    title, label = match.groups()
                    stack = re.search(r"\\n(\d+) bytes \(([^)]+)\)", label)
                    if stack:
                        frames[title] = max(frames.get(title, 0), int(stack.group(1)))
            elif line.startswith("edge:"):
                match = re.search(r'sourcename: "([^"]+)" targetname: "([^"]+)"', line)
                if match:
                    calls[match.group(1)].add(match.group(2))

    cycles = []

    def visit(name, path, total):
        if name in path:
            cycles.append(path[path.index(name):] + [name])
            return total, path
        next_path = path + [name]
        total += frames.get(name, 0)
        best = (total, next_path)
        for child in calls.get(name, ()):
            candidate = visit(child, next_path, total)
            if candidate[0] > best[0]:
                best = candidate
        return best

    result = visit(entrypoint, [], 0)
    require(not cycles, f"recursive call cycle in verifier call graph: {cycles[:3]}")
    missing = sorted({name for name in result[1] if name not in frames})
    return result[0], result[1], missing


def main():
    verify_vector_hashes()
    compiler = os.environ.get("CC", "gcc")
    cxx = os.environ.get("CXX", "g++")
    with tempfile.TemporaryDirectory(prefix="gxos-ed25519-") as temp_name:
        temp = Path(temp_name)
        objects = []
        hosted_objects = []
        freestanding_flags = [
            "-std=c11", "-O2", "-Wall", "-Wextra", "-ffreestanding",
            "-fno-builtin", "-fno-stack-protector", "-fstack-usage",
            "-fcallgraph-info=su",
            "-Wno-unused-function",
            "-DGXOS_BARE_METAL", "-m64", "-mcmodel=large", "-mno-red-zone",
            "-mno-mmx", "-mno-sse", "-mno-sse2", "-fno-pic", "-fno-pie",
            "-mno-stack-arg-probe", "-fno-stack-check",
            "-DCONFIGURED=1", "-DSODIUM_STATIC", "-DHAVE_TI_MODE=1",
            "-Dabort=__builtin_trap",
            f"-I{ROOT / 'kernel' / 'core' / 'include'}",
        ]
        include_dirs = [
            "include", "include/sodium", "crypto_hash/sha512", "crypto_hash/sha512/cp",
            "crypto_sign/ed25519/ref10", "crypto_verify", "crypto_core/ed25519",
            "crypto_core/ed25519/ref10",
        ]
        for directory in include_dirs:
            freestanding_flags.append(f"-I{ROOT / 'third_party' / 'libsodium' / 'src' / directory}")

        hosted_flags = [
            "-std=c11", "-O2", "-Wall", "-Wextra", "-Wno-unused-function",
            "-DCONFIGURED=1", "-DSODIUM_STATIC", "-DHAVE_TI_MODE=1",
            "-Dabort=__builtin_trap",
            f"-I{ROOT / 'kernel' / 'core' / 'include'}",
            *(f"-I{ROOT / 'third_party' / 'libsodium' / 'src' / directory}"
              for directory in include_dirs),
        ]

        for index, source in enumerate(source_paths()):
            require(source.is_file(), f"missing pinned source {source}")
            obj = temp / f"backend-{index}.o"
            subprocess.run([compiler, *freestanding_flags, "-c", str(source), "-o", str(obj)],
                           cwd=ROOT, check=True)
            objects.append(obj)
            hosted_obj = temp / f"hosted-{index}.o"
            subprocess.run([compiler, *hosted_flags, "-c", str(source), "-o", str(hosted_obj)],
                           cwd=ROOT, check=True)
            hosted_objects.append(hosted_obj)

        runtime_obj = temp / "kernel-cxx-runtime.o"
        runtime_flags = [
            "-std=c++14", "-O2", "-Wall", "-Wextra", "-ffreestanding",
            "-fno-exceptions", "-fno-rtti", "-nostdlib", "-nostdinc++",
            "-fno-builtin", "-Wno-unused-function",
            "-fstack-usage", "-fcallgraph-info=su",
            "-m64", "-mcmodel=large", "-mno-red-zone", "-mno-mmx",
            "-mno-sse", "-mno-sse2", "-fno-pic", "-fno-pie",
            "-mno-stack-arg-probe", "-fno-stack-check", "-fno-stack-protector",
            "-DGXOS_BARE_METAL", "-I", str(ROOT / "kernel" / "core" / "include"),
            "-I", str(ROOT / "kernel" / "arch" / "amd64" / "include"),
        ]
        subprocess.run([cxx, *runtime_flags, "-c",
                        str(ROOT / "kernel" / "core" / "cxx_runtime.cpp"),
                        "-o", str(runtime_obj)], cwd=ROOT, check=True)

        executables = {}
        for mode, mode_objects in (("freestanding", objects), ("hosted", hosted_objects)):
            exe = temp / (f"strict-runner-{mode}.exe" if os.name == "nt"
                          else f"strict-runner-{mode}")
            subprocess.run([cxx, "-std=c++14", "-O2", "-Wall", "-Wextra",
                            str(RUNNER), *(str(obj) for obj in mode_objects), "-o", str(exe)],
                           cwd=ROOT, check=True)
            executables[mode] = exe

        custom_doc = json.loads(INDEPENDENT.read_text(encoding="utf-8"))
        custom = []
        reference_checks = 0
        for vector in custom_doc["vectors"]:
            pk = bytes.fromhex(vector["public_key"])
            msg = bytes.fromhex(vector["message"])
            sig = bytes.fromhex(vector["signature"])
            custom.append((encode_case(pk, sig, msg), True, vector["name"]))
        try:
            from cryptography.exceptions import InvalidSignature
            from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey

            for vector in custom_doc["vectors"]:
                Ed25519PublicKey.from_public_bytes(bytes.fromhex(vector["public_key"])).verify(
                    bytes.fromhex(vector["signature"]), bytes.fromhex(vector["message"]))
                reference_checks += 1
            try:
                Ed25519PublicKey.from_public_bytes(bytes.fromhex(
                    custom_doc["vectors"][1]["public_key"])).verify(
                        bytes.fromhex(custom_doc["vectors"][1]["signature"]), b"s")
            except InvalidSignature:
                reference_checks += 1
            else:
                raise RuntimeError("independent cryptography reference accepted corrupted message")
        except ImportError:
            pass

        by_name = {entry["name"]: entry for entry in custom_doc["vectors"]}
        rfc1 = by_name["rfc8032-test-1-empty"]
        rfc2 = by_name["rfc8032-test-2-one-byte"]
        valid_pk = bytes.fromhex(rfc1["public_key"])
        valid_sig = bytes.fromhex(rfc1["signature"])
        empty = b""
        malformed = []

        def reject(name, pk, sig, msg=b"", **kw):
            malformed.append((encode_case(pk, sig, msg, **kw), False, name))

        reject("corrupt-message", bytes.fromhex(rfc2["public_key"]),
               bytes.fromhex(rfc2["signature"]), b"s")
        altered_sig = bytearray(valid_sig)
        altered_sig[0] ^= 1
        reject("corrupt-signature", valid_pk, bytes(altered_sig), empty)
        reject("wrong-valid-public-key", bytes.fromhex(rfc2["public_key"]), valid_sig, empty)

        zeros32 = bytes(32)
        zeros64 = bytes(64)
        identity = b"\x01" + bytes(31)
        order2 = bytes.fromhex("ec" + "ff" * 30 + "7f")
        noncanonical_y = bytes.fromhex("ed" + "ff" * 30 + "7f")
        signbit_zero_x = bytes.fromhex("01" + "00" * 30 + "80")
        order2_sig = order2 + bytes(32)
        identity_sig = identity + bytes(32)
        reject("all-zero-key-and-signature", zeros32, zeros64)
        reject("low-order-identity-public-key", identity, zeros64)
        reject("small-subgroup-order2-key-and-signature", order2, order2_sig, b"\x00")
        reject("noncanonical-public-key", noncanonical_y, zeros64)
        reject("noncanonical-public-key-sign-bit", signbit_zero_x, zeros64)
        reject("identity-public-key-and-R", identity, identity_sig)
        reject("small-subgroup-signature-R", valid_pk, order2_sig, b"\x00")
        reject("invalid-signature-R", valid_pk, bytes.fromhex("ff" * 32) + bytes(32))
        reject("noncanonical-signature-R", valid_pk, noncanonical_y + bytes(32))
        scalar_l = bytes.fromhex("edd3f55c1a631258d69cf7a2def9de1400000000000000000000000000000010")
        reject("noncanonical-signature-scalar-L", valid_pk, valid_sig[:32] + scalar_l, empty)

        reject("short-public-key", valid_pk, valid_sig, empty, pk_len=31)
        reject("long-public-key", valid_pk, valid_sig, empty, pk_len=33)
        reject("short-signature", valid_pk, valid_sig, empty, sig_len=63)
        reject("long-signature", valid_pk, valid_sig, empty, sig_len=65)
        reject("null-public-key", valid_pk, valid_sig, empty, null_pk=True)
        reject("null-signature", valid_pk, valid_sig, empty, null_sig=True)
        malformed.append((encode_case(valid_pk, valid_sig, empty, null_msg=True), True,
                          "null-zero-length-message"))
        reject("null-nonempty-message", valid_pk, valid_sig, b"\x00", msg_len=1, null_msg=True)
        reject("message-over-maximum", valid_pk, valid_sig, bytes(4097), msg_len=4097)

        corpus = json.loads(WYCH.read_text(encoding="utf-8"))
        wycheproof = []
        category_counts = {"valid": 0, "invalid": 0, "acceptable": 0}
        for group in corpus["testGroups"]:
            pk = bytes.fromhex(group["publicKey"]["pk"])
            for test in group["tests"]:
                category = test["result"]
                category_counts[category] += 1
                msg = bytes.fromhex(test["msg"])
                sig = bytes.fromhex(test["sig"])
                expected = category == "valid"
                wycheproof.append((encode_case(pk, sig, msg), expected,
                                   f"wycheproof-{test['tcId']}-{category}"))

        cases = custom + malformed + wycheproof
        payload = "".join(case[0] + "\n" for case in cases)
        for mode, exe in executables.items():
            process = subprocess.run([str(exe)], input=payload, text=True, capture_output=True,
                                     cwd=ROOT, check=True)
            outcomes = process.stdout.splitlines()
            require(len(outcomes) == len(cases),
                    f"{mode} runner returned {len(outcomes)} outcomes for {len(cases)} cases")
            failed = []
            for (line, expected, name), actual in zip(cases, outcomes):
                if actual not in ("0", "1") or (actual == "1") != expected:
                    failed.append((name, expected, actual))
            require(not failed, f"{mode} strict verification failures: {failed[:12]}")

        stack_entries = []
        for report in temp.glob("backend-*.su"):
            for row in report.read_text(encoding="utf-8", errors="replace").splitlines():
                fields = row.rsplit("\t", 2)
                if len(fields) == 3 and fields[1].isdigit():
                    stack_entries.append((fields[0], int(fields[1]), fields[2]))
        require(bool(stack_entries), "compiler did not produce -fstack-usage reports")
        dynamic_entries = [entry for entry in stack_entries if "dynamic" in entry[2]]
        stack_sum = sum(entry[1] for entry in stack_entries)
        largest_frame = max(stack_entries, key=lambda entry: entry[1])
        object_bytes = sum(obj.stat().st_size for obj in objects)
        stack_upper, stack_path, missing_stack_nodes = max_call_chain_stack(
            temp.glob("*.ci"), "gxos_ed25519_verify_strict")
        require(stack_upper < 16 * 1024,
                f"reachable verifier stack estimate {stack_upper} exceeds the AMD64 16 KiB boot stack")
        report_dir = os.environ.get("GXOS_ED25519_REPORT_DIR")
        if report_dir:
            destination = Path(report_dir)
            destination.mkdir(parents=True, exist_ok=True)
            for report in (*temp.glob("*.su"), *temp.glob("*.ci")):
                shutil.copy2(report, destination / report.name)
            print(f"compiler_reports={destination.resolve()}")

        print(f"backend=libsodium-1.0.22 commit=77e1ce5d6dee871c49ef211222ba18ef0c486bda")
        print(f"freestanding_objects={len(objects)} compiler={compiler}")
        print(f"hosted_cases={len(cases)} freestanding_cases={len(cases)} failures=0")
        print(f"independent_valid={len(custom)} direct_api_cases={len(malformed)} "
              "(21 negative, 1 null-empty success)")
        print(f"cryptography_reference_checks={reference_checks}")
        print("wycheproof=" + ",".join(f"{k}:{v}" for k, v in category_counts.items()) +
              " (valid accepted; invalid and acceptable rejected)")
        print(f"unique_cases={len(cases)} failures=0")
        print(f"backend_object_bytes={object_bytes} static_stack_frames={len(stack_entries)} "
              f"largest_frame={largest_frame[1]}:{largest_frame[0]}")
        print(f"static_stack_frame_sum_upper_bound={stack_sum} "
              f"dynamic_frame_reports={len(dynamic_entries)}")
        print(f"reachable_call_chain_stack_upper_bound={stack_upper} "
              f"path={' -> '.join(stack_path)}")
        print(f"reachable_unmeasured_callees={','.join(missing_stack_nodes) or 'none'}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError, ValueError) as exc:
        print(f"Ed25519 strict corpus failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
