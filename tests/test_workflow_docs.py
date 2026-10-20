"""Negative checks ensure the small scaffold check catches real omissions."""
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[1]

class WorkflowDocsCheck(unittest.TestCase):
    def fixture(self, folder):
        root = Path(folder)
        for relative in ["scripts/check_workflow_docs.py", ".github/ISSUE_TEMPLATE/work_item.md", ".github/pull_request_template.md", "docs/DECISIONS.md"]:
            dest = root / relative
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(SOURCE / relative, dest)
        # Replace real index with a minimal valid fixture, excluding repository links.
        (root / "docs/DECISIONS.md").write_text("# Decisions\nhttps://github.com/example/example\n")
        return root

    def run_check(self, root):
        return subprocess.run([sys.executable, str(root / "scripts/check_workflow_docs.py")], capture_output=True, text=True)

    def test_valid_then_missing_required_prompt(self):
        with tempfile.TemporaryDirectory() as folder:
            root = self.fixture(folder)
            self.assertEqual(self.run_check(root).returncode, 0)
            (root / ".github/pull_request_template.md").unlink()
            self.assertNotEqual(self.run_check(root).returncode, 0)

    def test_broken_link_and_detached_table_row(self):
        with tempfile.TemporaryDirectory() as folder:
            root = self.fixture(folder)
            (root / "README.md").write_text("[Missing](absent.md)\n\n| 23 | detached |\n")
            result = self.run_check(root)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("missing local target", result.stderr)
            self.assertIn("detached table row", result.stderr)
