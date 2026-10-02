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
    trace_event_file = (args.trace.parent / "usb-uhci.trace-events.txt"
                        if args.trace else None)
    enabled_trace_events = set(
        read_text(trace_event_file).splitlines()
        if trace_event_file and trace_event_file.is_file() else [])

    guest_commands: dict[int, dict[str, str]] = {}
    csw_submit: dict[int, dict[str, str]] = {}
    csw_complete: dict[int, dict[str, str]] = {}
    fault_records: list[dict[str, str]] = []
    for line in serial.splitlines():
        if ("signature=USB_CSW_TIMEOUT_ACTIVE_TD" in line or
                "signature=USB_CSW_TIMEOUT_OBSERVED" in line):
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

    # Keep complete QEMU command spans so repeated BOT tags cannot silently
    # overwrite one another.  A span runs from one MSC command submit up to
    # the next command submit; UHCI packet and descriptor events in that range
    # belong to the command currently being serviced.
    qemu_spans_by_tag: dict[int, list[dict[str, object]]] = {}
    kernel_submit_indices = [index for index, tag in qemu_submits
                             if index >= kernel_trace_start]
    for position, start in enumerate(kernel_submit_indices):
        end = (kernel_submit_indices[position + 1]
               if position + 1 < len(kernel_submit_indices) else len(trace_lines))
        span = trace_lines[start:end]
        submit_match = re.search(
            r"usb_msd_cmd_submit lun (\d+), tag (0x[0-9a-fA-F]+), flags "
            r"(0x[0-9a-fA-F]+), len (\d+), data-len (\d+)", trace_lines[start])
        if not submit_match:
            continue
        tag = int(submit_match.group(2), 16)
        data_out = []
        data_in = []
        in_packets = []
        td_loads = []
        qh_loads = []
        qh_progress = []
        td_completions = []
        packet_results = []
        command_complete = None
        send_status = None
        for offset, line in enumerate(span):
            line_no = start + offset + 1
            match = re.search(r"usb_msd_data_out (\d+)/(\d+)", line)
            if match:
                data_out.append({"traceLine": line_no,
                                 "bytes": int(match.group(1)),
                                 "remaining": int(match.group(2))})
            match = re.search(r"usb_msd_data_in (\d+)/(\d+)", line)
            if match:
                data_in.append({"traceLine": line_no,
                                "bytes": int(match.group(1)),
                                "remaining": int(match.group(2))})
            match = re.search(
                r"usb_uhci_packet_add token (0x[0-9a-fA-F]+), td (0x[0-9a-fA-F]+)", line)
            if match and (int(match.group(1), 16) & 0xFF) == 0x69:
                in_packets.append({"traceLine": line_no,
                                   "token": f"0x{int(match.group(1), 16):08X}",
                                   "td": f"0x{int(match.group(2), 16):08X}"})
            match = re.search(
                r"usb_uhci_td_load qh (0x[0-9a-fA-F]+), td (0x[0-9a-fA-F]+), "
                r"ctrl (0x[0-9a-fA-F]+), token (0x[0-9a-fA-F]+)", line)
            if match:
                td_loads.append({"traceLine": line_no,
                                 "qh": f"0x{int(match.group(1), 16):08X}",
                                 "td": f"0x{int(match.group(2), 16):08X}",
                                 "token": f"0x{int(match.group(4), 16):08X}"})
            match = re.search(r"usb_uhci_qh_load qh (0x[0-9a-fA-F]+)", line)
            if match:
                qh_loads.append({"traceLine": line_no,
                                 "qh": f"0x{int(match.group(1), 16):08X}"})
            if ("usb_uhci_td_queue" in line or "usb_uhci_td_nextqh" in line or
                    "usb_uhci_td_async" in line or
                    "usb_uhci_packet_link_async" in line or
                    "usb_msd_packet_async" in line):
                qh_progress.append({"traceLine": line_no, "event": line})
            match = re.search(
                r"usb_uhci_td_complete qh (0x[0-9a-fA-F]+), td (0x[0-9a-fA-F]+)", line)
            if match:
                td_completions.append({"traceLine": line_no,
                                       "qh": f"0x{int(match.group(1), 16):08X}",
                                       "td": f"0x{int(match.group(2), 16):08X}"})
            match = re.search(
                r"usb_uhci_(?:packet_complete_(success|error|short(?:xfer)?|stall|babble|async)|packet_cancel) "
                r"token (0x[0-9a-fA-F]+), td (0x[0-9a-fA-F]+)", line)
            if match:
                packet_results.append({"traceLine": line_no,
                                       "result": match.group(1) or "cancel",
                                       "token": f"0x{int(match.group(2), 16):08X}",
                                       "td": f"0x{int(match.group(3), 16):08X}"})
            match = re.search(r"usb_msd_cmd_complete status (\d+), tag (0x[0-9a-fA-F]+)", line)
            if match and int(match.group(2), 16) == tag:
                command_complete = {"traceLine": line_no,
                                    "status": int(match.group(1))}
            match = re.search(r"usb_msd_send_status status (\d+), tag (0x[0-9a-fA-F]+), len (\d+)", line)
            if match and int(match.group(2), 16) == tag:
                send_status = {"traceLine": line_no,
                               "status": int(match.group(1)),
                               "length": int(match.group(3))}
        detail = {
            "traceSpan": [start + 1, end],
            "dataOutPackets": data_out,
            "dataOutBytes": sum(packet["bytes"] for packet in data_out),
            "dataInPackets": data_in,
            "inPacketAdds": in_packets,
            "tdLoads": td_loads,
            "qhLoads": qh_loads,
            "qhProgressEvents": qh_progress,
            "packetResults": packet_results,
            "tdCompletions": td_completions,
            "commandComplete": command_complete,
            "sendStatus": send_status,
        }
        qemu_spans_by_tag.setdefault(tag, []).append(detail)

    joins = []
    guest_tag_occurrences: Counter[int] = Counter()
    qemu_detail_by_sequence: dict[int, dict[str, object]] = {}
    guest_fault_by_sequence = {
        value(fault, "sequence"): fault for fault in fault_records
        if value(fault, "sequence") is not None
    }
    ordered_guest_sequences = sorted(guest_commands)

    def history_item(history_sequence: int) -> dict[str, object]:
        history_guest = guest_commands.get(history_sequence, {})
        history_submit = csw_submit.get(history_sequence, {})
        history_complete = csw_complete.get(history_sequence, {})
        history_fault = guest_fault_by_sequence.get(history_sequence)
        history_tag = (value(history_submit, "cbw-tag") or
                       value(history_submit, "expected-csw-tag") or
                       value(history_guest, "tag"))
        history_result = history_complete.get("class")
        if history_fault:
            history_result = history_fault.get("signature") or "TRANSPORT_FAILURE"
        elif not history_result:
            history_result = "CSW_PENDING"
        start_frame = value(history_submit, "frnum-submit")
        end_frame = value(history_complete, "frnum-complete")
        return {
            "sequence": history_sequence,
            "opcode": history_guest.get("opcode") or history_submit.get("opcode"),
            "opcodeName": history_guest.get("opcode-name"),
            "tag": f"0x{history_tag:08X}" if history_tag is not None else None,
            "direction": history_guest.get("direction") or history_submit.get("direction"),
            "bytes": history_guest.get("bytes"),
            "lba": history_guest.get("lba") or history_submit.get("lba"),
            "blockCount": history_guest.get("blocks") or history_submit.get("blocks"),
            "logicalBlockSize": history_guest.get("logical-block-size") or
                history_submit.get("logical-block-size"),
            "result": history_result,
            "actualCswBytes": history_complete.get("actual-received"),
            "dataOutExpectedBytes": history_submit.get("data-out-expected"),
            "dataOutActualBytes": history_submit.get("data-out-actual"),
            "dataOutTdCount": history_submit.get("data-out-td-count"),
            "outToggleStart": history_submit.get("out-toggle-start"),
            "outToggleFinal": history_submit.get("out-toggle-final"),
            "expectedCswToggle": history_submit.get("csw-expected-toggle"),
            "finalCswToggle": history_complete.get("toggle-final"),
            "cswElapsedFrames": ((end_frame - start_frame) & 0x7FF)
                if end_frame is not None and start_frame is not None else None,
        }
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
        detail = None
        if tag is not None:
            matches_for_tag = qemu_spans_by_tag.get(tag, [])
            occurrence = guest_tag_occurrences[tag]
            if occurrence < len(matches_for_tag):
                detail = matches_for_tag[occurrence]
                qemu_detail_by_sequence[seq] = detail
            guest_tag_occurrences[tag] += 1
        expected_td = value(submit, "td-pa")
        expected_qh = value(submit, "qh-pa")
        previous_sequences = [prior for prior in ordered_guest_sequences
                              if prior < seq][-16:]
        if detail is not None:
            status_line = detail["sendStatus"]["traceLine"] if detail.get("sendStatus") else None
            data_stage_lines = [packet["traceLine"] for packet in
                detail["dataOutPackets"] + detail["dataInPackets"]]
            data_stage_end_line = max(data_stage_lines) if data_stage_lines else 0
            detail["cswDataStageEndLine"] = data_stage_end_line
            detail["cswInPacketAdds"] = [packet for packet in detail["inPacketAdds"]
                if packet["traceLine"] > data_stage_end_line]
            detail["cswInTdFetches"] = [
                load for load in detail["tdLoads"]
                if (int(load["token"], 16) & 0xFF) == 0x69 and
                load["traceLine"] > data_stage_end_line]
            for packet in detail["cswInPacketAdds"]:
                packet["matchesGuestExpectedTd"] = (
                    expected_td is not None and
                    int(packet["td"], 16) == expected_td)
            for load in detail["cswInTdFetches"]:
                load["matchesGuestExpectedTd"] = (
                    expected_td is not None and int(load["td"], 16) == expected_td)
            for completion in detail["tdCompletions"]:
                completion["matchesGuestExpectedTd"] = (
                    expected_td is not None and
                    int(completion["td"], 16) == expected_td)
                completion["matchesGuestExpectedQh"] = (
                    expected_qh is not None and
                    int(completion["qh"], 16) == expected_qh)
            detail["guestExpectedTd"] = f"0x{expected_td:08X}" if expected_td is not None else None
            detail["guestExpectedQh"] = f"0x{expected_qh:08X}" if expected_qh is not None else None
            detail["expectedTdPacketAddObserved"] = any(
                packet["matchesGuestExpectedTd"] for packet in detail["cswInPacketAdds"])
            successful_csw_packets = [result for result in detail["packetResults"]
                if result["result"] == "success" and
                (int(result["token"], 16) & 0xFF) == 0x69 and
                any(result["traceLine"] > packet["traceLine"] and
                    result["token"].lower() == packet["token"].lower() and
                    result["td"].lower() == packet["td"].lower()
                    for packet in detail["cswInPacketAdds"])]
            detail["expectedTdCompletionObserved"] = any(
                expected_td is not None and
                result["td"].lower() == f"0x{expected_td:08X}".lower()
                for result in successful_csw_packets)
            detail["expectedTdQhCompletionObserved"] = any(
                completion["matchesGuestExpectedTd"] and
                completion["matchesGuestExpectedQh"] and
                any(completion["traceLine"] > result["traceLine"] and
                    completion["td"].lower() == result["td"].lower()
                    for result in successful_csw_packets)
                for completion in detail["tdCompletions"])
            detail["expectedTdFetchObserved"] = any(
                load["matchesGuestExpectedTd"]
                for load in detail["cswInTdFetches"])
            detail["qemuAsyncCswEvents"] = []
            for event in detail["qhProgressEvents"]:
                line = event["event"]
                match = re.search(
                    r"usb_uhci_packet_link_async token (0x[0-9a-fA-F]+), td (0x[0-9a-fA-F]+)",
                    line)
                if match and expected_td is not None and \
                        int(match.group(2), 16) == expected_td and \
                        event["traceLine"] > data_stage_end_line:
                    detail["qemuAsyncCswEvents"].append({
                        "traceLine": event["traceLine"], "kind": "packet_link_async",
                        "token": f"0x{int(match.group(1), 16):08X}",
                        "td": f"0x{int(match.group(2), 16):08X}"})
                match = re.search(
                    r"usb_uhci_td_async qh (0x[0-9a-fA-F]+), td (0x[0-9a-fA-F]+)",
                    line)
                if match and expected_td is not None and \
                        int(match.group(2), 16) == expected_td and \
                        (expected_qh is None or int(match.group(1), 16) == expected_qh) and \
                        event["traceLine"] > data_stage_end_line:
                    detail["qemuAsyncCswEvents"].append({
                        "traceLine": event["traceLine"], "kind": "td_async",
                        "qh": f"0x{int(match.group(1), 16):08X}",
                        "td": f"0x{int(match.group(2), 16):08X}"})
            detail["qemuAsyncCswObserved"] = bool(detail["qemuAsyncCswEvents"])
            detail["qemuMassStorageAsyncObserved"] = any(
                "usb_msd_packet_async" in event["event"] and
                event["traceLine"] > data_stage_end_line
                for event in detail["qhProgressEvents"])
            detail["expectedQhOtherTdLoadedInCswPhase"] = any(
                expected_qh is not None and
                load["traceLine"] > data_stage_end_line and
                int(load["qh"], 16) == expected_qh and
                int(load["td"], 16) != expected_td
                for load in detail["tdLoads"])
            detail["tdFetchTraceEnabled"] = "usb_uhci_td_load" in enabled_trace_events
            expected_csw_packets = [packet for packet in detail["cswInPacketAdds"]
                if packet["matchesGuestExpectedTd"]]
            detail["expectedTdPacketAttempts"] = len(expected_csw_packets)
            detail["expectedTdPacketResults"] = [result for result in detail["packetResults"]
                if any(result["traceLine"] > packet["traceLine"] and
                    result["token"].lower() == packet["token"].lower() and
                    result["td"].lower() == packet["td"].lower()
                    for packet in expected_csw_packets)]
            detail["guestAddressEqualsQemuTd"] = (
                expected_td is not None and any(
                    int(packet["td"], 16) == expected_td
                    for packet in detail["cswInPacketAdds"]))
        joins.append({
            "sequence": seq,
            "opcode": guest.get("opcode") or submit.get("opcode"),
            "opcodeName": guest.get("opcode-name"),
            "cbwTag": f"0x{tag:08X}" if tag is not None else None,
            "incarnation": submit.get("incarnation"),
            "guestTdPhysical": submit.get("td-pa"),
            "guestQhPhysical": submit.get("qh-pa"),
            "guestDmaBufferPhysical": submit.get("buffer-pa"),
            "guestLba": submit.get("lba"),
            "guestBlockCount": submit.get("blocks"),
            "guestLogicalBlockSize": submit.get("logical-block-size"),
            "guestCommandDataBytes": submit.get("command-data-bytes"),
            "guestDataOutExpectedBytes": submit.get("data-out-expected"),
            "guestDataOutActualBytes": submit.get("data-out-actual"),
            "guestDataOutTdCount": submit.get("data-out-td-count"),
            "guestFirstDataOutTd": submit.get("data-out-first-td"),
            "guestLastDataOutTd": submit.get("data-out-last-td"),
            "guestDataOutStartToggle": submit.get("out-toggle-start"),
            "guestDataOutFinalToggle": submit.get("out-toggle-final"),
            "guestExpectedCswToggle": submit.get("csw-expected-toggle"),
            "guestCswExpectedLength": submit.get("csw-expected-length"),
            "guestCswTdPhysical": submit.get("td-pa"),
            "guestCswEndpoint": submit.get("endpoint"),
            "guestCswSubmitFrame": submit.get("frnum-submit"),
            "guestCswCompleteFrame": complete.get("frnum-complete"),
            "transferGeneration": submit.get("generation"),
            "guestCompletionBytes": complete.get("actual-received"),
            "guestCswElapsedFrames": (
                ((value(complete, "frnum-complete") -
                  value(submit, "frnum-submit")) & 0x7FF)
                if value(complete, "frnum-complete") is not None and
                   value(submit, "frnum-submit") is not None else None),
            "previousCommandHistory": [history_item(prior)
                                       for prior in previous_sequences],
            "guestCallerCsw": complete.get("caller-csw"),
            "guestCswClass": complete.get("class"),
            "qemu": qemu,
            "qemuTraceSpan": detail if guest.get("opcode", "").lower() in
                ("0x2a", "0x8a") else None,
            "tagUniqueInGuest": len(tag_to_sequence.get(tag, [])) == 1 if tag is not None else False,
        })

    matched = sum(1 for row in joins if row["qemu"].get("sendStatus"))
    join_by_sequence = {row["sequence"]: row for row in joins}
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
        sequence = value(fault, "sequence")
        detail = qemu_detail_by_sequence.get(sequence, {}) if sequence is not None else {}
        guest_join = join_by_sequence.get(sequence, {}) if sequence is not None else {}
        if not trace_captured:
            classification = "TRACE_MISSING"
            reason = "No QEMU trace was captured for this failed run."
        elif not detail:
            classification = "NO_MATCHING_QEMU_COMMAND_SPAN"
            reason = (
                "The trace has no matching Mass Storage command submit; TD scheduling "
                "and device response cannot be distinguished."
            )
        elif detail.get("qemuAsyncCswObserved") and not detail.get("sendStatus"):
            classification = "QEMU_CSW_ASYNC_PENDING_WITHOUT_STATUS"
            reason = (
                "QEMU linked the guest's exact CSW TD into its asynchronous packet path, "
                "and the trace contains no Mass Storage status for the tag before the "
                "guest timeout. This establishes pending asynchronous CSW work; it does "
                "not establish why the emulated storage operation remained pending."
            )
        elif detail.get("sendStatus") and detail.get("expectedTdQhCompletionObserved"):
            if fault.get("diagnostic-late-completion") == "yes":
                classification = "UHCI_COMPLETION_AFTER_PRODUCTION_DEADLINE"
                reason = (
                    "QEMU completed the expected CSW TD and the guest observed it only "
                    "during diagnostic extension; the normal transfer deadline expired first."
                )
            elif fault.get("td-active-at-deadline") == "yes" and \
                    fault.get("td-active-after-diagnostic") == "yes":
                classification = "QEMU_TD_COMPLETE_GUEST_TD_STILL_ACTIVE"
                reason = (
                    "QEMU traced successful completion of the guest's exact QH/TD, while "
                    "the guest snapshot still reported that TD active after diagnostic "
                    "extension. This points to a descriptor/completion visibility mismatch; "
                    "the trace alone does not identify its cause."
                )
            else:
                classification = "QEMU_COMPLETED_EXPECTED_CSW_TD_GUEST_TIMEOUT"
                reason = (
                    "QEMU traced successful completion of the guest's exact CSW QH/TD, "
                    "but the guest reported a CSW timeout. Inspect the deadline snapshot "
                    "and late-completion flag to distinguish timing from visibility."
                )
        elif detail.get("sendStatus") and not detail.get("expectedTdPacketAddObserved"):
            if detail.get("expectedQhOtherTdLoadedInCswPhase"):
                classification = "EXPECTED_QH_FETCHED_DIFFERENT_TD_AFTER_CSW"
                reason = (
                    "After QEMU emitted the CSW, UHCI traced a different TD on the guest's "
                    "expected QH and did not trace the expected CSW TD packet. This is direct "
                    "evidence of QH/TD progression away from the expected descriptor; it does "
                    "not by itself identify the descriptor-link cause."
                )
            elif detail.get("expectedTdFetchObserved"):
                classification = "EXPECTED_CSW_TD_FETCHED_WITHOUT_PACKET_ADD"
                reason = (
                    "QEMU traced a fetch of the exact CSW IN TD after status emission, but "
                    "did not trace its packet submission. The failure is in UHCI processing "
                    "between descriptor fetch and USB packet submission."
                )
            elif detail.get("tdFetchTraceEnabled"):
                classification = "CSW_STATUS_EMITTED_EXPECTED_TD_NOT_FETCHED"
                reason = (
                    "QEMU emitted the CSW, and TD-fetch tracing was enabled, but the trace "
                    "contains no fetch or packet submission for the guest's expected CSW TD. "
                    "The exact CSW descriptor was not observed by the traced UHCI fetch path."
                )
            else:
                classification = "CSW_STATUS_EMITTED_TD_FETCH_NOT_CAPTURED"
                reason = (
                    "QEMU emitted the CSW but the expected CSW TD was not queued. This run "
                    "did not enable TD-fetch tracing, so the trace cannot establish whether "
                    "the controller fetched the descriptor."
                )
        elif detail.get("sendStatus") and detail.get("expectedTdPacketAddObserved"):
            guest_qh_state = fault.get("deadline-qh-state") or fault.get("qh-state")
            terminal_packet_results = [result for result in
                detail.get("expectedTdPacketResults", [])
                if result["result"] in ("error", "stall", "babble", "shortxfer", "cancel")]
            if terminal_packet_results:
                classification = "EXPECTED_CSW_TD_PACKET_FAILED"
                reason = (
                    "QEMU traced a terminal packet result for the expected CSW TD: " +
                    ", ".join(result["result"] for result in terminal_packet_results) +
                    ". This identifies packet/controller failure at the CSW stage."
                )
            elif detail.get("qemuAsyncCswObserved") and \
                    not detail.get("expectedTdCompletionObserved"):
                classification = "EXPECTED_CSW_TD_REMAINS_ASYNC"
                reason = (
                    "QEMU traced the expected CSW TD in its asynchronous packet path, but "
                    "the traced span contains no successful completion before the guest "
                    "timeout. The async event is direct; its internal wait cause is not."
                )
            elif detail.get("expectedTdCompletionObserved") and \
                    not detail.get("expectedTdQhCompletionObserved"):
                classification = "CSW_PACKET_SUCCESS_WITHOUT_EXPECTED_QH_COMPLETION"
                reason = (
                    "QEMU traced a successful CSW IN packet for the expected TD but no "
                    "matching TD completion on the expected QH. The guest QH snapshot and "
                    "descriptor state distinguish a progression/visibility fault."
                )
            elif detail.get("expectedTdPacketAttempts", 0) > 1 and \
                    not detail.get("expectedTdPacketResults"):
                classification = "REPEATED_CSW_IN_SUBMISSIONS_NO_COMPLETION_NAK_UNPROVEN"
                reason = (
                    "QEMU traced repeated submissions of the exact CSW IN TD without a "
                    "terminal packet result. This build's UHCI trace events do not directly "
                    "report USB NAK, so retries are evidence of repeated attempts only; "
                    "NAK is not asserted."
                )
            elif guest_qh_state == "QH_MOVED_TO_NEXT_TD" and \
                    fault.get("td-active-at-deadline") == "yes":
                classification = "GUEST_QH_ADVANCED_PAST_ACTIVE_CSW_TD"
                reason = (
                    "At the production deadline the guest QH pointed at the next TD while "
                    "the expected CSW TD remained active. QEMU's command span confirms the "
                    "CSW was emitted and its exact TD was queued; inspect the captured link, "
                    "TD status, packet result, and descriptor reuse history."
                )
            elif guest_qh_state == "QH_TERMINATED" and \
                    fault.get("td-active-at-deadline") == "yes":
                classification = "GUEST_QH_TERMINATED_WITH_ACTIVE_CSW_TD"
                reason = (
                    "At the production deadline the guest QH was terminated while the CSW TD "
                    "remained active. The QEMU trace confirms status emission and exact TD "
                    "submission; inspect the QH link and controller progression."
                )
            else:
                classification = "EXPECTED_CSW_TD_QUEUED_WITHOUT_SUCCESSFUL_COMPLETION"
                reason = (
                    "QEMU emitted the CSW and queued the guest's exact CSW TD, but the traced "
                    "span contains no successful completion of that TD. Inspect packet error, "
                    "async, and guest controller state."
                )
        elif detail.get("commandComplete"):
            classification = "MSD_COMMAND_COMPLETE_WITHOUT_CSW_STATUS"
            reason = (
                "QEMU completed the matching mass-storage command but emitted no CSW status "
                "in its trace span. This identifies the emulated device response path as the "
                "first missing stage in the captured chronology."
            )
        elif detail.get("dataOutBytes", 0) == value(fault, "data-out-expected"):
            classification = "FULL_DATA_OUT_WITHOUT_COMMAND_OR_CSW_COMPLETION"
            reason = (
                "The trace shows the expected OUT byte count, but no matching QEMU command "
                "completion or CSW status. The failure is after bulk OUT delivery; the "
                "available events do not isolate the internal cause."
            )
        else:
            classification = "UNCLASSIFIED"
            reason = (
                "A matching command span exists, but its observed data, command, CSW, and "
                "UHCI events do not isolate the failed stage."
            )
        failure_classifications.append({
            "sequence": fault.get("sequence"),
            "signature": fault.get("signature"),
            "opcode": fault.get("opcode"),
            "cbwTag": f"0x{tag:08X}" if tag is not None else None,
            "classification": classification,
            "reason": reason,
            "guestFault": fault,
            "guestCommandMetadata": {
                key: guest_join.get(key) for key in (
                    "guestLba", "guestBlockCount", "guestLogicalBlockSize",
                    "guestCommandDataBytes", "guestDataOutExpectedBytes",
                    "guestDataOutActualBytes", "guestDataOutTdCount",
                    "guestFirstDataOutTd", "guestLastDataOutTd",
                    "guestDataOutStartToggle", "guestDataOutFinalToggle",
                    "guestExpectedCswToggle", "guestCswExpectedLength",
                    "guestCswTdPhysical", "guestCswEndpoint",
                    "guestCswSubmitFrame", "guestCswCompleteFrame",
                    "guestCswElapsedFrames")
            },
            "previousCommandHistory": guest_join.get("previousCommandHistory", []),
            "qemuEvidenceForTag": qemu,
            "qemuTraceSpan": detail,
        })
    summary = {
        "schema": "DM21-USB-TRACE-CORRELATION-1",
        "serialPath": str(args.serial.resolve()),
        "tracePath": str(args.trace.resolve()) if args.trace else None,
        "qemuTraceCaptured": trace_captured,
        "qemuTdFetchTraceEnabled": "usb_uhci_td_load" in enabled_trace_events,
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
        "qemuCommandSpans": sum(len(spans) for spans in qemu_spans_by_tag.values()),
        "joinLimit": (
            "Trace spans are joined to guest command chronology by BOT tag occurrence. "
            "QEMU UHCI TD/QH addresses are compared numerically with the captured guest "
            "physical addresses; equality is reported only when directly observed. "
            "UHCI trace events do not directly prove USB NAK responses, so packet retries "
            "or async events are retained as observations and never labeled as NAK."
        ),
        "joins": joins,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: val for key, val in summary.items() if key != "joins"}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
