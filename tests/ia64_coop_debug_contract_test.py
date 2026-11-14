#!/usr/bin/env python3
"""Negative and boundary tests for the one-run cooperative Ski diagnostic."""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "diagnose-ia64-coop-switch.py"
spec = importlib.util.spec_from_file_location("ia64_coop_debug_diagnostic", SCRIPT)
assert spec is not None and spec.loader is not None
diagnostic = importlib.util.module_from_spec(spec)
spec.loader.exec_module(diagnostic)


RANGES = ((0x100000, 0x108000), (0x108000, 0x10E1D0))


def progress(*samples: tuple[int, int]) -> str:
    lines: list[str] = []
    for ip, count in samples:
        lines.extend((f"Hex: {ip:x} Decimal: {ip}", f"$insts$ = {count}"))
    return "\n".join(lines) + "\n"


def dd_dump(base: int, words: list[int]) -> str:
    lines: list[str] = []
    for index in range(0, len(words), 2):
        group = words[index:index + 2]
        lines.append(f"{base + index * 8:016x}: " + " ".join(f"{word:016x}" for word in group))
    return "\n".join(lines) + "\n"


class ProgressObservationTests(unittest.TestCase):
    def test_valid_samples_decode_iip_slot_and_instruction_count(self) -> None:
        samples = diagnostic.parse_progress_samples(
            progress((0x108000, 0), (0x108004, 1), (0x10AE90, 513)), RANGES)
        self.assertEqual(samples[0], {
            "sample": 0, "instructions": 0, "ip": 0x108000,
            "iip": 0x108000, "slot": 0,
        })
        self.assertEqual((samples[1]["iip"], samples[1]["slot"]), (0x108000, 1))
        self.assertEqual((samples[2]["iip"], samples[2]["slot"]), (0x10AE90, 0))

    def test_repeated_iip_is_preserved_as_loop_evidence(self) -> None:
        samples = diagnostic.parse_progress_samples(
            progress((0x108004, 1), (0x108004, 513), (0x108004, 1025)), RANGES)
        self.assertEqual([sample["iip"] for sample in samples], [0x108000] * 3)
        self.assertEqual([sample["instructions"] for sample in samples], [1, 513, 1025])

    def test_missing_or_unpaired_progress_is_rejected(self) -> None:
        for text in ("Ski stopped before eval\n", "Hex: 108000 Decimal: 1081344\n"):
            with self.subTest(text=text), self.assertRaises(diagnostic.DiagnosticError):
                diagnostic.parse_progress_samples(text, RANGES)

    def test_invalid_slot_outside_range_and_nonincreasing_count_are_rejected(self) -> None:
        cases = (
            progress((0x10800C, 1)),
            progress((0x200000, 1)),
            progress((0x108000, 5), (0x108004, 5)),
            progress((0x108000, diagnostic.MAX_STEPPED_INSTRUCTIONS + 1)),
        )
        for text in cases:
            with self.subTest(text=text), self.assertRaises(diagnostic.DiagnosticError):
                diagnostic.parse_progress_samples(text, RANGES)

    def test_only_the_fixed_instruction_budget_is_supported(self) -> None:
        self.assertEqual(diagnostic.validate_instruction_budget(
            diagnostic.SUPPORTED_INSTRUCTION_BUDGET),
            diagnostic.SUPPORTED_INSTRUCTION_BUDGET)
        for value in (0, 512, diagnostic.SUPPORTED_INSTRUCTION_BUDGET + 512):
            with self.subTest(value=value), self.assertRaises(diagnostic.DiagnosticError):
                diagnostic.validate_instruction_budget(value)


