#!/usr/bin/env python3
"""Join DM20 guest BOT records to QEMU usb_msd/ UHCI trace chronology."""
from __future__ import annotations

import argparse
import json
import re
from collections import Counter
from pathlib import Path


def read_text(path: Path) -> str:
    text = path.read_text(encoding="utf-8", errors="replace")
    text = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", text)
    return "".join(ch for ch in text if ch in "\r\n\t" or " " <= ch <= "~")


def fields(line: str) -> dict[str, str]:
    return dict(re.findall(r"([a-z][a-z0-9-]*)=([^\s]+)", line, re.I))


def value(record: dict[str, str], key: str) -> int | None:
    raw = record.get(key)
    if raw is None:
        return None
    try:
        return int(raw, 0)
    except ValueError:
        return None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--serial", required=True, type=Path)
    parser.add_argument("--trace", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    serial = read_text(args.serial)
    trace_captured = bool(args.trace and args.trace.is_file())
    trace_lines = read_text(args.trace).splitlines() if trace_captured else []

    guest_commands: dict[int, dict[str, str]] = {}
    csw_submit: dict[int, dict[str, str]] = {}
    csw_complete: dict[int, dict[str, str]] = {}
    fault_records: list[dict[str, str]] = []
    for line in serial.splitlines():
        if "signature=USB_CSW_TIMEOUT_ACTIVE_TD" in line:
            rec = fields(line)
            match = re.search(r"sequence=BOT#([0-9a-fA-F]+)", line)
            if match:
                rec["sequence"] = str(int(match.group(1), 16))
            fault_records.append(rec)
        elif "[USB-MSC] command-submit BOT#" in line:
            rec = fields(line)
            match = re.search(r"command-submit BOT#([0-9a-fA-F]+)(?:\s+([^\s]+))?", line)
            if match:
                seq = int(match.group(1), 16)
                rec["sequence"] = str(seq)
                if match.group(2):
                    rec["opcode-name"] = match.group(2)
                guest_commands[seq] = rec
        elif "[USB-UHCI] csw-submit sequence=BOT#" in line:
            rec = fields(line)
            match = re.search(r"csw-submit sequence=BOT#([0-9a-fA-F]+)", line)
            if match:
                rec["sequence"] = str(int(match.group(1), 16))
                csw_submit[int(match.group(1), 16)] = rec
        elif "[USB-UHCI] csw-complete sequence=BOT#" in line:
            rec = fields(line)
            match = re.search(r"csw-complete sequence=BOT#([0-9a-fA-F]+)", line)
            if match:
                rec["sequence"] = str(int(match.group(1), 16))
                csw_complete[int(match.group(1), 16)] = rec

    qemu_submits: list[tuple[int, int]] = []
    for index, line in enumerate(trace_lines):
        submit = re.search(
            r"usb_msd_cmd_submit lun \d+, tag (0x[0-9a-fA-F]+), flags 0x[0-9a-fA-F]+, len \d+, data-len \d+",
            line,
        )
        if submit:
            qemu_submits.append((index, int(submit.group(1), 16)))
    kernel_trace_start = 0
    for previous, current in zip(qemu_submits, qemu_submits[1:]):
        if current[1] == 1 and previous[1] > 1:
            kernel_trace_start = current[0]
            break

    qemu_by_tag: dict[int, dict[str, object]] = {}
    last_in_packet_add: tuple[int, int, int] | None = None
    pending_status_tag: int | None = None
    pending_td_complete_tag: int | None = None
    command_submits = command_completes = status_records = 0
    pre_kernel_submits = sum(1 for _, tag in qemu_submits
                             if kernel_trace_start and _ < kernel_trace_start)
    post_kernel_tags: list[int] = []
    schedule_starts = 0
    timeout_faults = len(fault_records)
    for index, line in enumerate(trace_lines):
        if line.startswith("usb_uhci_schedule_start"):
            schedule_starts += 1
        packet = re.search(r"usb_uhci_packet_add token (0x[0-9a-fA-F]+), td (0x[0-9a-fA-F]+)", line)
        if packet and index >= kernel_trace_start:
            token = int(packet.group(1), 16)
            if (token & 0xFF) == 0x69:
                last_in_packet_add = (index, token, int(packet.group(2), 16))
        submit = re.search(
            r"usb_msd_cmd_submit lun (\d+), tag (0x[0-9a-fA-F]+), flags (0x[0-9a-fA-F]+), len (\d+), data-len (\d+)",
            line,
        )
        if submit:
            tag = int(submit.group(2), 16)
            command_submits += 1
            if index >= kernel_trace_start:
                post_kernel_tags.append(tag)
                qemu_by_tag.setdefault(tag, {})["submit"] = {
                    "traceLine": index + 1,
                    "lun": int(submit.group(1)),
                    "flags": int(submit.group(3), 16),
                    "cdbLength": int(submit.group(4)),
                    "dataLength": int(submit.group(5)),
                }
        complete = re.search(r"usb_msd_cmd_complete status (\d+), tag (0x[0-9a-fA-F]+)", line)
        if complete:
            tag = int(complete.group(2), 16)
            command_completes += 1
            if index >= kernel_trace_start:
                qemu_by_tag.setdefault(tag, {})["commandComplete"] = {
                    "traceLine": index + 1,
                    "status": int(complete.group(1)),
                }
        send = re.search(r"usb_msd_send_status status (\d+), tag (0x[0-9a-fA-F]+), len (\d+)", line)
        if send:
            tag = int(send.group(2), 16)
            status_records += 1
            if index >= kernel_trace_start:
                qemu_by_tag.setdefault(tag, {})["sendStatus"] = {
                    "traceLine": index + 1,
                    "status": int(send.group(1)),
                    "length": int(send.group(3)),
                    "uhciPacketAddLine": last_in_packet_add[0] + 1 if last_in_packet_add else None,
                    "qemuHostTd": f"0x{last_in_packet_add[2]:08X}" if last_in_packet_add else None,
                }
                pending_status_tag = tag
        packet_success = re.search(
            r"usb_uhci_packet_complete_success token (0x[0-9a-fA-F]+), td (0x[0-9a-fA-F]+)",
            line,
        )
        if packet_success and pending_status_tag is not None:
            tag = pending_status_tag
            td = int(packet_success.group(2), 16)
            status = qemu_by_tag.get(tag, {}).get("sendStatus", {})
            expected_td = int(status["qemuHostTd"], 16) if status.get("qemuHostTd") else None
            if expected_td is None or td == expected_td:
                status["uhciPacketCompleteSuccess"] = {
                    "traceLine": index + 1,
                    "token": f"0x{int(packet_success.group(1), 16):08X}",
                    "td": f"0x{td:08X}",
                }
                pending_td_complete_tag = tag
            pending_status_tag = None
        td_complete = re.search(
            r"usb_uhci_td_complete qh (0x[0-9a-fA-F]+), td (0x[0-9a-fA-F]+)",
            line,
        )
        if td_complete and pending_td_complete_tag is not None:
            tag = pending_td_complete_tag
            status = qemu_by_tag.get(tag, {}).get("sendStatus", {})
            completed_td = int(td_complete.group(2), 16)
            expected_td = int(status["qemuHostTd"], 16) if status.get("qemuHostTd") else None
            if expected_td is None or completed_td == expected_td:
                status["uhciTdComplete"] = {
                    "traceLine": index + 1,
                    "qh": f"0x{int(td_complete.group(1), 16):08X}",
                    "td": f"0x{completed_td:08X}",
                }
            pending_td_complete_tag = None

    post_kernel_submits = len(post_kernel_tags)
    post_kernel_tag_contiguous = bool(post_kernel_tags) and all(
        tag == index + 1 for index, tag in enumerate(post_kernel_tags)
    )

    joins = []
    tag_to_sequence: dict[int, list[int]] = {}
    for seq, guest in guest_commands.items():
        tag = value(guest, "tag")
        if tag is not None:
            tag_to_sequence.setdefault(tag, []).append(seq)
    for seq in sorted(set(guest_commands) | set(csw_submit) | set(csw_complete)):
        guest = guest_commands.get(seq, {})
        submit = csw_submit.get(seq, {})
        complete = csw_complete.get(seq, {})
        tag = value(submit, "cbw-tag") or value(submit, "expected-csw-tag") or value(guest, "tag")
        qemu = qemu_by_tag.get(tag, {}) if tag is not None else {}
        joins.append({
            "sequence": seq,
            "opcode": guest.get("opcode") or submit.get("opcode"),
            "opcodeName": guest.get("opcode-name"),
            "cbwTag": f"0x{tag:08X}" if tag is not None else None,
            "incarnation": submit.get("incarnation"),
            "guestTdPhysical": submit.get("td-pa"),
            "guestQhPhysical": submit.get("qh-pa"),
            "guestDmaBufferPhysical": submit.get("buffer-pa"),
            "transferGeneration": submit.get("generation"),
            "guestCompletionBytes": complete.get("actual-received"),
            "guestCallerCsw": complete.get("caller-csw"),
            "guestCswClass": complete.get("class"),
            "qemu": qemu,
            "tagUniqueInGuest": len(tag_to_sequence.get(tag, [])) == 1 if tag is not None else False,
        })

    matched = sum(1 for row in joins if row["qemu"].get("sendStatus"))
    qemu_len13 = sum(1 for row in joins if row["qemu"].get("sendStatus", {}).get("length") == 13)
    guest_len13 = sum(1 for row in joins if value(csw_complete.get(row["sequence"], {}), "actual-received") == 13)
    qemu_status_by_tag = Counter(
        row["qemu"].get("sendStatus", {}).get("status")
        for row in joins if row["qemu"].get("sendStatus") is not None
    )
    failure_classifications = []
    for fault in fault_records:
        tag = value(fault, "cbw-tag") or value(fault, "expected-csw-tag")
        qemu = qemu_by_tag.get(tag, {}) if tag is not None else {}
        if not trace_captured:
            reason = "No QEMU trace was captured for this failed run."
        elif not qemu.get("submit"):
            reason = (
                "The trace has no matching Mass Storage command submit; TD scheduling "
                "and device response cannot be distinguished."
            )
        elif qemu.get("sendStatus"):
            reason = (
                "QEMU emitted a status for this tag, but guest descriptor visibility "
                "and exact TD completion still need direct evidence."
            )
        else:
            reason = (
                "A matching command was observed, but the available trace does not "
                "prove why its CSW TD remained active."
            )
        failure_classifications.append({
            "sequence": fault.get("sequence"),
            "signature": fault.get("signature"),
            "opcode": fault.get("opcode"),
            "cbwTag": f"0x{tag:08X}" if tag is not None else None,
            "classification": "UNCLASSIFIED",
            "reason": reason,
            "guestFault": fault,
            "qemuEvidenceForTag": qemu,
        })
    summary = {
        "schema": "DM20-USB-TRACE-CORRELATION-1",
        "serialPath": str(args.serial.resolve()),
        "tracePath": str(args.trace.resolve()) if args.trace else None,
        "qemuTraceCaptured": trace_captured,
        "guestCommandRecords": len(guest_commands),
        "guestCswSubmitRecords": len(csw_submit),
        "guestCswCompleteRecords": len(csw_complete),
        "guestCommandsWithoutCswSubmit": len(set(guest_commands) - set(csw_submit)),
        "guestCommandsMissingCswSubmitSequences": [
            f"0x{seq:X}" for seq in sorted(set(guest_commands) - set(csw_submit))[:16]
        ],
        "cswSubmitsWithoutGuestCommand": len(set(csw_submit) - set(guest_commands)),
        "cswSubmitsMissingGuestCommandSequences": [
            f"0x{seq:X}" for seq in sorted(set(csw_submit) - set(guest_commands))[:16]
        ],
        "guestCswCompletionsMissing": len(set(csw_submit) - set(csw_complete)),
        "guestCswCompletionMissingSequences": [
            f"0x{seq:X}" for seq in sorted(set(csw_submit) - set(csw_complete))[:16]
        ],
        "activeCswTimeoutFaults": timeout_faults,
        "failureClassifications": failure_classifications,
        "qemuScheduleStartEvents": schedule_starts,
        "qemuMassStorageCommandSubmits": command_submits,
        "qemuMassStorageCommandCompletes": command_completes,
        "qemuMassStorageStatusRecords": status_records,
        "qemuPreKernelMassStorageSubmits": pre_kernel_submits,
        "qemuKernelMassStorageSubmits": post_kernel_submits,
        "qemuKernelTagSequenceContiguousFromOne": post_kernel_tag_contiguous,
        "guestTagToQemuStatusJoins": matched,
        "qemu13ByteStatusRecordsJoined": qemu_len13,
        "guest13ByteCswCompletions": guest_len13,
        "qemuCommandStatusCodes": {str(key): count for key, count in sorted(qemu_status_by_tag.items())},
        "joinLimit": (
            "QEMU usb_uhci td fields are reported from QEMU's internal address space. "
            "They numerically match the guest TD physical address in the traced "
            "WRITE(10) control, but the classifier joins by BOT tag and trace "
            "chronology and does not assume that address identity is universal."
        ),
        "joins": joins,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: val for key, val in summary.items() if key != "joins"}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
