#!/usr/bin/env python3
"""Focused regression tests for guest/QEMU USB CSW trace classification."""
from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CLASSIFIER = ROOT / "scripts" / "classify-dm20-usb-trace.py"
TAG = "0x000001BC"
QH = "0x3BF60AF0"
TD = "0x3BF60B00"


def records(*, send_status: bool = True, fetch: bool = True,
            packet_add: bool = True, complete: bool = True,
            qh_state: str = "QH_POINTS_AT_EXPECTED_TD",
            packet_attempts: int = 1,
            packet_failure: str | None = None,
            csw_async: bool = False) -> tuple[str, str, str]:
    serial = "\n".join([
        "[USB-MSC] command-submit BOT#000001BB TEST-UNIT-READY opcode=0x00 "
        "direction=0x80 bytes=0x00000000 tag=0x000001BB",
        "[USB-UHCI] csw-submit sequence=BOT#000001BB opcode=0x00 "
        "cbw-tag=0x000001BB expected-csw-tag=0x000001BB direction=0x80 "
        "frnum-submit=0x0150 out-toggle-start=0x00 csw-expected-toggle=0x01",
        "[USB-UHCI] csw-complete sequence=BOT#000001BB opcode=0x00 "
        "actual-received=0x000D frnum-submit=0x0150 frnum-complete=0x0151 "
        "toggle-final=0x00 class=COMPLETE_EXPECTED_CSW",
        f"[USB-MSC] command-submit BOT#000001BC WRITE10 opcode=0x2A tag={TAG} "
        "direction=0x00 bytes=0x00000200 lba=0x0000000000027FDF "
        "blocks=0x00000001 logical-block-size=0x00000200",
        f"[USB-UHCI] csw-submit sequence=BOT#000001BC opcode=0x2A "
        f"cbw-tag={TAG} expected-csw-tag={TAG} td-pa={TD} qh-pa={QH} "
        "lba=0x0000000000027FDF blocks=0x00000001 logical-block-size=0x00000200 "
        "command-data-bytes=0x00000200 data-out-expected=0x00000200 "
        "data-out-actual=0x00000200 data-out-td-count=0x00000008 "
        "data-out-first-td=0x3BF60B00 data-out-last-td=0x3BF60B70 "
        "out-toggle-start=0x00 out-toggle-final=0x00 csw-expected-toggle=0x01 "
        "csw-expected-length=0x000D endpoint=0x81 frnum-submit=0x015C",
        f"[USB-FAULT] signature=USB_CSW_TIMEOUT_OBSERVED "
        f"sequence=BOT#000001BC opcode=0x2A cbw-tag={TAG} "
        f"expected-csw-tag={TAG} data-out-expected=0x200 "
        f"data-out-actual=0x200 td-pa={TD} qh-pa={QH} "
        f"td-active-at-deadline=yes td-active-after-diagnostic=yes "
        f"diagnostic-late-completion=no deadline-qh-state={qh_state}",
    ]) + "\n"

    trace = [
        f"usb_msd_cmd_submit lun 0, tag 0x1bc, flags 0x00000000, len 10, data-len 512",
        f"usb_uhci_td_load qh {QH.lower()}, td {TD.lower()}, ctrl 0x00000000, token 0x000102e1",
    ]
    for remaining in range(512, 0, -64):
        trace.append(f"usb_msd_data_out 64/{remaining}")
    # The first OUT data TD deliberately shares the CSW TD address. Its OUT
    # completion must not be mistaken for CSW IN completion.
    trace.extend([
        f"usb_uhci_packet_add token 0x102e1, td {TD.lower()}",
        f"usb_uhci_packet_complete_success token 0x102e1, td {TD.lower()}",
        f"usb_uhci_td_complete qh {QH.lower()}, td {TD.lower()}",
    ])
    if fetch:
        trace.append(
            f"usb_uhci_td_load qh {QH.lower()}, td {TD.lower()}, ctrl 0x00000000, token 0x00008269")
    if send_status:
        trace.extend([
            "usb_msd_cmd_complete status 0, tag 0x1bc",
            "usb_msd_send_status status 0, tag 0x1bc, len 13",
        ])
    if packet_add:
        for _ in range(packet_attempts):
            trace.append(f"usb_uhci_packet_add token 0x8269, td {TD.lower()}")
    if csw_async:
        trace.extend([
            "usb_msd_packet_async",
            f"usb_uhci_packet_link_async token 0x8269, td {TD.lower()}",
            f"usb_uhci_td_async qh {QH.lower()}, td {TD.lower()}",
        ])
    if packet_failure:
        trace.append(f"usb_uhci_packet_complete_{packet_failure} token 0x8269, td {TD.lower()}")
    if complete:
        trace.extend([
            f"usb_uhci_packet_complete_success token 0x8269, td {TD.lower()}",
            f"usb_uhci_td_complete qh {QH.lower()}, td {TD.lower()}",
        ])
    trace_text = "\n".join(trace) + "\n"
    events = "usb_uhci_td_load\nusb_uhci_qh_load\n"
    return serial, trace_text, events


