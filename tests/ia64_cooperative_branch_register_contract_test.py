#!/usr/bin/env python3
"""Positive and mutation fixtures for the cooperative BR1-BR5 contract."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIR = ROOT / "tests" / "ia64_cooperative_context_switch"
CONTRACT_PATH = ROOT / "scripts" / "ia64_coop_branch_register_contract.py"
spec = importlib.util.spec_from_file_location("ia64_coop_br_contract", CONTRACT_PATH)
assert spec is not None and spec.loader is not None
contract = importlib.util.module_from_spec(spec)
spec.loader.exec_module(contract)

# Independent fixture oracle. It intentionally does not import the validator's
# EXPECTED values, so changing source and validator together cannot bless drift.
FIXTURE_VALUES = {
    "A": [0x4252314141413031, 0x4252324141413031, 0x4252334141413031,
          0x4252344141413031, 0x4252354141413031],
    "B": [0x4252314242423032, 0x4252324242423032, 0x4252334242423032,
          0x4252344242423032, 0x4252354242423032],
}
FIXTURE_SLOTS = [160, 168, 176, 184, 192]


def replace_once(text: str, old: str, new: str) -> str:
    if old not in text:
        raise AssertionError(f"fixture token is missing: {old!r}")
    return text.replace(old, new, 1)


def disassembly_fixture() -> str:
    lines: list[str] = []
    address = 0x1000

    def symbol(name: str) -> None:
        lines.append(f"{address:016x} <{name}>:")

    def op(instruction: str) -> None:
        nonlocal address
        lines.append(f"  {address:016x}:\t00 00 00 00 00 00\t{instruction}")
        address += 0x10

    symbol("ia64_coop_probe_setup")
    for value, slot in zip(FIXTURE_VALUES["B"], FIXTURE_SLOTS):
        op(f"movl r15=0x{value:x}")
        op(f"adds r28={slot},r14")
        op("st8 [r28]=r15")

    symbol("ia64_coop_a_test")
    for reg, value in enumerate(FIXTURE_VALUES["A"], start=1):
        op(f"movl r14=0x{value:x}")
        op(f"mov b{reg}=r14")
    symbol("ia64_coop_a_resumed_first")
    for reg, value in enumerate(FIXTURE_VALUES["A"], start=1):
        op(f"mov r14=b{reg}")
        op(f"movl r15=0x{value:x}")
        op("cmp.eq p6,p7=r14,r15")
    symbol("ia64_coop_b_test")
    for reg, value in enumerate(FIXTURE_VALUES["B"], start=1):
        op(f"mov r14=b{reg}")
        op(f"movl r15=0x{value:x}")
        op("cmp.eq p6,p7=r14,r15")
    symbol("ia64_coop_b_resumed")
    symbol("ia64_coop_context_switch")
    op("mov r23=b0")
    op("adds r28=64,r14")
    op("st8 [r28]=r23")
    for reg, slot in enumerate(FIXTURE_SLOTS, start=1):
        op(f"mov r27=b{reg}")
        op(f"adds r28={slot},r14")
        op("st8 [r28]=r27")
    for reg, slot in enumerate(FIXTURE_SLOTS, start=1):
        op(f"adds r28={slot},r15")
        op("ld8 r20=[r28]")
        op(f"mov b{reg}=r20")
    op("adds r28=64,r15")
    op("ld8 r20=[r28]")
    op("mov b0=r20")
    symbol("ia64_coop_switch_fail")
    return "\n".join(lines) + "\n"


class CooperativeBranchRegisterContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.sequence = (SOURCE_DIR / "probe_sequence.s").read_text(encoding="utf-8")
        cls.switch = (SOURCE_DIR / "context_switch.s").read_text(encoding="utf-8")
        cls.context = (SOURCE_DIR / "context.h").read_text(encoding="utf-8")
        cls.disassembly = disassembly_fixture()

    def validate(self, sequence: str | None = None, switch: str | None = None,
                 disassembly: str | None = None) -> dict:
        return contract.validate_branch_register_contract(
            self.sequence if sequence is None else sequence,
            self.switch if switch is None else switch,
            self.context,
            self.disassembly if disassembly is None else disassembly,
        )

    def assert_rejected(self, **kwargs) -> None:
        with self.assertRaises(contract.BranchRegisterContractError):
            self.validate(**kwargs)

    def test_positive_fixture_proves_each_register_assignment_and_restore(self) -> None:
        result = self.validate()
        source = result["source_contract"]
        target = result["target_disassembly"]
        self.assertEqual([source["context_a_initial"][f"b{reg}"] for reg in range(1, 6)],
                         FIXTURE_VALUES["A"])
        self.assertEqual([source["context_b_initial"][f"b{reg}"] for reg in range(1, 6)],
                         FIXTURE_VALUES["B"])
        self.assertEqual(target["context_a_initial"],
                         {f"b{reg}": value for reg, value in enumerate(FIXTURE_VALUES["A"], 1)})
        self.assertEqual(target["context_b_initial"],
                         {f"b{reg}": value for reg, value in enumerate(FIXTURE_VALUES["B"], 1)})
        self.assertEqual(target["saved_branch_registers"],
                         {f"b{reg}": slot for reg, slot in enumerate(FIXTURE_SLOTS, 1)})
        self.assertEqual(target["restored_branch_registers"],
                         {f"b{reg}": slot for reg, slot in enumerate(FIXTURE_SLOTS, 1)})
        self.assertEqual(len(set(FIXTURE_VALUES["A"] + FIXTURE_VALUES["B"])), 10)

    def test_br1_b_written_to_c_br2_is_rejected(self) -> None:
        sequence = replace_once(
            self.sequence,
            "movl r15 = BR2_B_INITIAL ;;\n    SET64 r14, C_BR2, r15",
            "movl r15 = BR1_B_INITIAL ;;\n    SET64 r14, C_BR2, r15",
        )
        self.assert_rejected(sequence=sequence)

    def test_br2_b_written_to_c_br1_is_rejected(self) -> None:
        sequence = replace_once(
            self.sequence,
            "movl r15 = BR1_B_INITIAL ;;\n    SET64 r14, C_BR1, r15",
            "movl r15 = BR2_B_INITIAL ;;\n    SET64 r14, C_BR1, r15",
        )
        self.assert_rejected(sequence=sequence)

    def test_duplicate_branch_register_sentinels_are_rejected(self) -> None:
        sequence = replace_once(
            self.sequence, ".equ BR2_B_INITIAL, 0x4252324242423032",
            ".equ BR2_B_INITIAL, 0x4252314242423032")
        self.assert_rejected(sequence=sequence)

    def test_incorrect_branch_register_slot_offset_is_rejected(self) -> None:
        sequence = replace_once(self.sequence, ".equ C_BR2, 168", ".equ C_BR2, 176")
        self.assert_rejected(sequence=sequence)

    def test_source_and_target_disassembly_mismatch_is_rejected(self) -> None:
        disassembly = replace_once(
            self.disassembly, "movl r15=0x4252324242423032",
            "movl r15=0x4252314242423032")
        self.assert_rejected(disassembly=disassembly)

    def test_assertions_changed_to_match_bad_initialization_are_rejected(self) -> None:
        sequence = replace_once(
            self.sequence,
            "CHECK_PRESERVED_REGS GR4_B, GR5_B, GR6_B, GR7_B, BR1_B, BR2_B, BR3_B, BR4_B, BR5_B",
            "CHECK_PRESERVED_REGS GR4_B, GR5_B, GR6_B, GR7_B, BR1_B, BR1_B, BR3_B, BR4_B, BR5_B",
        )
        self.assert_rejected(sequence=sequence)

    def test_context_a_sentinel_loaded_into_context_b_is_rejected(self) -> None:
        sequence = replace_once(
            self.sequence,
            "movl r15 = BR2_B_INITIAL ;;\n    SET64 r14, C_BR2, r15",
            "movl r15 = BR2_A_INITIAL ;;\n    SET64 r14, C_BR2, r15",
        )
        self.assert_rejected(sequence=sequence)

    def test_correct_initial_state_with_corrupted_b2_restore_is_rejected(self) -> None:
        disassembly = replace_once(self.disassembly, "mov b2=r20", "mov b3=r20")
        self.assert_rejected(disassembly=disassembly)


if __name__ == "__main__":
    unittest.main()
