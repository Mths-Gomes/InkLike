"""Check branded manual-install copies against the canonical OTA artifacts."""

import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
with contextlib.redirect_stderr(io.StringIO()):
    RENAME = runpy.run_path(str(ROOT / 'scripts/rename_firmware.py'))


class BuildEnvironment(dict):
    def GetProjectOption(self, name):
        return self.get(name)


class InkLikeReleaseArtifactsTest(unittest.TestCase):
    def test_manual_and_ota_artifacts_have_identical_contents(self):
        for device in ('x3-x4', 'sticky', 'x4-pro', 'x4-classic'):
            with self.subTest(device=device), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                image = root / 'firmware.bin'
                payload = b'firmware-copy-check\x00\xff'
                image.write_bytes(payload)
                env = BuildEnvironment(PROJECT_DIR=str(root), PIOENV='default', custom_firmware_device_type=device)
                with patch.dict(os.environ, {'CROSSINK_RELEASE_VERSION': '1.0.0'}), contextlib.redirect_stdout(io.StringIO()):
                    RENAME['rename_firmware']([], [image], env)
                for name in (f'firmware-{device}.bin', f'firmware-{device}-v1.0.0.bin', f'InkLike-{device}-v1.0.0.bin'):
                    self.assertEqual((root / name).read_bytes(), payload)

    def test_catalog_uses_inklike_metadata_and_canonical_download(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image = root / 'firmware-x3-x4-v1.0.0.bin'
            payload = b'catalog-hash-check'
            image.write_bytes(payload)
            output = root / 'catalog'
            subprocess.run([
                sys.executable, str(ROOT / 'scripts/generate_release_catalog.py'),
                '--firmware', str(image), '--output', str(output),
                '--repo', 'Mths-Gomes/InkLike', '--version', '1.0.0',
            ], check=True, capture_output=True, text=True)
            release, = json.loads(output.read_text(encoding='utf-8'))['releases']
            self.assertEqual(release['version'], '1.0.0')
            self.assertEqual(release['variant'], 'x3-x4')
            self.assertEqual(release['notes'], 'InkLike 1.0.0 stable firmware')
            self.assertEqual(release['firmware_url'],
                             'https://github.com/Mths-Gomes/InkLike/releases/download/v1.0.0/firmware-x3-x4-v1.0.0.bin')
            self.assertEqual(release['firmware_sha256'], hashlib.sha256(payload).hexdigest())
            self.assertEqual(release['size'], len(payload))


if __name__ == '__main__':
    unittest.main()
