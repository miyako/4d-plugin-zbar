"""Replay the identical final harness on an immutable source-only git archive."""

import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest

from test_ownership import BASELINE, ROOT


class BaselineTests(unittest.TestCase):
    def test_immutable_baseline(self):
        with tempfile.TemporaryDirectory(prefix="zbar-ownership-baseline-") as directory:
            target = Path(directory)
            archive = subprocess.check_output([
                "git", "archive", BASELINE, "4D Plugin API", "support",
                "4DPlugin-ZBAR.cpp", "4DPlugin-ZBAR.h", "include", "windows/scanner.h",
            ], cwd=ROOT)
            with tarfile.open(fileobj=io.BytesIO(archive)) as source:
                source.extractall(target, filter="data")
            results = target / "results.json"
            env = dict(os.environ, OWNERSHIP_SOURCE_ROOT=str(target),
                       OWNERSHIP_BASELINE_COPY="1", OWNERSHIP_RESULTS=str(results))
            result = subprocess.run(
                [sys.executable, str(ROOT / ".github/tests/test_ownership.py")],
                env=env, capture_output=True, text=True,
            )
            if not results.exists():
                self.fail(result.stdout + result.stderr)
            leaves = json.loads(results.read_text())
            output = os.environ.get("OWNERSHIP_BASELINE_RESULTS")
            if output:
                Path(output).write_text(json.dumps(leaves, indent=2))
                Path(output).with_suffix(".log").write_text(result.stdout + result.stderr)
            failures = [r for r in leaves if not r["passed"]]
            controls = [r for r in leaves if r["passed"]]
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(len(leaves), 204)
            self.assertEqual(len(failures), 106)
            self.assertEqual(len(controls), 98)
            for case in leaves:
                name, kind = case["name"], case["kind"]
                allocated_text = case["empty"] == "allocated" or bool(case["text"])
                expected_failure = (
                    name in {"copy-array", "assign-array", "copy-blob", "assign-blob",
                             "copy-traits", "set-picture", "dispatch", "decode-empty", "decode-throws"}
                    or (name in {"set-narrow", "set-wide", "set-wchars", "set-utf16"}
                        and case["empty"] == "allocated")
                    or (name.startswith("get-") and (
                        (kind == "string" and allocated_text)
                        or kind in {"picture", "object", "error-poison"}
                        or (kind == "collection" and name != "get-c")
                        or (kind == "error-kind0" and name == "get-a")))
                    or (name == "stringify" and (kind.startswith("error-") or kind.startswith("copy-")))
                )
                self.assertEqual(not case["passed"], expected_failure, case)
            for name in ("copy-array", "assign-array", "copy-blob", "assign-blob"):
                case = next(r for r in failures if r["name"] == name)
                self.assertIn("AddressSanitizer: heap-use-after-free", case["output"])
            self.assertTrue(any(r["name"] == "dispatch" for r in failures))
            self.assertTrue(any(r["name"] == "set-narrow" and "string owners leaked" in r["output"] for r in failures))
            self.assertTrue(any(r["name"] == "get-a" for r in failures))
            self.assertTrue(any(r["name"] == "rejections" for r in controls))
            print(f"BASELINE leaves={len(leaves)} failures={len(failures)} controls={len(controls)}")


if __name__ == "__main__":
    unittest.main()
