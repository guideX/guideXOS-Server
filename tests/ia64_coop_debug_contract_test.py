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


class DebuggerInitializationFailureTests(unittest.TestCase):
    def test_breakpoint_capacity_error_stops_at_zero_instruction_elf_entry(self) -> None:
        commands = "\n".join(f"bs 0x{0x108000 + index * 0x10:x}"
                              for index in range(16))
        samples = [{"iip": 0x108000, "ip": 0x108000, "instructions": 0,
                    "sample": 0, "slot": 0}]
        result = diagnostic.analyze_debugger_initialization(
            "All breakpoints in use\n", commands, samples, 0x108000)
        self.assertIsNotNone(result)
        assert result is not None
        self.assertEqual(result["failure_class"], "SIMULATOR_OBSERVATION_LIMITATION")
        self.assertEqual(result["breakpoint_set_commands"], 16)
        self.assertEqual(result["unique_breakpoint_targets"], 16)
        self.assertEqual(result["guest_instructions_executed"], 0)
        self.assertEqual(result["failure_frontier"],
                         "ELF entry before first guest instruction")
        self.assertIsNone(diagnostic.analyze_debugger_initialization(
            "", commands, samples, 0x108000))


class InstalledBreakpointListTests(unittest.TestCase):
    def test_bl_output_captures_six_real_slot_numbers_modes_and_addresses(self) -> None:
        text = """   #    Address                   Command
   0  P 000000000010aec0  IA-64
   1  P 000000000010aed0  IA-64
   2  P 000000000010dcb4  IA-64
   3  P 000000000010b250  IA-64
   4  P 000000000010c850  IA-64
   5  P 000000000010b930  IA-64
"""
        installed = diagnostic.parse_installed_program_breakpoints(text)
        self.assertEqual([item["number"] for item in installed], list(range(6)))
        self.assertEqual([item["address"] for item in installed], [
            0x10AEC0, 0x10AED0, 0x10DCB4, 0x10B250, 0x10C850, 0x10B930,
        ])
        self.assertTrue(all(item["address_mode"] == "physical" and
                            item["address_exact"] and
                            item["architecture"] == "IA-64" for item in installed))
        comparison = diagnostic.compare_installed_program_breakpoints(
            installed,
            [0x10AEC0, 0x10AED0, 0x10DCB4, 0x10B250, 0x10C850, 0x10B930],
            6)
        self.assertEqual(comparison, {
            "count_matches": True,
            "slots_match": True,
            "addresses_match": True,
            "matches_plan": True,
        })

    def test_bl_output_preserves_symbolized_truncated_locations(self) -> None:
        text = """   #    Address                   Command
   0  P ia64_coop_b_fir* IA-64
   1  P ia64_coop_b_bsp* IA-64
   2  P ia64_coop_conte* IA-64
   3  P ia64_coop_a_res* IA-64
   4  P ia64_coop_b_res* IA-64
   5  P ia64_coop_a_res* IA-64
"""
        installed = diagnostic.parse_installed_program_breakpoints(text)
        self.assertEqual([item["number"] for item in installed], list(range(6)))
        self.assertEqual([item["location"] for item in installed], [
            "ia64_coop_b_fir*", "ia64_coop_b_bsp*", "ia64_coop_conte*",
            "ia64_coop_a_res*", "ia64_coop_b_res*", "ia64_coop_a_res*",
        ])
        self.assertTrue(all(item["address"] is None and not item["address_exact"]
                            for item in installed))
        comparison = diagnostic.compare_installed_program_breakpoints(
            installed, [0x10AEC0, 0x10AED0, 0x10DCB4,
                        0x10B250, 0x10C850, 0x10B930], 6)
        self.assertEqual(comparison, {
            "count_matches": True,
            "slots_match": True,
            "addresses_match": None,
            "matches_plan": True,
        })

    def test_non_program_breakpoint_output_is_not_counted(self) -> None:
        self.assertEqual(diagnostic.parse_installed_program_breakpoints(
            "No breakpoints set\nData breakpoint #0 at 0x112200\n"), [])


