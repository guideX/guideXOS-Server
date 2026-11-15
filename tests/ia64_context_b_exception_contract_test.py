#!/usr/bin/env python3
"""Negative fixtures for the one-run Context B exception diagnostic."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "diagnose-ia64-context-b-exception.py"
spec = importlib.util.spec_from_file_location("ia64_context_b_exception_diagnostic", SCRIPT)
assert spec is not None and spec.loader is not None
diagnostic = importlib.util.module_from_spec(spec)
spec.loader.exec_module(diagnostic)


IVA = 0x100000
TEXT = (0x108000, 0x10E1D0)
ARTIFACT = "a" * 64
SIMULATOR = "b" * 64
RUN_STARTED_NS = 1000


def valid_exception_snapshot() -> dict[str, int]:
    return {
        "current_ip": IVA + diagnostic.VECTOR_OFFSET,
        "iva": IVA,
        "psr": 0x10,
        "rsc": 3,
        "bsp": 0x143400,
        "bspstore": 0x143400,
        "bspst": 0x143400,
        "sp": 0x131C10,
        "pfs": 0,
        "b0": 0x10AF00,
        "iip": 0x10C070,
        "ipsr": 0x2010,
        "isr": 0,
        "ifs": 0,
        "iipa": 0x10C060,
    }


def valid_provenance(**updates: object) -> dict[str, object]:
    record: dict[str, object] = {
        "snapshot_id": "general-exception-entry",
        "path": "/capture/registers/05-ge-entry.txt",
        "mtime_ns": RUN_STARTED_NS,
        "sha256": "c" * 64,
        "artifact_sha256": ARTIFACT,
        "simulator_sha256": SIMULATOR,
        "context_a_id": 0xA,
        "context_b_id": 0xB,
    }
    record.update(updates)
    return record


def valid_plan() -> tuple[Path, list[dict[str, object]], dict[str, int], dict[str, object]]:
    root = Path(tempfile.gettempdir()) / "ia64-5ar7-contract-capture"
    addresses = {
        "ivt": IVA,
        "general_exception": IVA + 0x5400,
        "pause_loop": IVA + 0x5410,
        "context_b_first": 0x10AEC0,
        "context_b_call_boundary": 0x10AEF0,
        "context_b_test": 0x10C070,
        "test_fail": 0x10CF60,
        "context_a": 0x112600,
        "context_b": 0x112200,
        "probe_record": 0x112000,
    }
    targets = [
        {"address": addresses["context_b_first"], "label": "Context B first instruction"},
        {"address": addresses["context_b_call_boundary"], "label": "Context B bootstrap call boundary"},
        {"address": addresses["context_b_test"], "label": "Context B test entry"},
        {"address": addresses["general_exception"], "label": "General Exception entry"},
        {"address": addresses["pause_loop"], "label": "IVT pause loop"},
        {"address": addresses["test_fail"], "label": "test fail fallback"},
    ]
    symbols: dict[str, object] = {
        "ivt": IVA,
        "text": TEXT,
        "load_ranges": ((IVA, IVA + 0x8000), (0x108000, 0x10E1D0),
                        (0x110000, 0x153428)),
    }
    return root, targets, addresses, symbols


class InterruptionSnapshotTests(unittest.TestCase):
    def test_valid_general_exception_snapshot_decodes_bundle_and_slot(self) -> None:
        result = diagnostic.validate_interruption_snapshot(
            valid_exception_snapshot(), expected_iva=IVA, text_range=TEXT)
        self.assertEqual(result["fault_bundle"], 0x10C070)
        self.assertEqual(result["fault_slot"], 0)
        self.assertEqual(result["classification"], "illegal-operation")
        self.assertEqual(result["bspstore_debugger_value"], 0x143400)
        self.assertFalse(result["ifs_ifm_interpreted"])

    def test_wrong_general_exception_vector_is_rejected(self) -> None:
        snapshot = valid_exception_snapshot()
        snapshot["current_ip"] = IVA + diagnostic.PAUSE_OFFSET
        with self.assertRaisesRegex(diagnostic.DiagnosticError, "not at the General Exception"):
            diagnostic.validate_interruption_snapshot(
                snapshot, expected_iva=IVA, text_range=TEXT)

    def test_wrong_cr_iva_is_rejected(self) -> None:
        snapshot = valid_exception_snapshot()
        snapshot["iva"] = IVA + 0x8000
        with self.assertRaisesRegex(diagnostic.DiagnosticError, "CR.IVA"):
            diagnostic.validate_interruption_snapshot(
                snapshot, expected_iva=IVA, text_range=TEXT)

    def test_saved_instruction_slot_mismatch_is_rejected(self) -> None:
        snapshot = valid_exception_snapshot()
        snapshot["ipsr"] |= 1 << 41
        with self.assertRaisesRegex(diagnostic.DiagnosticError, "same valid slot"):
            diagnostic.validate_interruption_snapshot(
                snapshot, expected_iva=IVA, text_range=TEXT)

    def test_invalid_isr_interpretation_is_rejected(self) -> None:
        with self.assertRaisesRegex(diagnostic.DiagnosticError, "means privileged-operation"):
            diagnostic.interpret_isr(0x10, "illegal-operation")

    def test_unsupported_isr_class_is_rejected(self) -> None:
        with self.assertRaisesRegex(diagnostic.DiagnosticError, "unsupported General Exception"):
            diagnostic.interpret_isr(0x50)

    def test_missing_saved_interruption_state_is_rejected(self) -> None:
        snapshot = valid_exception_snapshot()
        del snapshot["ipsr"]
        with self.assertRaisesRegex(diagnostic.DiagnosticError, "missing: ipsr"):
            diagnostic.validate_interruption_snapshot(
                snapshot, expected_iva=IVA, text_range=TEXT)

    def test_first_exact_vector_checkpoint_is_used(self) -> None:
        later = valid_exception_snapshot()
        later["ipsr"] = 0
        analysis, checkpoint, error = diagnostic.analyze_first_vector_checkpoint(
            [
                {"snapshot_id": "first-vector-entry", "current_ip": IVA + 0x5400,
                 "registers": valid_exception_snapshot()},
                {"snapshot_id": "later-vector-entry", "current_ip": IVA + 0x5400,
                 "registers": later},
            ], vector_entry=IVA + 0x5400, expected_iva=IVA, text_range=TEXT)
        self.assertIsNone(error)
        self.assertEqual(checkpoint, "first-vector-entry")
        self.assertIsNotNone(analysis)

    def test_checkpoint_after_vector_entry_cannot_claim_exception_capture(self) -> None:
        analysis, checkpoint, error = diagnostic.analyze_first_vector_checkpoint(
            [{"snapshot_id": "pause-loop", "current_ip": IVA + 0x5410,
              "registers": valid_exception_snapshot()}],
            vector_entry=IVA + 0x5400, expected_iva=IVA, text_range=TEXT)
        self.assertIsNone(analysis)
        self.assertIsNone(checkpoint)
        self.assertIn("exactly at the General Exception", error or "")


class SnapshotProvenanceTests(unittest.TestCase):
    def test_register_dump_hash_is_independent_of_probe_elf_hash(self) -> None:
        diagnostic.validate_snapshot_provenance(
            [valid_provenance()], run_started_ns=RUN_STARTED_NS,
            artifact_sha256=ARTIFACT, simulator_sha256=SIMULATOR)

    def test_stale_snapshot_is_rejected(self) -> None:
        with self.assertRaisesRegex(diagnostic.DiagnosticError, "stale"):
            diagnostic.validate_snapshot_provenance(
                [valid_provenance(mtime_ns=RUN_STARTED_NS - 1)],
                run_started_ns=RUN_STARTED_NS,
                artifact_sha256=ARTIFACT, simulator_sha256=SIMULATOR)

    def test_duplicate_snapshot_id_or_path_is_rejected(self) -> None:
        first = valid_provenance()
        second = valid_provenance(path="/capture/registers/06-copy.txt")
        with self.assertRaisesRegex(diagnostic.DiagnosticError, "duplicate"):
            diagnostic.validate_snapshot_provenance(
                [first, second], run_started_ns=RUN_STARTED_NS,
                artifact_sha256=ARTIFACT, simulator_sha256=SIMULATOR)

    def test_wrong_context_identity_is_rejected(self) -> None:
        with self.assertRaisesRegex(diagnostic.DiagnosticError, "context identity"):
            diagnostic.validate_snapshot_provenance(
                [valid_provenance(context_b_id=0xA)], run_started_ns=RUN_STARTED_NS,
                artifact_sha256=ARTIFACT, simulator_sha256=SIMULATOR)

    def test_mismatched_artifact_identity_is_rejected(self) -> None:
        with self.assertRaisesRegex(diagnostic.DiagnosticError, "artifact identity"):
            diagnostic.validate_snapshot_provenance(
                [valid_provenance(artifact_sha256="d" * 64)],
                run_started_ns=RUN_STARTED_NS,
                artifact_sha256=ARTIFACT, simulator_sha256=SIMULATOR)

    def test_malformed_snapshot_file_hash_is_rejected(self) -> None:
        with self.assertRaisesRegex(diagnostic.DiagnosticError, "file SHA-256"):
            diagnostic.validate_snapshot_provenance(
                [valid_provenance(sha256="not-a-sha256")],
                run_started_ns=RUN_STARTED_NS,
                artifact_sha256=ARTIFACT, simulator_sha256=SIMULATOR)

    def test_mismatched_simulator_identity_is_rejected(self) -> None:
        with self.assertRaisesRegex(diagnostic.DiagnosticError, "simulator identity"):
            diagnostic.validate_snapshot_provenance(
                [valid_provenance(simulator_sha256="d" * 64)],
                run_started_ns=RUN_STARTED_NS,
                artifact_sha256=ARTIFACT, simulator_sha256=SIMULATOR)


class DebuggerPlanTests(unittest.TestCase):
    def command_file(self) -> tuple[str, Path, list[dict[str, object]],
                                    tuple[tuple[int, int], ...], tuple[tuple[int, int], ...]]:
        root, targets, addresses, symbols = valid_plan()
        text = diagnostic.make_command_file(root, addresses, targets, symbols)
        valid_ranges = ((IVA, IVA + 0x8000), TEXT)
        load_ranges = tuple(symbols["load_ranges"])
        return text, root, targets, valid_ranges, load_ranges

    def test_six_verified_breakpoints_pass(self) -> None:
        text, root, targets, valid_ranges, load_ranges = self.command_file()
        plan = diagnostic.validate_command_file(text, root, targets, valid_ranges, load_ranges)
        self.assertEqual(plan["unique_breakpoint_targets"], 6)
        self.assertTrue(plan["bounded_execution"])
        self.assertEqual(plan["maximum_guest_instructions"], 136677)

    def test_unsupported_debugger_command_is_rejected(self) -> None:
        text, root, targets, valid_ranges, load_ranges = self.command_file()
        with self.assertRaisesRegex(diagnostic.DiagnosticError, "unsupported.*command"):
            diagnostic.validate_command_file(
                text.replace("bl\n", "cont\nbl\n", 1), root, targets,
                valid_ranges, load_ranges)

    def test_excess_breakpoint_allocation_is_rejected(self) -> None:
        text, root, targets, valid_ranges, load_ranges = self.command_file()
        text = text.replace("bl\n", "bs 0x10c080\nbl\n", 1)
        with self.assertRaisesRegex(diagnostic.DiagnosticError, "six-target"):
            diagnostic.validate_command_file(
                text, root, targets, valid_ranges, load_ranges)


if __name__ == "__main__":
    unittest.main()
