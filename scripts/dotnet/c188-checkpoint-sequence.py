#!/usr/bin/env python3
"""Ordered, branch-aware checkpoint validation for C188 startup diagnostics."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


@dataclass(frozen=True)
class Step:
    name: str
    patterns: tuple[str, ...]
    optional: bool = False
    branch_key: str | None = None
    branch_value: str | None = None


def literal(name: str, marker: str, *, optional: bool = False) -> Step:
    return Step(name, (re.escape(marker),), optional)


def any_of(name: str, *markers: str) -> Step:
    return Step(name, tuple(re.escape(marker) for marker in markers))


def r4_pair(before: int, after: int, label: str) -> list[Step]:
    return [
        literal(f"before {label}", f"C188R4-{before:03d} BEFORE {label}"),
        literal(f"after {label}", f"C188R4-{after:03d} AFTER {label}"),
    ]


R4_OPERATION_STEPS: list[Step] = []
_r4_pairs = [
    (201, "C133-PROOF-CONTEXT-INITIAL"),
    (203, "C133-COMBO-TEST-CONTEXT-INITIAL"),
    (205, "C133-HOST-TEST-CONTEXT-INITIAL"),
    (207, "C134-HOST-TEST-CONTEXT-INITIAL"),
    (209, "C135-PROOF-CONTEXT-INITIAL"),
    (211, "C135-PROOF-CONTEXT-MERGE-C151-INITIAL"),
    (213, "C135-MENU-TEST-CONTEXT-INITIAL"),
    (215, "C135-HOST-TEST-CONTEXT-INITIAL"),
    (217, "C136-PROOF-CONTEXT-INITIAL"),
    (219, "C119-PROOF-CONTEXT"),
    (221, "C119-OPEN-BUTTON-RESET"),
    (223, "C119-SAVE-BUTTON-RESET"),
    (225, "C119-SAVE-AS-BUTTON-RESET"),
    (230, "C120-PROOF-CONTEXT"),
    (232, "C121-PROOF-CONTEXT"),
    (234, "C121-CHECKBOX-TEST-CONTEXT"),
    (236, "C121-HOST-TEST-CONTEXT"),
    (238, "C122-PROOF-CONTEXT"),
    (240, "C122-LABEL-TEST-CONTEXT"),
    (242, "C122-LABEL-HOST-TEST-CONTEXT"),
    (244, "C123-PROOF-CONTEXT"),
    (246, "C123-SEPARATOR-TEST-CONTEXT"),
    (248, "C123-SEPARATOR-HOST-TEST-CONTEXT"),
    (250, "C124-PROOF-CONTEXT"),
    (252, "C124-RADIO-TEST-CONTEXT"),
    (254, "C124-RADIO-HOST-TEST-CONTEXT"),
    (256, "C125-PROOF-CONTEXT"),
    (258, "C125-PROGRESS-TEST-CONTEXT"),
    (260, "C126-PROOF-CONTEXT"),
    (262, "C126-GROUP-BOX-TEST-CONTEXT"),
    (264, "C125-PROOF-CONTEXT-MERGE-C126"),
    (266, "C127-PROOF-CONTEXT"),
    (268, "C127-PANEL-TEST-CONTEXT"),
    (270, "C125-PROOF-CONTEXT-MERGE-C127"),
    (272, "C128-PROOF-CONTEXT"),
    (274, "C128-PANEL-LIFECYCLE-TEST-CONTEXT"),
    (276, "C125-PROOF-CONTEXT-MERGE-C128"),
    (278, "C129-PROOF-CONTEXT"),
    (280, "C129-SHIFT-TAB-TEST-CONTEXT"),
    (282, "C129-PROOF-STAGE-RESET"),
    (284, "C131-PROOF-CONTEXT"),
    (286, "C131-CHECKBOX-TEST-CONTEXT"),
    (288, "C131-HOST-TEST-CONTEXT"),
    (290, "C132-PROOF-CONTEXT"),
    (292, "C132-RADIO-TEST-CONTEXT"),
    (294, "C132-HOST-TEST-CONTEXT"),
    (296, "C133-PROOF-CONTEXT-SECOND-PASS"),
    (298, "C133-PROOF-CONTEXT-MERGE-C134"),
    (300, "C133-PROOF-CONTEXT-MERGE-C135"),
    (304, "C133-PROOF-CONTEXT-MERGE-C151"),
    (306, "C133-COMBO-TEST-CONTEXT-SECOND-PASS"),
    (308, "C133-HOST-TEST-CONTEXT-SECOND-PASS"),
    (310, "C134-HOST-TEST-CONTEXT-SECOND-PASS"),
    (312, "C135-PROOF-CONTEXT-SECOND-PASS"),
    (314, "C135-PROOF-CONTEXT-MERGE-C151"),
    (316, "C135-MENU-TEST-CONTEXT-SECOND-PASS"),
    (318, "C135-HOST-TEST-CONTEXT-SECOND-PASS"),
    (320, "C137-PROOF-CONTEXT-MERGE-C151"),
    (322, "C138-PROOF-CONTEXT"),
    (324, "C139-PROOF-CONTEXT"),
    (326, "C138-PROOF-CONTEXT-MERGE-C139"),
    (328, "C137-PROOF-CONTEXT-MERGE-C138"),
    (330, "C135-PROOF-CONTEXT-MERGE-C138"),
    (332, "C136-PROOF-CONTEXT-MERGE-C138"),
    (334, "C120-PROOF-CONTEXT-MERGE-C132"),
    (336, "C121-PROOF-CONTEXT-MERGE-C132"),
    (338, "C122-PROOF-CONTEXT-MERGE-C132"),
    (340, "C120-PROOF-CONTEXT-MERGE-C133"),
    (342, "C121-PROOF-CONTEXT-MERGE-C133"),
    (344, "C131-PROOF-CONTEXT-MERGE-C133"),
    (346, "C122-PROOF-CONTEXT-MERGE-C133"),
    (348, "C120-PROOF-CONTEXT-MERGE-C137"),
    (350, "C120-PROOF-CONTEXT-MERGE-C147"),
    (352, "C121-PROOF-CONTEXT-MERGE-C137"),
    (354, "C131-PROOF-CONTEXT-MERGE-C137"),
    (356, "C124-PROOF-CONTEXT-MERGE-C125"),
    (358, "C123-PROOF-CONTEXT-MERGE-C124"),
    (360, "C122-PROOF-CONTEXT-MERGE-C123"),
]
for before_id, label in _r4_pairs:
    R4_OPERATION_STEPS.extend(r4_pair(before_id, before_id + 1, label))


def branch_step(name: str, checkpoint_id: int, branch: str) -> Step:
    prefix = f"C188R4-{checkpoint_id:03d} BRANCH {branch}="
    return Step(name, (re.escape(prefix + "TAKEN"), re.escape(prefix + "SKIPPED")))


def branch_pair(before: int, label: str, branch: str) -> list[Step]:
    return [
        Step(
            f"before {label}",
            (re.escape(f"C188R4-{before:03d} BEFORE {label}"),),
            optional=True,
            branch_key=branch,
            branch_value="TAKEN",
        ),
        Step(
            f"after {label}",
            (re.escape(f"C188R4-{before + 1:03d} AFTER {label}"),),
            optional=True,
            branch_key=branch,
            branch_value="TAKEN",
        ),
    ]


def r6_branch_step(name: str, checkpoint_id: int, branch: str) -> Step:
    prefix = f"C188R6-{checkpoint_id:03d} BRANCH {branch}="
    return Step(name, (re.escape(prefix + "TAKEN"), re.escape(prefix + "SKIPPED")))


def r6_branch_pair(before: int, label: str, branch: str) -> list[Step]:
    return [
        Step(
            f"before {label}",
            (re.escape(f"C188R6-{before:03d} BEFORE {label}"),),
            optional=True,
            branch_key=branch,
            branch_value="TAKEN",
        ),
        Step(
            f"after {label}",
            (re.escape(f"C188R6-{before + 1:03d} AFTER {label}"),),
            optional=True,
            branch_key=branch,
            branch_value="TAKEN",
        ),
    ]


_c120_step_index = next(
    index for index, step in enumerate(R4_OPERATION_STEPS)
    if step.name == "before C120-PROOF-CONTEXT"
)
R4_OPERATION_STEPS[_c120_step_index:_c120_step_index] = [
    branch_step("C119 disabled-button branch", 227, "C119-DISABLED"),
    *branch_pair(228, "C119-SAVE-BUTTON-DISABLE", "C119-DISABLED"),
]


R4_BRANCH_STEPS = [
    branch_step("C133 host test branch", 370, "C133-HOST-TEST"),
    *branch_pair(371, "C133-HOST-TEST-CALL", "C133-HOST-TEST"),
    branch_step("C134 host test branch", 373, "C134-HOST-TEST"),
    *branch_pair(374, "C134-HOST-TEST-CALL", "C134-HOST-TEST"),
    branch_step("C135 host test branch", 376, "C135-HOST-TEST"),
    *branch_pair(377, "C135-HOST-TEST-CALL", "C135-HOST-TEST"),
    branch_step("C133 proof test branch", 379, "C133-PROOF-TEST"),
    *branch_pair(380, "C133-PROOF-TEST-CALL", "C133-PROOF-TEST"),
    branch_step("path label reset branch", 382, "C122-LABEL-RESET"),
    *branch_pair(383, "C122-LABEL-RESET", "C122-LABEL-RESET"),
    branch_step("separator reset branch", 385, "C123-SEPARATOR-RESET"),
    *branch_pair(386, "C123-SEPARATOR-RESET", "C123-SEPARATOR-RESET"),
    branch_step("radio setup branch", 388, "C124-RADIO-SETUP"),
    *branch_pair(389, "C124-RADIO-SETUP", "C124-RADIO-SETUP"),
    branch_step("combo setup branch", 391, "C133-COMBO-SETUP"),
    *branch_pair(392, "C133-COMBO-SETUP", "C133-COMBO-SETUP"),
    branch_step("popup menu setup branch", 394, "C135-POPUP-SETUP"),
    *branch_pair(395, "C135-POPUP-SETUP", "C135-POPUP-SETUP"),
    branch_step("document usage reset branch", 397, "C125-DOCUMENT-USAGE-RESET"),
    *branch_pair(398, "C125-DOCUMENT-USAGE-RESET", "C125-DOCUMENT-USAGE-RESET"),
    branch_step("C126 group box layout branch", 400, "C126-LAYOUT"),
    *branch_pair(401, "C126-LAYOUT", "C126-LAYOUT"),
    branch_step("C127 panel layout branch", 403, "C127-LAYOUT"),
    *branch_pair(404, "C127-LAYOUT", "C127-LAYOUT"),
]


def r3_steps(include_r4: bool) -> list[Step]:
    steps = [
        Step("C150 application instance", (r"C150-APP-INSTANCE id=\d+ generation=\d+",)),
        literal("application launch begin", "C188R3-010 APPLICATION-LAUNCH-BEGIN"),
        literal("Notes launch entry", "C188R3-100 NOTES-LAUNCH-ENTRY"),
        literal("Notes context validation", "C188R3-101 NOTES-CONTEXT-VALIDATED"),
        literal("C147 runtime context", "C147-NOTES-CONTEXT startup=loaded list=enabled result=PASS", optional=True),
        literal("C148 settings context", "C148-NOTES-CONTEXT settings=v2-migration-proof source=shared-runtime result=PASS", optional=True),
        literal("Notes settings context ready", "C188R3-102 NOTES-SETTINGS-CONTEXT-READY"),
        literal("Notes startup state reset", "C188R3-103 NOTES-STARTUP-STATE-RESET"),
    ]
    if include_r4:
        steps.extend(R4_OPERATION_STEPS)
        steps.extend(R4_BRANCH_STEPS)
    steps.extend(
        [
            literal("control initialization", "C188R3-110 NOTES-CONTROL-INITIALIZATION-BEGIN"),
            literal("control host creation", "C188R3-111 NOTES-CONTROL-HOST-CREATE-BEGIN"),
            literal("control host reset", "C188R3-112 NOTES-CONTROL-HOST-RESET-DONE"),
            literal("list reset begin", "C188R3-120 NOTES-LIST-RESET-BEGIN"),
            literal("list reset return", "C188R3-121 NOTES-LIST-RESET-RETURN"),
            literal("list population begin", "C188R3-122 NOTES-LIST-POPULATION-BEGIN"),
            literal("list population return", "C188R3-123 NOTES-LIST-POPULATION-RETURN"),
            literal("list control registration", "C188R3-124 NOTES-LIST-CONTROL-REGISTERED"),
            literal("control initialization return", "C188R3-125 NOTES-CONTROL-INITIALIZATION-RETURN"),
            literal("document load begin", "C188R3-130 NOTES-DOCUMENT-LOAD-BEGIN"),
            literal("document load return", "C188R3-131 NOTES-DOCUMENT-LOAD-RETURN"),
            literal("document state ready", "C188R3-132 NOTES-DOCUMENT-STATE-READY"),
            literal("surface request begin", "C188R3-140 NOTES-SURFACE-REQUEST-BEGIN"),
            any_of("surface request result", "C188R3-141 NOTES-SURFACE-REQUEST-SUCCESS", "C188R3-142 NOTES-SURFACE-REQUEST-FAILURE"),
            literal("layout initialization begin", "C188R3-150 NOTES-LAYOUT-INITIALIZATION-BEGIN"),
            any_of("layout initialization result", "C188R3-151 NOTES-LAYOUT-INITIALIZATION-RETURN-SUCCESS", "C188R3-152 NOTES-LAYOUT-INITIALIZATION-RETURN-FAILURE"),
            literal("first render begin", "C188R3-160 NOTES-FIRST-RENDER-BEGIN"),
            literal("RenderMain entry", "C188R3-200 RENDER-MAIN-ENTRY"),
            literal("RenderMain buffers ready", "C188R3-201 RENDER-MAIN-BUFFERS-READY"),
            literal("background begin", "C188R3-210 RENDER-BACKGROUND-BEGIN"),
            any_of("background result", "C188R3-211 RENDER-BACKGROUND-RETURN-SUCCESS", "C188R3-212 RENDER-BACKGROUND-RETURN-FAILURE"),
            literal("heading begin", "C188R3-213 RENDER-HEADING-BEGIN"),
            any_of("heading result", "C188R3-214 RENDER-HEADING-RETURN-SUCCESS", "C188R3-215 RENDER-HEADING-RETURN-FAILURE"),
            literal("launch count begin", "C188R3-216 RENDER-LAUNCH-COUNT-BEGIN"),
            any_of("launch count result", "C188R3-217 RENDER-LAUNCH-COUNT-RETURN-SUCCESS", "C188R3-218 RENDER-LAUNCH-COUNT-RETURN-FAILURE"),
            literal("path label begin", "C188R3-219 RENDER-PATH-LABEL-BEGIN"),
            any_of("path label result", "C188R3-220 RENDER-PATH-LABEL-RETURN-SUCCESS", "C188R3-221 RENDER-PATH-LABEL-RETURN-FAILURE"),
            literal("text area begin", "C188R3-222 RENDER-TEXT-AREA-BEGIN"),
            any_of("text area result", "C188R3-223 RENDER-TEXT-AREA-RETURN-SUCCESS", "C188R3-224 RENDER-TEXT-AREA-RETURN-FAILURE"),
            literal("list render begin", "C188R3-225 RENDER-LIST-BEGIN"),
            any_of("list render result", "C188R3-226 RENDER-LIST-RETURN-SUCCESS", "C188R3-227 RENDER-LIST-RETURN-FAILURE"),
            literal("status render begin", "C188R3-228 RENDER-STATUS-BEGIN"),
            any_of("status render result", "C188R3-229 RENDER-STATUS-RETURN-SUCCESS", "C188R3-230 RENDER-STATUS-RETURN-FAILURE"),
            literal("editor info begin", "C188R3-231 RENDER-EDITOR-INFO-BEGIN"),
            any_of("editor info result", "C188R3-232 RENDER-EDITOR-INFO-RETURN-SUCCESS", "C188R3-233 RENDER-EDITOR-INFO-RETURN-FAILURE"),
            literal("keyboard tips begin", "C188R3-234 RENDER-KEYBOARD-TIPS-BEGIN"),
            any_of("keyboard tips result", "C188R3-235 RENDER-KEYBOARD-TIPS-RETURN-SUCCESS", "C188R3-236 RENDER-KEYBOARD-TIPS-RETURN-FAILURE"),
            literal("open button begin", "C188R3-237 RENDER-OPEN-BUTTON-BEGIN"),
            any_of("open button result", "C188R3-238 RENDER-OPEN-BUTTON-RETURN-SUCCESS", "C188R3-239 RENDER-OPEN-BUTTON-RETURN-FAILURE"),
            literal("save button begin", "C188R3-240 RENDER-SAVE-BUTTON-BEGIN"),
            any_of("save button result", "C188R3-241 RENDER-SAVE-BUTTON-RETURN-SUCCESS", "C188R3-242 RENDER-SAVE-BUTTON-RETURN-FAILURE"),
            literal("save as button begin", "C188R3-243 RENDER-SAVE-AS-BUTTON-BEGIN"),
            any_of("save as button result", "C188R3-244 RENDER-SAVE-AS-BUTTON-RETURN-SUCCESS", "C188R3-245 RENDER-SAVE-AS-BUTTON-RETURN-FAILURE"),
            literal("settings button begin", "C188R3-246 RENDER-SETTINGS-BUTTON-BEGIN"),
            any_of("settings button result", "C188R3-247 RENDER-SETTINGS-BUTTON-RETURN-SUCCESS", "C188R3-248 RENDER-SETTINGS-BUTTON-RETURN-FAILURE"),
            literal("reload button begin", "C188R3-249 RENDER-RELOAD-BUTTON-BEGIN"),
            any_of("reload button result", "C188R3-250 RENDER-RELOAD-BUTTON-RETURN-SUCCESS", "C188R3-251 RENDER-RELOAD-BUTTON-RETURN-FAILURE"),
            any_of("RenderMain result", "C188R3-299 RENDER-MAIN-RETURN-SUCCESS", "C188R3-298 RENDER-MAIN-RETURN-FAILURE"),
            literal("first render return", "C188R3-161 NOTES-FIRST-RENDER-RETURN"),
            literal("post-render settings begin", "C188R3-162 NOTES-POST-RENDER-SETTINGS-BEGIN"),
            literal("post-render settings return", "C188R3-163 NOTES-POST-RENDER-SETTINGS-RETURN"),
            any_of("Notes launch result", "C188R3-170 NOTES-LAUNCH-RESULT-SUCCESS", "C188R3-171 NOTES-LAUNCH-RESULT-FAILURE"),
            literal("application launch return", "C188R3-011 APPLICATION-LAUNCH-RETURN"),
            any_of("application result", "C188R3-012 APPLICATION-RESULT-SUCCESS", "C188R3-013 APPLICATION-RESULT-FAILURE"),
            Step("native launch return", (r"\[C102-LAUNCH-RETURN\] status=0x[0-9A-Fa-f]+",)),
            literal("input manager initialized", "[KERNEL] Input manager initialized"),
            literal("main input loop", "[KERNEL] Entering main loop (waiting for input)..."),
        ]
    )
    return steps


def r6_steps() -> list[Step]:
    steps = r3_steps(True)
    launch_entry = next(index for index, step in enumerate(steps) if step.name == "Notes launch entry")
    steps.insert(launch_entry, literal("before ManagedNotes.Launch call", "C188R6-010 BEFORE APPLICATION-LAUNCH-CALL"))

    post_render = next(index for index, step in enumerate(steps) if step.name == "post-render settings return") + 1
    post_render_steps = [
        literal("after checkpoint 163", "C188R6-200 AFTER C188R3-163"),
        literal("post-render context summaries begin", "C188R6-201 BEFORE POSTRENDER-CONTEXT-SUMMARIES"),
        r6_branch_step("C133 initial summary branch", 228, "C133-INITIAL-SUMMARY"),
        *r6_branch_pair(229, "C133-INITIAL-SUMMARY-LOG", "C133-INITIAL-SUMMARY"),
        r6_branch_step("C135 initial summary branch", 231, "C135-INITIAL-SUMMARY"),
        *r6_branch_pair(232, "C135-INITIAL-SUMMARY", "C135-INITIAL-SUMMARY"),
        literal("post-render context summaries return", "C188R6-202 AFTER POSTRENDER-CONTEXT-SUMMARIES"),
        literal("before C117 post-render status logs", "C188R6-234 BEFORE C117-POSTRENDER-STATUS-LOGS"),
        literal("after C117 post-render status logs", "C188R6-235 AFTER C117-POSTRENDER-STATUS-LOGS"),
        r6_branch_step("focused proof catalog branch", 203, "FOCUSED-PROOF-CATALOG"),
        *r6_branch_pair(204, "FOCUSED-PROOF-CATALOG", "FOCUSED-PROOF-CATALOG"),
        r6_branch_step("C137 test branch", 206, "C137-TEST"),
        *r6_branch_pair(207, "C137-RUNTIME-CAPTURE", "C137-TEST"),
        *r6_branch_pair(209, "C137-DEFAULTS-COMMIT", "C137-TEST"),
        *r6_branch_pair(211, "C137-SUITE", "C137-TEST"),
        *r6_branch_pair(213, "C137-RUNTIME-RESTORE", "C137-TEST"),
        r6_branch_step("C137 summary branch", 215, "C137-TEST-SUMMARY"),
        *r6_branch_pair(216, "C137-TEST-SUMMARY-LOG", "C137-TEST-SUMMARY"),
        r6_branch_step("C139 test branch", 218, "C139-TEST"),
        *r6_branch_pair(219, "C139-SUITE", "C139-TEST"),
        r6_branch_step("C139 summary branch", 221, "C139-TEST-SUMMARY"),
        *r6_branch_pair(222, "C139-TEST-SUMMARY-LOG", "C139-TEST-SUMMARY"),
        literal("before Notes result calculation", "C188R6-224 BEFORE NOTES-LAUNCH-RESULT-CALCULATION"),
        literal("before Notes result log", "C188R6-225 BEFORE NOTES-LAUNCH-RESULT-LOG"),
        literal("after Notes result log", "C188R6-226 AFTER NOTES-LAUNCH-RESULT-LOG"),
        literal("before ManagedNotes.Launch return", "C188R6-227 BEFORE MANAGEDNOTES-LAUNCH-RETURN"),
    ]
    steps[post_render:post_render] = post_render_steps

    caller_return = next(index for index, step in enumerate(steps) if step.name == "application launch return")
    steps.insert(caller_return, literal("after application launch return", "C188R6-011 AFTER APPLICATION-LAUNCH-RETURN"))
    caller_return = next(index for index, step in enumerate(steps) if step.name == "application launch return") + 1
    steps.insert(caller_return, literal("before application result log", "C188R6-012 BEFORE APPLICATION-RESULT-LOG"))
    application_result = next(index for index, step in enumerate(steps) if step.name == "application result") + 1
    steps.insert(application_result, literal("after application result log", "C188R6-013 AFTER APPLICATION-RESULT-LOG"))
    return steps


EVENT_PATTERNS = (
    re.compile(r"^C188R[346]-\d{3} [A-Z0-9][A-Z0-9 =_-]*$"),
    re.compile(r"^C147-NOTES-CONTEXT startup=loaded list=enabled result=PASS$"),
    re.compile(r"^C148-NOTES-CONTEXT settings=v2-migration-proof source=shared-runtime result=PASS$"),
    re.compile(r"^C150-APP-INSTANCE id=\d+ generation=\d+$"),
    re.compile(r"^\[C102-LAUNCH-RETURN\] status=0x[0-9A-Fa-f]+$"),
    re.compile(r"^\[KERNEL\] Input manager initialized$"),
    re.compile(r"^\[KERNEL\] Entering main loop \(waiting for input\)\.\.\.$"),
)


def _pattern_matches(pattern: str, value: str) -> bool:
    return re.fullmatch(pattern, value) is not None


def extract_events(serial_text: str) -> list[dict[str, object]]:
    events: list[dict[str, object]] = []
    offset = 0
    for line_number, line_with_ending in enumerate(serial_text.splitlines(keepends=True), start=1):
        line = line_with_ending.rstrip("\r\n")
        stripped = line.strip()
        marker_text = re.sub(r"^\[(?:C102|C150)-MANAGED-OUTPUT\]\s+", "", stripped)
        for pattern in EVENT_PATTERNS:
            match = pattern.fullmatch(marker_text)
            if match:
                start_in_line = line.find(marker_text)
                events.append(
                    {
                        "marker": marker_text,
                        "line": line_number,
                        "byteOffset": len(serial_text[: offset + start_in_line].encode("utf-8")),
                    }
                )
                break
        offset += len(line_with_ending)
    return events


def _branch_values(events: list[dict[str, object]]) -> dict[str, str]:
    values: dict[str, str] = {}
    for event in events:
        marker = str(event["marker"])
        match = re.fullmatch(r"C188R[46]-\d{3} BRANCH ([A-Z0-9-]+)=(TAKEN|SKIPPED)", marker)
        if match:
            values[match.group(1)] = match.group(2)
    return values


def validate_text(
    serial_text: str,
    steps: Iterable[Step],
    *,
    capture_state: str = "unknown",
) -> dict[str, object]:
    events = extract_events(serial_text)
    ordered_steps = list(steps)
    branches = _branch_values(events)
    missing: list[str] = []
    optional_missing: list[str] = []
    duplicates: list[dict[str, object]] = []
    out_of_order: list[dict[str, object]] = []
    unexpected_branch_events: list[str] = []
    consumed_positions: set[int] = set()
    previous_position = -1
    last_completed: str | None = None
    first_missing: str | None = None

    all_positions: dict[str, list[int]] = {}
    for index, event in enumerate(events):
        all_positions.setdefault(str(event["marker"]), []).append(index)
    for marker, positions in all_positions.items():
        if len(positions) > 1:
            duplicates.append({"marker": marker, "count": len(positions), "lines": [events[p]["line"] for p in positions]})

    for step in ordered_steps:
        if step.branch_key is not None:
            branch_value = branches.get(step.branch_key)
            if branch_value is None:
                is_optional = step.optional
            else:
                is_optional = branch_value != step.branch_value
        else:
            is_optional = step.optional

        positions = [
            index
            for index, event in enumerate(events)
            if any(_pattern_matches(pattern, str(event["marker"])) for pattern in step.patterns)
        ]
        if step.branch_key is not None and branches.get(step.branch_key) != step.branch_value:
            if branches.get(step.branch_key) is not None and positions:
                unexpected_branch_events.extend(str(events[pos]["marker"]) for pos in positions)
            continue

        if not positions:
            if is_optional:
                optional_missing.append(step.name)
            else:
                missing.append(step.name)
                if first_missing is None:
                    first_missing = step.name
            continue

        # If an expected checkpoint only appears before the prior expected
        # checkpoint, report the ordering error and retain the raw chronology.
        available = [position for position in positions if position > previous_position]
        if not available:
            out_of_order.append(
                {
                    "checkpoint": step.name,
                    "marker": str(events[positions[0]]["marker"]),
                    "line": events[positions[0]]["line"],
                    "afterCheckpoint": last_completed,
                }
            )
            if first_missing is None and not is_optional:
                first_missing = step.name
            continue

        selected = available[0]
        consumed_positions.add(selected)
        previous_position = selected
        last_completed = str(events[selected]["marker"])
        if len(available) > 1 and len(step.patterns) > 1:
            duplicates.append(
                {
                    "checkpoint": step.name,
                    "count": len(available),
                    "lines": [events[p]["line"] for p in available],
                    "kind": "multiple-alternatives",
                }
            )

    complete = capture_state == "complete"
    serial_has_final_terminator = serial_text.endswith("\n") or serial_text.endswith("\r")
    if complete and serial_has_final_terminator:
        truncation = "complete-capture"
    elif complete:
        truncation = "closed-capture-final-line-not-terminated"
    elif capture_state == "truncated":
        truncation = "known-truncated"
    elif capture_state == "incomplete":
        truncation = "capture-incomplete"
    else:
        truncation = "unknown"

    if first_missing is None and missing:
        first_missing = missing[0]
    integrity_ok = not duplicates and not out_of_order and not unexpected_branch_events
    sequence_ok = not missing and integrity_ok
    last_observed = events[-1]["marker"] if events else None
    return {
        "result": "PASS" if sequence_ok else "FAIL",
        "captureState": capture_state,
        "serialTruncationStatus": truncation,
        "serialHasFinalLineTerminator": serial_has_final_terminator,
        "checkpointAbsenceInterpretation": "unobserved-in-complete-capture" if complete else "not-distinguishable-from-capture-truncation",
        "lastObservedCheckpoint": last_observed,
        "lastExpectedCheckpointCompleted": last_completed,
        "firstExpectedMissingCheckpoint": first_missing,
        "missingRequiredCheckpoints": missing,
        "missingOptionalCheckpoints": optional_missing,
        "duplicateCheckpoints": duplicates,
        "outOfOrderCheckpoints": out_of_order,
        "unexpectedBranchCheckpoints": sorted(set(unexpected_branch_events)),
        "branchOutcomes": branches,
        "observedOrderedCheckpoints": events,
        "recognizedCheckpointCount": len(events),
    }


def describe(phase: str) -> dict[str, object]:
    if phase == "C188R6":
        steps = r6_steps()
    else:
        steps = r3_steps(phase in {"C188R4", "C188R5"})
    return {
        "phase": phase,
        "expectedOrderedCheckpoints": [
            {"name": step.name, "patterns": list(step.patterns), "optional": step.optional, "branchKey": step.branch_key, "branchValue": step.branch_value}
            for step in steps
        ],
        "interpretation": "Optional path checkpoints do not count as missing. A closed complete serial capture distinguishes an unobserved checkpoint from truncation; duplicate and out-of-order markers are reported separately.",
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    describe_parser = subparsers.add_parser("describe")
    describe_parser.add_argument("--phase", choices=("C188R3", "C188R4", "C188R5", "C188R6"), required=True)
    validate_parser = subparsers.add_parser("validate")
    validate_parser.add_argument("--phase", choices=("C188R3", "C188R4", "C188R5", "C188R6"), required=True)
    validate_parser.add_argument("--serial", type=Path, required=True)
    validate_parser.add_argument("--capture-state", choices=("complete", "truncated", "incomplete", "unknown"), default="unknown")
    args = parser.parse_args(argv)
    if args.command == "describe":
        print(json.dumps(describe(args.phase), indent=2))
        return 0
    raw = args.serial.read_bytes()
    text = raw.decode("utf-8", errors="replace")
    phase_steps = r6_steps() if args.phase == "C188R6" else r3_steps(args.phase in {"C188R4", "C188R5"})
    result = validate_text(text, phase_steps, capture_state=args.capture_state)
    result["serialPath"] = str(args.serial)
    result["serialSha256"] = hashlib.sha256(raw).hexdigest().upper()
    print(json.dumps(result, indent=2))
    return 0 if result["result"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
