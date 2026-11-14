#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import json
import struct
import tempfile
import unittest
from pathlib import Path

analyzer_path = Path(__file__).with_name("analyze-c188r25-failfast-origin.py")
spec = importlib.util.spec_from_file_location("r25_failfast_analyzer", analyzer_path)
assert spec is not None and spec.loader is not None
analyzer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analyzer)


def valid_words() -> list[int]:
    return [analyzer.MAGIC, analyzer.SCHEMA, analyzer.RECORD_BYTES, 9, 7,
            0x100012345, 0x1000ABCDE, 2, 7, analyzer.PUBLISHED]


class FailFastOriginTests(unittest.TestCase):
    def test_valid_first_failure_record(self) -> None:
        words = valid_words()
        self.assertEqual(analyzer.validate_words(words), (True, "first_failure_record_valid"))
        self.assertEqual(words[3], 9)  # source diagnostic reason retained by the record
        self.assertEqual(words[4], analyzer.FAST_FAIL_FATAL_APP_EXIT)

    def test_local_reason_is_distinct_from_fast_fail_code_seven(self) -> None:
        words = valid_words()
        self.assertNotEqual(words[3], words[4])
        self.assertEqual(words[4], 7)
        self.assertEqual(analyzer.FAST_FAIL_FATAL_APP_EXIT, 7)

    def test_rejects_wrong_magic(self) -> None:
        words = valid_words(); words[0] ^= 1
        self.assertEqual(analyzer.validate_words(words)[1], "record_magic_invalid")

    def test_rejects_wrong_schema(self) -> None:
        words = valid_words(); words[1] = 2
        self.assertEqual(analyzer.validate_words(words)[1], "record_schema_invalid")

    def test_rejects_wrong_bounds(self) -> None:
        words = valid_words(); words[2] = 88
        self.assertEqual(analyzer.validate_words(words)[1], "record_size_invalid")

    def test_rejects_wrong_fast_fail_mapping(self) -> None:
        words = valid_words(); words[4] = 6
        self.assertEqual(analyzer.validate_words(words)[1], "fast_fail_code_unexpected")

    def test_rejects_missing_caller(self) -> None:
        words = valid_words(); words[5] = 0
        self.assertEqual(analyzer.validate_words(words)[1], "caller_capture_address_missing")

    def test_rejects_missing_failfast_implementation(self) -> None:
        words = valid_words(); words[6] = 0
        self.assertEqual(analyzer.validate_words(words)[1], "failfast_implementation_address_missing")

    def test_rejects_unknown_initialization_stage(self) -> None:
        words = valid_words(); words[7] = 4
        self.assertEqual(analyzer.validate_words(words)[1], "initialization_stage_invalid")

    def test_rejects_unknown_record_flags(self) -> None:
        words = valid_words(); words[8] = 3
        self.assertEqual(analyzer.validate_words(words)[1], "record_flags_invalid")

    def test_rejects_unpublished_record(self) -> None:
        words = valid_words(); words[9] = 0
        self.assertEqual(analyzer.validate_words(words)[1], "record_not_published")

    def test_rejects_wrong_record_length(self) -> None:
        self.assertEqual(analyzer.validate_words(valid_words()[:-1])[1], "record_word_count_invalid")

    def test_reason_seven_snapshot_is_terminal_not_origin(self) -> None:
        result = analyzer.classify_r24_registers(
            "RIP=00000001000F2F77 RCX=0000000000000007 RSP=000000000517F058",
            0x1000F2F70, 0x1000F2F77,
        )
        self.assertEqual(result["classification"], "shared_fastfail_reason_7_terminal_path")
        self.assertFalse(result["terminal_loop_is_original_site"])

    def test_other_rip_does_not_classify_terminal_fast_fail(self) -> None:
        result = analyzer.classify_r24_registers(
            "RIP=00000001000F3196 RCX=0000000000000007", 0x1000F2F70, 0x1000F2F77
        )
        self.assertEqual(result["classification"], "unattributed_cpu_state")

    def test_hmp_parser_ignores_line_addresses(self) -> None:
        text = "0x0000000100100000: 0x0000000000000001 0x0000000000000002\n"
        self.assertEqual(analyzer.parse_hmp_words(text), [1, 2])

    def test_hmp_record_round_trips_as_ten_bounded_words(self) -> None:
        words = valid_words()
        text = "0x0000000100100000: " + " ".join(f"0x{word:016x}" for word in words) + "\n"
        self.assertEqual(analyzer.parse_hmp_words(text), words)
        self.assertEqual(analyzer.validate_words(analyzer.parse_hmp_words(text)),
                         (True, "first_failure_record_valid"))

    def _make_candidate(self, bad_call: bool = False) -> tuple[tempfile.TemporaryDirectory[str], Path]:
        temporary = tempfile.TemporaryDirectory()
        candidate = Path(temporary.name)
        elf = bytearray(0x300)
        elf[:4] = b"\x7fELF"
        elf[4:6] = b"\x02\x01"
        struct.pack_into("<Q", elf, 32, 64)
        struct.pack_into("<HH", elf, 54, 56, 1)
        # The writable load segment has a zero-filled tail, where the record lives.
        struct.pack_into("<IIQQQQQQ", elf, 64, 1, 7, 0x100, 0x1000, 0, 0x100, 0x200, 0x1000)
        caller_pc = 0x1025
        call_offset = 0x100 + caller_pc - 0x1000 - 5
        elf[call_offset] = 0xE9 if bad_call else 0xE8
        struct.pack_into("<i", elf, call_offset + 1, 0x1000 - caller_pc)
        elf_path = candidate / "managed.elf"
        elf_path.write_bytes(elf)
        map_path = candidate / "managed.map"
        map_path.write_text(
            "0000000000000000 guideXosNativeAotR25CaptureCallerReturnAddress 0000000000001000 f runtime-pack/platform.obj\n"
            "0000000000000000 guideXosFailFastImpl 0000000000001008 f runtime-pack/platform.obj\n"
            "0000000000000000 failAtInitialization 0000000000001018 f runtime-pack/platform.obj\n",
            encoding="utf-8",
        )
        map_path.write_text(
            map_path.read_text(encoding="utf-8")
            + "0000000000000000 g_guideXosR25FailFastOriginRecord 0000000000001100 .bss\n",
            encoding="utf-8",
        )
        runtime_dir = candidate / "runtime-pack"
        runtime_dir.mkdir()
        runtime_manifest = runtime_dir / "runtime-pack.manifest.json"
        runtime_manifest.write_text(json.dumps({"c188r25FailFastOriginCapture": True, "identity": "test"}), encoding="utf-8")
        (candidate / "candidate-manifest.json").write_text(
            json.dumps({"managedMap": str(map_path), "managedElf": str(elf_path),
                        "runtimePackManifest": str(runtime_manifest), "managedElfSha256": "test"}),
            encoding="utf-8",
        )
        return temporary, candidate

    def test_resolves_caller_only_after_direct_capture_call(self) -> None:
        temporary, candidate = self._make_candidate()
        try:
            result = analyzer.resolve_caller(candidate, 0x1025)
            self.assertTrue(result["resolved"])
            self.assertEqual(result["enclosingSymbol"], "failAtInitialization")
            self.assertEqual(result["objectProvenance"], "runtime-pack/platform.obj")
            self.assertFalse(result["failureContractProven"])
        finally:
            temporary.cleanup()

    def test_rejects_invalid_capture_instruction_for_attribution(self) -> None:
        temporary, candidate = self._make_candidate(bad_call=True)
        try:
            result = analyzer.resolve_caller(candidate, 0x1025)
            self.assertFalse(result["resolved"])
            self.assertEqual(result["reason"], "capture_return_pc_not_after_direct_call")
        finally:
            temporary.cleanup()

    def test_rejects_caller_outside_executable_segment(self) -> None:
        temporary, candidate = self._make_candidate()
        try:
            result = analyzer.resolve_caller(candidate, 0x1800)
            self.assertFalse(result["resolved"])
            self.assertEqual(result["reason"], "caller_pc_outside_executable_load_segment")
        finally:
            temporary.cleanup()

    def test_record_is_qualified_in_writable_zero_filled_segment_tail(self) -> None:
        temporary, candidate = self._make_candidate()
        try:
            result = analyzer.verify_candidate(candidate)
            self.assertTrue(result["recordSectionWritable"])
            self.assertEqual(result["recordAddress"], "0x0000000000001100")
        finally:
            temporary.cleanup()


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(FailFastOriginTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    print(f"R25_FAILFAST_ORIGIN_TESTS={result.testsRun} RESULT={'PASS' if result.wasSuccessful() else 'FAIL'}")
    raise SystemExit(not result.wasSuccessful())
