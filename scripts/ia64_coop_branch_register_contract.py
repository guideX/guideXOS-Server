"""Independent source and target-disassembly checks for cooperative BR1-BR5."""

from __future__ import annotations

import re
from typing import Any


class BranchRegisterContractError(ValueError):
    pass


# These are deliberate, independent oracle values. Do not derive them from the
# assembly source being validated.
EXPECTED = {
    "A": {
        1: 0x4252314141413031,
        2: 0x4252324141413031,
        3: 0x4252334141413031,
        4: 0x4252344141413031,
        5: 0x4252354141413031,
    },
    "B": {
        1: 0x4252314242423032,
        2: 0x4252324242423032,
        3: 0x4252334242423032,
        4: 0x4252344242423032,
        5: 0x4252354242423032,
    },
}
SLOTS = {1: 160, 2: 168, 3: 176, 4: 184, 5: 192}


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise BranchRegisterContractError(message)


def _equs(source: str, label: str) -> dict[str, int]:
    result: dict[str, int] = {}
    for name, value in re.findall(
            r"(?mi)^\s*\.equ\s+([A-Za-z_][A-Za-z0-9_]*)\s*,\s*([^\s,]+)", source):
        try:
            parsed = int(value, 0)
        except ValueError:
            continue
        _require(name not in result, f"{label} defines .equ {name} more than once")
        result[name] = parsed
    return result


def _procedure(source: str, name: str, label: str) -> str:
    match = re.search(
        rf"(?ms)^\.proc\s+{re.escape(name)}\s*\n(.*?)^\.endp\s+{re.escape(name)}\s*$",
        source,
    )
    _require(match is not None, f"{label} lacks bounded .proc {name}")
    assert match is not None
    return match.group(1)