class CswTraceClassificationTests(unittest.TestCase):
    def classify(self, *, send_status: bool = True, fetch: bool = True,
                 packet_add: bool = True, complete: bool = True,
                 qh_state: str = "QH_POINTS_AT_EXPECTED_TD",
                 packet_attempts: int = 1,
                 packet_failure: str | None = None,
                 csw_async: bool = False) -> dict:
        serial_text, trace_text, events_text = records(
            send_status=send_status, fetch=fetch, packet_add=packet_add,
            complete=complete, qh_state=qh_state,
            packet_attempts=packet_attempts, packet_failure=packet_failure,
            csw_async=csw_async)
        with tempfile.TemporaryDirectory(prefix="dm21-csw-trace-") as temp:
            root = Path(temp)
            serial = root / "serial.log"
            trace = root / "uhci.trace.log"
            output = root / "correlation.json"
            serial.write_text(serial_text, encoding="utf-8")
            trace.write_text(trace_text, encoding="utf-8")
            (root / "usb-uhci.trace-events.txt").write_text(events_text, encoding="ascii")
            subprocess.run([sys.executable, str(CLASSIFIER), "--serial", str(serial),
                            "--trace", str(trace), "--output", str(output)],
                           check=True, capture_output=True, text=True)
            return json.loads(output.read_text(encoding="utf-8"))

    def test_exact_csw_qh_td_completion_and_write_bytes(self) -> None:
        result = self.classify()
        row = result["failureClassifications"][0]
        span = row["qemuTraceSpan"]
        self.assertEqual(row["classification"], "QEMU_TD_COMPLETE_GUEST_TD_STILL_ACTIVE")
        self.assertEqual(span["dataOutBytes"], 512)
        self.assertTrue(span["guestAddressEqualsQemuTd"])
        self.assertTrue(span["expectedTdQhCompletionObserved"])
        metadata = row["guestCommandMetadata"]
        self.assertEqual(metadata["guestLba"], "0x0000000000027FDF")
        self.assertEqual(metadata["guestDataOutActualBytes"], "0x00000200")
        self.assertEqual(metadata["guestDataOutTdCount"], "0x00000008")
        self.assertEqual(metadata["guestExpectedCswToggle"], "0x01")
        self.assertEqual(row["previousCommandHistory"][0]["opcodeName"],
                         "TEST-UNIT-READY")
        self.assertEqual(row["previousCommandHistory"][0]["result"],
                         "COMPLETE_EXPECTED_CSW")

    def test_reused_out_td_does_not_masquerade_as_csw_success(self) -> None:
        result = self.classify(packet_add=False, complete=False)
        row = result["failureClassifications"][0]
        self.assertEqual(row["classification"], "EXPECTED_CSW_TD_FETCHED_WITHOUT_PACKET_ADD")
        self.assertFalse(row["qemuTraceSpan"]["expectedTdCompletionObserved"])

    def test_status_without_expected_td_fetch(self) -> None:
        result = self.classify(fetch=False, packet_add=False, complete=False)
        self.assertEqual(result["failureClassifications"][0]["classification"],
                         "CSW_STATUS_EMITTED_EXPECTED_TD_NOT_FETCHED")

    def test_guest_qh_advanced_past_active_csw_td(self) -> None:
        result = self.classify(complete=False, qh_state="QH_MOVED_TO_NEXT_TD")
        self.assertEqual(result["failureClassifications"][0]["classification"],
                         "GUEST_QH_ADVANCED_PAST_ACTIVE_CSW_TD")

    def test_repeated_csw_in_submissions_do_not_claim_nak(self) -> None:
        result = self.classify(complete=False, packet_attempts=3)
        row = result["failureClassifications"][0]
        self.assertEqual(row["classification"],
                         "REPEATED_CSW_IN_SUBMISSIONS_NO_COMPLETION_NAK_UNPROVEN")
        self.assertEqual(row["qemuTraceSpan"]["expectedTdPacketAttempts"], 3)
        self.assertIn("NAK is not asserted", row["reason"])

    def test_terminal_packet_error_is_distinguished(self) -> None:
        result = self.classify(complete=False, packet_failure="error")
        self.assertEqual(result["failureClassifications"][0]["classification"],
                         "EXPECTED_CSW_TD_PACKET_FAILED")

    def test_async_csw_path_is_distinct_from_nak_or_missing_fetch(self) -> None:
        result = self.classify(send_status=False, complete=False, csw_async=True)
        row = result["failureClassifications"][0]
        self.assertEqual(row["classification"], "QEMU_CSW_ASYNC_PENDING_WITHOUT_STATUS")
        self.assertTrue(row["qemuTraceSpan"]["qemuAsyncCswObserved"])
        self.assertTrue(row["qemuTraceSpan"]["qemuMassStorageAsyncObserved"])
        self.assertNotIn("NAK", row["classification"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
