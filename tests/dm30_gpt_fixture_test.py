#!/usr/bin/env python3
"""Validate DM30's independent 512-byte and 4Kn GPT fixture generator."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import unittest


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CREATE = os.path.join(ROOT, "scripts", "create-dm30-gpt-fixture.py")
VERIFY = os.path.join(ROOT, "scripts", "verify-dm29-gpt.py")


class Dm30GptFixtureTests(unittest.TestCase):
    def test_three_partition_fixture_and_geometry(self) -> None:
        with tempfile.TemporaryDirectory(prefix="dm30-gpt-") as temp:
            for sector_size, expected_array_sectors in ((512, 32), (4096, 4)):
                image = os.path.join(temp, f"fixture-{sector_size}.raw")
                created = subprocess.run(
                    [sys.executable, CREATE, image, "--sector-size", str(sector_size)],
                    check=True, capture_output=True, text=True)
                fixture = json.loads(created.stdout)
                verified = subprocess.run(
                    [sys.executable, VERIFY, image, "--sector-size", str(sector_size)],
                    check=True, capture_output=True, text=True)
                report = json.loads(verified.stdout)
                self.assertEqual(fixture["logical_sector_size"], sector_size)
                self.assertEqual(fixture["gpt_entry_array_sectors"], expected_array_sectors)
                self.assertEqual(len(report["partitions"]), 3)
                self.assertEqual(report["redundancy"], "Healthy")
                self.assertTrue(report["normalized_copies_equal"])
                self.assertTrue(report["protective_mbr"]["valid"])
                self.assertEqual(
                    [part["number"] for part in report["partitions"]], [1, 2, 3])
                self.assertEqual(
                    [part["name"] for part in report["partitions"]],
                    ["DM30 Partition A", "DM30 Partition B", "DM30 Partition C"])
                self.assertEqual(
                    [len(part["canaries"]) for part in fixture["partitions"]],
                    [3, 3, 3])
                for left, right in zip(report["partitions"], fixture["partitions"]):
                    self.assertEqual(left["type_guid"], right["type_guid"])
                    self.assertEqual(left["unique_guid"], right["unique_guid"])
                    self.assertEqual(left["first_lba"], right["start_lba"])
                    self.assertEqual(left["last_lba"], right["end_lba"])
                    self.assertEqual(left["attributes"], right["attributes"])


if __name__ == "__main__":
    unittest.main()