def _check_source(sequence: str, switch: str, context: str) -> dict[str, Any]:
    sequence_equ = _equs(sequence, "probe sequence")
    switch_equ = _equs(switch, "context switch")

    for reg, offset in SLOTS.items():
        name = f"C_BR{reg}"
        _require(sequence_equ.get(name) == offset,
                 f"probe sequence {name} must have offset {offset}")
        _require(switch_equ.get(name) == offset,
                 f"context switch {name} must have offset {offset}")
    _require(sequence_equ.get("C_RP") == 64 and switch_equ.get("C_RP") == 64,
             "b0 return-link storage must remain in C_RP at offset 64")
    _require(re.search(r"(?m)^\s*uint64_t\s+br1_to_br5\[5\]\s*;", context) is not None,
             "Context must retain its five-element branch-register array")
    for reg, offset in SLOTS.items():
        array_index = reg - 1
        assertion = (r"offsetof\(Context,\s*br1_to_br5\)\s*==\s*160"
                     if reg == 1 else
                     rf"offsetof\(Context,\s*br1_to_br5\)\s*\+\s*{array_index}\s*\*\s*"
                     rf"sizeof\(uint64_t\)\s*==\s*{offset}")
        _require(re.search(assertion, context) is not None,
            f"Context branch-register array element {reg} must have offset {offset}")

    for identity in ("A", "B"):
        init_symbols: set[int] = set()
        expected_symbols: set[int] = set()
        for reg, value in EXPECTED[identity].items():
            expected_name = f"BR{reg}_{identity}"
            init_name = f"BR{reg}_{identity}_INITIAL"
            _require(sequence_equ.get(expected_name) == value,
                     f"independent assertion sentinel {expected_name} is incorrect")
            _require(sequence_equ.get(init_name) == value,
                     f"initialization sentinel {init_name} is incorrect")
            _require(expected_name != init_name,
                     f"{identity} BR{reg} initialization and assertion must be separately named")
            init_symbols.add(sequence_equ[init_name])
            expected_symbols.add(sequence_equ[expected_name])
        _require(len(init_symbols) == 5 and len(expected_symbols) == 5,
                 f"Context {identity} branch-register sentinels must be unique")

    setup = _procedure(sequence, "ia64_coop_probe_setup", "probe sequence")
    a_test = _procedure(sequence, "ia64_coop_a_test", "probe sequence")
    b_test = _procedure(sequence, "ia64_coop_b_test", "probe sequence")
    for reg in range(1, 6):
        init_b = rf"movl\s+r15\s*=\s*BR{reg}_B_INITIAL\s*;;\s*" \
            rf"SET64\s+r14,\s*C_BR{reg},\s*r15"
        _require(len(re.findall(init_b, setup)) == 1,
                 f"Context B BR{reg} must be initialized exactly once in C_BR{reg}")
        init_a = rf"movl\s+r14\s*=\s*BR{reg}_A_INITIAL\s*;;\s*mov\s+b{reg}\s*=\s*r14"
        _require(len(re.findall(init_a, a_test)) == 1,
                 f"Context A BR{reg} must be initialized directly and independently")
        _require(a_test.find(f"BR{reg}_A_INITIAL") <
                 a_test.find("br.call.sptk.many b0 = ia64_coop_context_switch"),
                 f"Context A BR{reg} must be initialized before the first cooperative transfer")

    calls = re.findall(r"(?m)^\s*CHECK_PRESERVED_REGS\s+([^\n]+)$", sequence)
    branch_groups: list[tuple[str, tuple[str, ...]]] = []
    for call in calls:
        arguments = [part.strip() for part in call.split(",")]
        if len(arguments) == 9:
            branch_groups.append((call, tuple(arguments[4:9])))
    a_expected_group = tuple(f"BR{reg}_A" for reg in range(1, 6))
    b_expected_group = tuple(f"BR{reg}_B" for reg in range(1, 6))
    _require(len([group for _, group in branch_groups if group[0].endswith("_A")]) == 2 and
             all(group == a_expected_group for _, group in branch_groups
                 if group[0].endswith("_A")),
             "every Context A assertion must use its independent BR1_A-BR5_A expectations")
    _require(len([group for _, group in branch_groups if group[0].endswith("_B")]) == 2 and
             all(group == b_expected_group for _, group in branch_groups
                 if group[0].endswith("_B")),
             "every Context B assertion must use its independent BR1_B-BR5_B expectations")

    for reg, offset in SLOTS.items():
        _require(re.search(rf"mov\s+r27\s*=\s*b{reg}\s*;;\s*"
                           rf"STORE64\s+C_BR{reg},\s*r27", switch) is not None,
                 f"switch must save b{reg} to its C_BR{reg} slot")
        _require(re.search(rf"LOAD64\s+C_BR{reg},\s*r28,\s*r20\s*\n\s*"
                           rf"mov\s+b{reg}\s*=\s*r20", switch) is not None,
                 f"switch must restore b{reg} from its C_BR{reg} slot")
    _require(re.search(r"mov\s+r23\s*=\s*b0\s*;;\s*.*?STORE64\s+C_RP,\s*r23",
                       switch, re.DOTALL) is not None and
             re.search(r"LOAD64\s+C_RP,\s*r28,\s*r20\s*\n\s*mov\s+b0\s*=\s*r20",
                       switch) is not None,
             "b0 return-link save/restore must remain separate in C_RP")

    _require(sequence.find("ia64_coop_probe_setup") < sequence.find("ia64_coop_a_test") and
             "br.call.sptk.many b0 = ia64_coop_context_switch" in a_test,
             "branch-register initialization must precede the first cooperative transfer")
    return {
        "context_a_initial": {f"b{reg}": value for reg, value in EXPECTED["A"].items()},
        "context_b_initial": {f"b{reg}": value for reg, value in EXPECTED["B"].items()},
        "context_a_expected": {f"b{reg}": value for reg, value in EXPECTED["A"].items()},
        "context_b_expected": {f"b{reg}": value for reg, value in EXPECTED["B"].items()},
        "slot_offsets": {f"b{reg}": offset for reg, offset in SLOTS.items()},
        "context_a_assertions": [list(group) for _, group in branch_groups
                                 if group == tuple(f"BR{reg}_A" for reg in range(1, 6))],
        "context_b_assertions": [list(group) for _, group in branch_groups
                                 if group == tuple(f"BR{reg}_B" for reg in range(1, 6))],
        "b0_return_link_offset": 64,
    }


