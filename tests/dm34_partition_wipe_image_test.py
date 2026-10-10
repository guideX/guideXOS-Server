import contextlib
import hashlib
import io
import json
import tempfile
import unittest
from pathlib import Path

from dm34_partition_wipe_image import verify


class PartitionWipeImageVerifierTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        self.pre = self.directory / "fixture.pre.raw"
        self.image = self.directory / "fixture.raw"
        original = bytes((i * 37 + 11) & 0xFF for i in range(4096))
        self.pre.write_bytes(original)
        self.image.write_bytes(original)
        manifest = {
            "schema": "DM34-partition-wipe-fixture-v1",
            "images": [{
                "image": self.image.name,
                "bytes": len(original),
                "preImage": self.pre.name,
                "preImageSha256": hashlib.sha256(original).hexdigest(),
                "sectorSize": 512,
                "target": {"startLba": 1, "endLbaInclusive": 2},
            }],
        }
        self.manifest = self.directory / "manifest.json"
        self.manifest.write_text(json.dumps(manifest), encoding="utf-8")

    def tearDown(self):
        self.temp.cleanup()

    def test_completed_requires_all_target_bytes_zero_and_preserves_outside(self):
        post = bytearray(self.pre.read_bytes())
        post[512:1536] = bytes(1024)
        self.image.write_bytes(post)
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(verify(self.manifest, self.image, "complete"), 0)

    def test_stopped_requires_target_change_without_claiming_prefix(self):
        post = bytearray(self.pre.read_bytes())
        post[512] = 0
        self.image.write_bytes(post)
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(verify(self.manifest, self.image, "stopped"), 0)

    def test_any_outside_target_change_fails(self):
        post = bytearray(self.pre.read_bytes())
        post[1536] ^= 0xFF
        self.image.write_bytes(post)
        with contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaisesRegex(ValueError, "outside-target"):
                verify(self.manifest, self.image, "stopped")


if __name__ == "__main__":
    unittest.main()