class CommandFileContractTests(unittest.TestCase):
    @staticmethod
    def symbols() -> dict[str, object]:
        return {
            "ivt": 0x100000,
            "text": [0x108000, 0x10E200],
            "context_a": 0x112600,
            "context_b": 0x112200,
            "probe_record": 0x112000,
            "probe_record_size": 1632,
            "switch_entry": 0x10CFC0,
            "context_b_first_entry": 0x10AEC0,
            "frontiers": {
                "setup_entry": 0x10A2D0,
                "setup_call": 0x10A2F8,
                "setup_nested_return": 0x10A300,
                "setup_exit": 0x10AB78,
                "setup_caller_return": 0x10AE30,
                "switch_entry": 0x10CFC0,
                "context_a_save": 0x10DCB4,
                "rbs_handoff": 0x10DE44,
                "context_b_first": 0x10AEC0,
                "bsp_bounds_valid": 0x10AED0,
                "context_a_resume_first": 0x10B250,
                "context_b_resume_second": 0x10C850,
                "context_a_resume_final": 0x10B930,
                "context_a_final_exit": 0x10C060,
                "context_b_unexpected_resume": 0x10CF50,
                "switch_return": 0x10E1C8,
            },
        }

    def test_six_frontier_breakpoints_pass_and_capture_the_installed_list(self) -> None:
        symbols = self.symbols()
        frontiers = symbols["frontiers"]
        selected = diagnostic.cooperative_breakpoints(frontiers)
        self.assertEqual([label for _, label in selected], [
            "Context B first-entry bootstrap", "Context B BSP bounds passed",
            "context-switch flushrs", "Context A first resumption",
            "Context B second resumption", "Context A final resumption",
        ])
        text = diagnostic.make_command_file(Path("/tmp/coop-debug"), symbols)
        plan = diagnostic.validate_command_file(
            text, RANGES, selected)
        self.assertEqual(plan["breakpoint_set_commands"], 6)
        self.assertEqual(plan["maximum_simultaneous_programmed"], 6)
        self.assertEqual(plan["unused_slots_at_peak"], 4)
        self.assertEqual(sum(line.startswith("bs ") for line in text.splitlines()), 6)
        self.assertIn("bl\nstep 64", text)
        self.assertIn("eval iva psr rsc bsp bspst rnat unat pfs sp gp b0 r14 r15 "
                      "r32 r33 r42 r43 iip ipsr isr ifs iipa", text)
        self.assertEqual(text.splitlines()[-1], "quit")


