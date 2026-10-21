"""Runs the BardBox firmware host tests (simulated values, no hardware)."""
import os
import shutil
import subprocess
import unittest
from pathlib import Path

FIRMWARE = Path(__file__).resolve().parents[1] / "software" / "firmware"


@unittest.skipUnless(shutil.which(os.environ.get("CXX", "c++")), "no C++ compiler available")
class FirmwareHostTests(unittest.TestCase):
    def run_script(self, name):
        return subprocess.run(["sh", f"test/host/{name}"], cwd=FIRMWARE,
                              capture_output=True, text=True, timeout=300)

    def test_module_checks_and_version_guard(self):
        run = self.run_script("run_host_tests.sh")
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn("all checks passed", run.stdout)
        self.assertIn("version guard", run.stdout)

    def test_host_demo_shows_records_and_live_cache(self):
        run = self.run_script("run_host_demo.sh")
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn("< ERR SAMPLE_NOT_READY", run.stdout)
        self.assertIn("< DAT,v3,1,", run.stdout)
        self.assertIn("acquired=unknown", run.stdout)


if __name__ == "__main__":
    unittest.main()