def _parse_disassembly(disassembly: str) -> tuple[dict[str, int], list[tuple[int, str]]]:
    symbols: dict[str, int] = {}
    instructions: list[tuple[int, str]] = []
    for line in disassembly.splitlines():
        symbol_match = re.match(r"^\s*([0-9a-fA-F]+)\s+<([^>]+)>:\s*$", line)
        if symbol_match:
            symbols[symbol_match.group(2)] = int(symbol_match.group(1), 16)
            continue
        address_match = re.match(r"^\s*([0-9a-fA-F]+):", line)
        if address_match is None:
            continue
        pieces = line.split("\t")
        instruction = pieces[-1].strip()
        if not pieces or not instruction:
            continue
        instruction = re.sub(r"^\[[A-Za-z0-9]+\]\s*", "", instruction)
        instruction = re.sub(r";+$", "", instruction).strip().lower()
        if re.match(r"^[0-9a-f]{2,16}(?:\s|$)", instruction):
            continue
        instructions.append((int(address_match.group(1), 16), instruction))
    return symbols, instructions


def _range(symbols: dict[str, int], instructions: list[tuple[int, str]],
           start: str, end: str, label: str) -> list[str]:
    _require(start in symbols and end in symbols,
             f"target disassembly lacks {label} symbol boundaries {start} and {end}")
    _require(symbols[start] < symbols[end], f"target disassembly has invalid {label} bounds")
    return [text for address, text in instructions
            if symbols[start] <= address < symbols[end] and not text.startswith("nop.")]


def _find_sequence(operations: list[str], patterns: tuple[str, ...]) -> bool:
    compiled = [re.compile(pattern) for pattern in patterns]
    for start in range(0, len(operations) - len(compiled) + 1):
        if all(pattern.fullmatch(operations[start + offset])
               for offset, pattern in enumerate(compiled)):
            return True
    return False


def _find_ordered(operations: list[str], patterns: tuple[str, ...],
                  max_distance: int = 24) -> bool:
    compiled = [re.compile(pattern) for pattern in patterns]
    for start, operation in enumerate(operations):
        if not compiled[0].fullmatch(operation):
            continue
        cursor = start + 1
        for pattern in compiled[1:]:
            while cursor < min(len(operations), start + max_distance) and \
                    not pattern.fullmatch(operations[cursor]):
                cursor += 1
            if cursor >= min(len(operations), start + max_distance):
                break
            cursor += 1
        else:
            return True
    return False