class RuntimeSwitchFrontierAnalysisTests(unittest.TestCase):
    def setUp(self) -> None:
        frontiers = CommandFileContractTests.symbols()["frontiers"]
        self.breakpoints = diagnostic.cooperative_breakpoints(frontiers)
        self.expected_bsp = 0x160060

    @staticmethod
    def symbols() -> dict[str, object]:
        return CommandFileContractTests.symbols()

    def evidence(self) -> tuple[list[dict[str, int]], list[dict[str, object]],
                                list[dict[str, object]]]:
        stages = (
            ("context-switch flushrs", 0x10DCB4, 10, 1),
            ("Context B first-entry bootstrap", 0x10AEC0, 20, 1),
            ("Context B BSP bounds passed", 0x10AED0, 30, 2),
            ("context-switch flushrs", 0x10DCB4, 40, 3),
            ("Context A first resumption", 0x10B250, 50, 4),
            ("context-switch flushrs", 0x10DCB4, 60, 5),
            ("Context B second resumption", 0x10C850, 70, 6),
            ("context-switch flushrs", 0x10DCB4, 80, 7),
            ("Context A final resumption", 0x10B930, 90, 8),
        )
        samples = [
            {"sample": index, "instructions": instructions, "ip": address,
             "iip": address & ~0xF, "slot": (address & 0xF) // 4}
            for index, (_, address, instructions, _) in enumerate(stages, start=1)
        ]
        registers = [
            {"instructions": instructions, "registers": {
                "rsc": 3,
                "bsp": self.expected_bsp if label == "Context B BSP bounds passed" else 0,
                "r14": self.expected_bsp if label == "Context B BSP bounds passed" else 0,
                "r15": self.expected_bsp if label == "Context B BSP bounds passed" else 0,
            }}
            for label, _, instructions, _ in stages
        ]
        snapshots = [
            {"sample": index, "probe_record": {"event_count": event_count}}
            for index, (_, _, _, event_count) in enumerate(stages, start=1)
        ]
        return samples, registers, snapshots

    def test_all_six_frontiers_and_four_switch_events_verify_full_sequence(self) -> None:
        samples, registers, snapshots = self.evidence()
        result = diagnostic.analyze_cooperative_switch_frontiers(
            samples, registers, snapshots, self.breakpoints, self.expected_bsp,
            (0x100000, 0x108000))
        self.assertTrue(result["context_b_bsp_check_valid"])
        self.assertTrue(result["context_b_to_a_first_observed"])
        self.assertEqual(result["flushrs_event_counts"], [1, 3, 5, 7])
        self.assertTrue(result["full_switch_sequence_verified"])

    def test_bad_bsp_value_blocks_frontier_and_full_qualification(self) -> None:
        samples, registers, snapshots = self.evidence()
        registers[2]["registers"]["r14"] += 8
        result = diagnostic.analyze_cooperative_switch_frontiers(
            samples, registers, snapshots, self.breakpoints, self.expected_bsp,
            (0x100000, 0x108000))
        self.assertFalse(result["context_b_bsp_check_valid"])
        self.assertFalse(result["context_b_to_a_first_observed"])
        self.assertFalse(result["full_switch_sequence_verified"])

    def test_first_ivt_checkpoint_retains_saved_exception_state(self) -> None:
        sample = {"sample": 1, "instructions": 50, "ip": 0x105400,
                  "iip": 0x105400, "slot": 0}
        registers = [{"instructions": 50, "registers": {
            "iip": 0x10C150, "ipsr": 0x2010, "isr": 0,
            "ifs": 0x55, "iipa": 0x10C140, "iva": 0x100000,
            "rsc": 3, "bsp": 0x160060, "bspst": 0x160060,
            "rnat": 0, "sp": 0x12F000, "gp": 0,
        }}]
        result = diagnostic.analyze_cooperative_switch_frontiers(
            [sample], registers, [], self.breakpoints, self.expected_bsp,
            (0x100000, 0x108000), (0x112600, 0x112200))
        exception = result["first_unexpected_exception"]
        self.assertIsNotNone(exception)
        self.assertEqual(exception["saved_iip"], 0x10C150)
        self.assertEqual(exception["saved_slot"], 0)
        self.assertEqual(exception["faulting_instruction_address"], 0x10C150)
        self.assertEqual(exception["vector_offset"], 0x5400)
        self.assertEqual(exception["isr"], 0)
        self.assertEqual(exception["isr_classification"], "illegal-operation")
        self.assertEqual(exception["ifs"], 0x55)
        self.assertEqual(exception["context_a_pointer"], 0x112600)
        self.assertEqual(exception["context_b_pointer"], 0x112200)

    def test_fault_instruction_is_looked_up_in_the_bound_disassembly(self) -> None:
        text = "  10c150:\t00 00 00\t[MMI]       mov.m r14=ar.bspstore;;\n"
        self.assertIn("mov.m r14=ar.bspstore",
                      diagnostic.disassembly_instruction_at(text, 0x10C150))

    def test_actual_ia64_5ar5_sixteen_address_plan_fails_host_preflight(self) -> None:
        # Exact addresses from the preserved IA64-5AR5 command file; all 16
        # resolve to distinct Ski program-breakpoint slots.
        old_addresses = (
            0x1080C8, 0x10A2D0, 0x10A2F8, 0x10A300,
            0x10AB78, 0x10AE30, 0x10CFC0, 0x10DCB4,
            0x10DE44, 0x10AEC0, 0x10B250, 0x10C850,
            0x10B930, 0x10C060, 0x10CF50, 0x10E1C8,
        )
        original = diagnostic.collect_breakpoint_targets(
            "\n".join(f"bs 0x{address:x}" for address in old_addresses))
        self.assertEqual(original["breakpoint_set_commands"], 16)
        self.assertEqual(original["unique_breakpoint_targets"], 16)

        base = diagnostic.make_command_file(Path("/tmp/coop-debug"), self.symbols())
        lines = [line for line in base.splitlines() if not line.startswith("bs ")]
        insertion = lines.index("bl")
        lines[insertion:insertion] = [f"bs 0x{address:x}" for address in old_addresses]
        reproduced = "\n".join(lines) + "\n"
        with self.assertRaisesRegex(diagnostic.DiagnosticError, "limit of 6"):
            diagnostic.validate_command_file(reproduced, RANGES)

    def test_equivalent_address_alias_does_not_consume_another_slot(self) -> None:
        symbols = self.symbols()
        expected = diagnostic.cooperative_breakpoints(symbols["frontiers"])
        base = diagnostic.make_command_file(Path("/tmp/coop-debug"), symbols)
        aliased = base.replace("bs 0x10aec0\n", "bs 0x10aec0\nbs 0x10aec1\n", 1)
        plan = diagnostic.validate_command_file(aliased, RANGES, expected)
        self.assertEqual(plan["breakpoint_set_commands"], 7)
        self.assertEqual(plan["unique_breakpoint_targets"], 6)
        self.assertEqual(plan["duplicate_or_equivalent_addresses"], [
            {"command_address": 0x10AEC1, "resolved_address": 0x10AEC0}])

    def test_breakpoint_addresses_must_be_resolved_in_range_and_valid_slots(self) -> None:
        base = diagnostic.make_command_file(Path("/tmp/coop-debug"), self.symbols())
        invalid_cases = (
            ("bs 0x10aec0", "bs ia64_coop_b_first_entry"),
            ("bs 0x10aec0", "bs 0x10aecc"),
            ("bs 0x10aec0", "bs 0x200000"),
            ("bs 0x10aec0", "bs 0x10a2d0"),
        )
        messages = ("resolved numeric addresses", "illegal IA-64 slot", "outside", "frontiers")
        for (old, replacement), message in zip(invalid_cases, messages):
            with self.subTest(replacement=replacement), self.assertRaisesRegex(
                    diagnostic.DiagnosticError, message):
                diagnostic.validate_command_file(
                    base.replace(old, replacement, 1), RANGES,
                    diagnostic.cooperative_breakpoints(self.symbols()["frontiers"]))

    def test_command_file_rejects_unbounded_or_missing_quit(self) -> None:
        text = diagnostic.make_command_file(Path("/tmp/coop-debug"), self.symbols())
        with self.assertRaises(diagnostic.DiagnosticError):
            diagnostic.validate_command_file(text.replace("step 64", "run", 1))
        with self.assertRaises(diagnostic.DiagnosticError):
            diagnostic.validate_command_file(text.removesuffix("quit\n"))
        with self.assertRaisesRegex(diagnostic.DiagnosticError, "installed breakpoints"):
            diagnostic.validate_command_file(text.replace("bl\n", "", 1))
        with self.assertRaisesRegex(diagnostic.DiagnosticError, "dynamic breakpoint"):
            diagnostic.validate_command_file(text.replace("bl\n", "bd 0\nbl\n", 1))

    def test_only_supported_debugger_commands_and_expressions_are_accepted(self) -> None:
        text = diagnostic.make_command_file(Path("/tmp/coop-debug"), self.symbols())
        with self.assertRaisesRegex(diagnostic.DiagnosticError, "unsupported.*command"):
            diagnostic.validate_command_file(text.replace("bl\n", "xyz\nbl\n", 1))
        with self.assertRaisesRegex(diagnostic.DiagnosticError, "unresolved/unsupported"):
            diagnostic.validate_command_file(text.replace("eval ip+0 $insts$",
                                                          "eval unknown_symbol", 1))


if __name__ == "__main__":
    unittest.main()