class SnapshotContractTests(unittest.TestCase):
    def test_context_identity_is_checked_and_zero_means_not_initialized(self) -> None:
        words = [0] * 128
        self.assertIsNone(diagnostic.validate_context_words(words, 0xA))
        words[diagnostic.CONTEXT_ID_OFFSET // 8] = 0xA
        words[8] = 0x100100
        words[9] = 0x123
        words[10] = 3
        words[11] = 0x133000
        words[12] = 0x133000
        state = diagnostic.validate_context_words(words, 0xA)
        self.assertEqual(state["context_id"], 0xA)
        self.assertEqual(state["bspstore"], 0x133000)
        words[diagnostic.CONTEXT_ID_OFFSET // 8] = 0xB
        with self.assertRaises(diagnostic.DiagnosticError):
            diagnostic.validate_context_words(words, 0xA)

    def test_two_qword_memory_dump_parser_requires_contiguous_complete_data(self) -> None:
        base = 0x112200
        words = list(range(128))
        parsed = diagnostic.parse_dd_words(dd_dump(base, words), base, 1024)
        self.assertEqual(parsed, words)
        symbolic_lines = dd_dump(base, words).splitlines()
        symbolic_lines[0] = "ia64_coop_context_a+0000: " + symbolic_lines[0].split(": ", 1)[1]
        self.assertEqual(diagnostic.parse_dd_words("\n".join(symbolic_lines), base, 1024), words)
        with self.assertRaises(diagnostic.DiagnosticError):
            diagnostic.parse_dd_words(dd_dump(base, words[:-1]), base, 1024)

    def test_probe_event_identity_order_and_count_are_checked(self) -> None:
        words = [0] * 43
        words[0:4] = [1, 1, 0xA, 1]
        record = diagnostic.validate_probe_record_words(words)
        self.assertEqual(record["events"][0]["event"], "A_TO_B")
        words[2] = 0xB
        with self.assertRaises(diagnostic.DiagnosticError):
            diagnostic.validate_probe_record_words(words)

    def test_stale_records_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "snapshot.txt"
            path.write_text("preserved\n", encoding="ascii")
            os.utime(path, (1, 1))
            with self.assertRaises(diagnostic.DiagnosticError):
                diagnostic.validate_fresh_records([path], time.time_ns())


class LocalizedFrontierTests(unittest.TestCase):
    def evidence(self) -> tuple[list[dict[str, int]], list[dict[str, object]],
                                list[dict[str, object]], dict[str, list[dict[str, object]]]]:
        samples: list[dict[str, int]] = []
        registers: list[dict[str, object]] = []
        snapshots: list[dict[str, object]] = []
        for index in range(33):
            instructions = index * 512
            iip = 0x10A2F0 + (index % 4) * 0x10
            samples.append({"sample": index, "instructions": instructions,
                            "ip": iip, "iip": iip, "slot": 0})
            registers.append({"instructions": instructions,
                              "registers": {"b0": diagnostic.SETUP_CALL_CONTINUATION}})
            snapshots.append({
                "sample": index,
                "context_a": None if index == 0 else {"context_id": 0xA, "switch_count": 0},
                "context_b": None if index == 0 else {"context_id": 0xB, "switch_count": 0},
                "probe_record": {"event_count": 0, "errors": 0},
            })
        breakpoints = {
            "SSC_CONSOLE_INIT break.i slot 2": [{"instructions": 33}],
            "ia64_coop_context_switch entry": [],
            "flushrs slot 1": [],
            "incoming ar.bspstore write slot 1": [],
            "Context B first-entry bootstrap": [],
            "Context A first return continuation": [],
            "Context B resume continuation": [],
            "Context A final return continuation": [],
            "switch br.ret slot 2": [],
        }
        return samples, registers, snapshots, breakpoints

    def test_setup_reentry_requires_many_matching_state_observations(self) -> None:
        samples, registers, snapshots, breakpoints = self.evidence()
        result = diagnostic.classify_setup_return_cycle(
            samples, registers, snapshots, breakpoints, stdout_bytes=0)
        self.assertIsNotNone(result)
        assert result is not None
        self.assertEqual(result["classification"], "IA64_COOPERATIVE_STALL_LOCALIZED")
        self.assertEqual(result["corroborating_samples"], 32)
        self.assertEqual(result["stable_b0"], 0x10A2E0)

    def test_single_pc_wrong_return_address_or_event_cannot_localize_setup(self) -> None:
        samples, registers, snapshots, breakpoints = self.evidence()
        single_pc = [dict(sample, iip=0x10A2F0) for sample in samples]
        self.assertIsNone(diagnostic.classify_setup_return_cycle(
            single_pc, registers, snapshots, breakpoints, stdout_bytes=0))

        samples, registers, snapshots, breakpoints = self.evidence()
        registers[-1]["registers"]["b0"] = 0x10AE00
        self.assertIsNone(diagnostic.classify_setup_return_cycle(
            samples, registers, snapshots, breakpoints, stdout_bytes=0))

        samples, registers, snapshots, breakpoints = self.evidence()
        snapshots[-1]["probe_record"]["event_count"] = 1
        self.assertIsNone(diagnostic.classify_setup_return_cycle(
            samples, registers, snapshots, breakpoints, stdout_bytes=0))

    def test_output_or_missing_switch_breakpoint_evidence_blocks_localization(self) -> None:
        samples, registers, snapshots, breakpoints = self.evidence()
        self.assertIsNone(diagnostic.classify_setup_return_cycle(
            samples, registers, snapshots, breakpoints, stdout_bytes=1))
        breakpoints["ia64_coop_context_switch entry"] = [{"instructions": 1000}]
        self.assertIsNone(diagnostic.classify_setup_return_cycle(
            samples, registers, snapshots, breakpoints, stdout_bytes=0))

        samples, registers, snapshots, breakpoints = self.evidence()
        del breakpoints["switch br.ret slot 2"]
        self.assertIsNone(diagnostic.classify_setup_return_cycle(
            samples, registers, snapshots, breakpoints, stdout_bytes=0))


class CommandFileContractTests(unittest.TestCase):
    def test_command_file_has_finite_staged_steps_and_exit_guards(self) -> None:
        symbols = {
            "ivt": 0x100000,
            "text": [0x108000, 0x10E1D0],
            "context_a": 0x112600,
            "context_b": 0x112200,
            "probe_record": 0x112000,
            "switch_entry": 0x10CF90,
            "context_b_first_entry": 0x10AE90,
        }
        text = diagnostic.make_command_file(Path("/tmp/coop-debug"), symbols)
        diagnostic.validate_command_file(text)
        self.assertNotRegex(text, r"(?im)^\s*(run|cont(?:inue)?|go)(?:\s|$)")
        self.assertEqual(text.splitlines()[-1], "quit")
        self.assertIn("eval psr rsc bsp bspst rnat unat pfs sp gp b0 r32 r33 r42 r43", text)

    def test_command_file_rejects_unbounded_or_missing_quit(self) -> None:
        symbols = {
            "ivt": 0x100000,
            "text": [0x108000, 0x10E1D0],
            "context_a": 0x112600,
            "context_b": 0x112200,
            "probe_record": 0x112000,
            "switch_entry": 0x10CF90,
            "context_b_first_entry": 0x10AE90,
        }
        text = diagnostic.make_command_file(Path("/tmp/coop-debug"), symbols)
        with self.assertRaises(diagnostic.DiagnosticError):
            diagnostic.validate_command_file(text.replace("step 64", "run", 1))
        with self.assertRaises(diagnostic.DiagnosticError):
            diagnostic.validate_command_file(text.removesuffix("quit\n"))


if __name__ == "__main__":
    unittest.main()