def _check_target(disassembly: str) -> dict[str, Any]:
    symbols, instructions = _parse_disassembly(disassembly)
    setup = _range(symbols, instructions, "ia64_coop_probe_setup", "ia64_coop_a_test",
                   "Context B setup")
    a_initial = _range(symbols, instructions, "ia64_coop_a_test", "ia64_coop_a_resumed_first",
                        "Context A initialization")
    a_checks = _range(symbols, instructions, "ia64_coop_a_resumed_first",
                      "ia64_coop_b_test", "Context A resumed branch checks")
    b_checks = _range(symbols, instructions, "ia64_coop_b_test", "ia64_coop_b_resumed",
                      "Context B first-entry branch checks")
    switch = _range(symbols, instructions, "ia64_coop_context_switch", "ia64_coop_switch_fail",
                    "cooperative switch")

    initialized_b: dict[str, int] = {}
    for reg, value in EXPECTED["B"].items():
        expected_offset = SLOTS[reg]
        matches = [i for i, operation in enumerate(setup)
                   if re.fullmatch(rf"movl\s+r15\s*=\s*0x{value:x}", operation)]
        valid = any(_find_sequence(setup[i:i + 3], (
            rf"movl\s+r15\s*=\s*0x{value:x}",
            rf"adds\s+r28\s*=\s*{expected_offset},\s*r14",
            r"st8\s+\[r28\]\s*=\s*r15",
        )) for i in matches)
        _require(valid,
                 f"target disassembly must write BR{reg}_B value {value:#x} to offset {expected_offset}")
        initialized_b[f"b{reg}"] = value

    initialized_a: dict[str, int] = {}
    for reg, value in EXPECTED["A"].items():
        matches = [i for i, operation in enumerate(a_initial)
                   if re.fullmatch(rf"movl\s+r14\s*=\s*0x{value:x}", operation)]
        valid = any(_find_sequence(a_initial[i:i + 2], (
            rf"movl\s+r14\s*=\s*0x{value:x}", rf"mov\s+b{reg}\s*=\s*r14"))
                    for i in matches)
        _require(valid, f"target disassembly must initialize Context A b{reg} independently")
        initialized_a[f"b{reg}"] = value

    for identity, operations in (("A", a_checks), ("B", b_checks)):
        for reg, value in EXPECTED[identity].items():
            required = (
                rf"mov\s+r14\s*=\s*b{reg}",
                rf"movl\s+r15\s*=\s*0x{value:x}",
                r"cmp\.eq\s+p6,\s*p7\s*=\s*r14,\s*r15",
            )
            _require(_find_sequence(operations, required),
                     f"target disassembly lacks the independent Context {identity} b{reg} assertion")

    saved: dict[str, int] = {}
    restored: dict[str, int] = {}
    for reg, offset in SLOTS.items():
        save_sequence = (
            rf"mov\s+r27\s*=\s*b{reg}",
            rf"adds\s+r28\s*=\s*{offset},\s*r14",
            r"st8\s+\[r28\]\s*=\s*r27",
        )
        _require(_find_sequence(switch, save_sequence),
                 f"target switch must save b{reg} to C_BR{reg} at {offset}")
        restore_sequence = (
            rf"adds\s+r28\s*=\s*{offset},\s*r15",
            r"ld8\s+r20\s*=\s*\[r28\]",
            rf"mov\s+b{reg}\s*=\s*r20",
        )
        _require(_find_sequence(switch, restore_sequence),
                 f"target switch must restore b{reg} from C_BR{reg} at {offset}")
        saved[f"b{reg}"] = offset
        restored[f"b{reg}"] = offset

    b0_save = (r"mov\s+r23\s*=\s*b0", r"adds\s+r28\s*=\s*64,\s*r14",
               r"st8\s+\[r28\]\s*=\s*r23")
    _require(_find_ordered(switch, b0_save),
             "target switch must save b0 separately in C_RP offset 64")
    b0_restore = (r"adds\s+r28\s*=\s*64,\s*r15", r"ld8\s+r20\s*=\s*\[r28\]",
                  r"mov\s+b0\s*=\s*r20")
    _require(_find_sequence(switch, b0_restore),
             "target switch must restore b0 separately from C_RP offset 64")
    return {"context_a_initial": initialized_a, "context_b_initial": initialized_b,
            "saved_branch_registers": saved, "restored_branch_registers": restored,
            "b0_return_link_restore": "C_RP@64"}


def validate_branch_register_contract(sequence_source: str, switch_source: str,
                                       context_header: str,
                                       target_disassembly: str) -> dict[str, Any]:
    """Prove branch-register initialization, assertions, layout, save, and restore."""
    source = _check_source(sequence_source, switch_source, context_header)
    target = _check_target(target_disassembly)
    _require(target["context_a_initial"] == source["context_a_initial"] and
             target["context_b_initial"] == source["context_b_initial"],
             "source branch-register definitions differ from target disassembly")
    return {"source_contract": source, "target_disassembly": target}
