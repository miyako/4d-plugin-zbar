"""Preserve SDK, production build/release configuration and public surface."""

import json
from pathlib import Path
import re
import subprocess
import unittest
import xml.etree.ElementTree as ET

from test_ownership import BASELINE, ROOT, SDK


class PreservationTests(unittest.TestCase):
    def test_only_audited_baseline_sources_change(self):
        editable = {
            "4D Plugin API/Classes/ARRAY_TEXT.h", "4D Plugin API/Classes/C_BLOB.h",
            "4DPlugin-ZBAR.cpp", "support/4DPlugin-JSON.cpp", "README.md",
            ".github/workflows/windows.yml",
        }
        tracked = set(subprocess.check_output(
            ["git", "ls-tree", "-r", "--name-only", BASELINE], cwd=ROOT, text=True).splitlines())
        changed = set(subprocess.check_output(
            ["git", "diff", "--name-only", BASELINE, "--"], cwd=ROOT, text=True).splitlines())
        self.assertEqual((tracked & changed) - editable, set())

    def test_windows_exclusion_is_exact_and_preserves_every_step(self):
        baseline = subprocess.check_output(
            ["git", "show", f"{BASELINE}:.github/workflows/windows.yml"],
            cwd=ROOT, text=True)
        expected = baseline.replace(
            "branches-ignore: [master]",
            "branches-ignore: [master, miyako-zbar-ownership]",
        ).replace(
            "  windows:\n",
            "  windows:\n"
            "    if: github.head_ref != 'miyako-zbar-ownership' && "
            "github.ref != 'refs/heads/miyako-zbar-ownership'\n",
        )
        self.assertEqual((ROOT / ".github/workflows/windows.yml").read_text(), expected)
        self.assertEqual(re.findall(r"^  (\w+):$", expected.split("jobs:\n")[1], re.M),
                         ["windows"])

    def test_windows_event_matrix(self):
        workflow = (ROOT / ".github/workflows/windows.yml").read_text()
        ignored = re.search(r"branches-ignore: \[([^]]+)\]", workflow).group(1).split(", ")
        gate = re.search(r"^    if: (.+)$", workflow, re.M).group(1)
        predicates = re.findall(r"github\.(head_ref|ref) != '([^']+)'", gate)
        self.assertEqual(len(predicates), 2)
        cases = [
            ("audit-push", "push", "", "refs/heads/miyako-zbar-ownership", False),
            ("audit-pr-master", "pull_request", "miyako-zbar-ownership", "refs/pull/1/merge", False),
            ("audit-pr-other-base", "pull_request", "miyako-zbar-ownership", "refs/pull/2/merge", False),
            ("audit-manual", "workflow_dispatch", "", "refs/heads/miyako-zbar-ownership", False),
            ("other-push", "push", "", "refs/heads/feature", True),
            ("similar-push", "push", "", "refs/heads/miyako-zbar-ownership-more", True),
            ("other-pr-master", "pull_request", "feature", "refs/pull/3/merge", True),
            ("similar-pr", "pull_request", "miyako-zbar-ownership-more", "refs/pull/4/merge", True),
            ("master-merge-push", "push", "", "refs/heads/master", False),
            ("master-manual", "workflow_dispatch", "", "refs/heads/master", True),
            ("other-manual", "workflow_dispatch", "", "refs/heads/feature", True),
        ]
        for name, event, head, ref, expected in cases:
            with self.subTest(event=name):
                triggered = event != "push" or ref.removeprefix("refs/heads/") not in ignored
                context = {"head_ref": head, "ref": ref}
                admitted = all(context[key] != value for key, value in predicates)
                self.assertEqual(triggered and admitted, expected)

    def test_exact_complete_sdk_blobs(self):
        import hashlib
        for name, expected in SDK.items():
            contents = (ROOT / "4D Plugin API" / name).read_bytes()
            blob = hashlib.sha1(f"blob {len(contents)}\0".encode() + contents).hexdigest()
            with self.subTest(name=name):
                self.assertEqual(blob, expected)

    def test_public_manifest_unchanged(self):
        manifest = json.loads((ROOT / "manifest.json").read_text())
        self.assertEqual(manifest["commands"], [{
            "theme": "ZBAR", "syntax": "ZBAR(&P;&C):J", "threadSafe": True,
        }])

    def test_all_production_source_memberships(self):
        project = ET.parse(ROOT / "ZBAR.vcxproj")
        ns = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}
        self.assertEqual({
            n.attrib["Include"].replace("\\", "/")
            for n in project.findall(".//m:ClCompile[@Include]", ns)
        }, {"4D Plugin API/4DPluginAPI.c", "4DPlugin-ZBAR.cpp", "support/4DPlugin-JSON.cpp"})
        mac = (ROOT / "ZBAR.xcodeproj/project.pbxproj").read_text()
        section = mac.split("/* Begin PBXSourcesBuildPhase section */")[1].split(
            "/* End PBXSourcesBuildPhase section */")[0]
        self.assertEqual(set(re.findall(r"/\* (.*?) in Sources \*/", section)), {
            "4DPluginAPI.c", "4DPlugin-ZBAR.cpp", "4DPlugin-JSON.cpp",
        })

    def test_new_workflow_is_narrow_unsigned(self):
        workflow = (ROOT / ".github/workflows/ownership.yml").read_text()
        self.assertIn("branches: [miyako-zbar-ownership, master]", workflow)
        self.assertIn("runs-on: macos-latest", workflow)
        self.assertIn("fetch-depth: 0", workflow)
        self.assertIn("contents: read", workflow)
        self.assertIn("python3 -m unittest discover", workflow)
        for forbidden in ("pull_request:", "workflow_dispatch:", "upload-artifact",
                          "msbuild", "xcodebuild", "notary", "codesign", "pip install"):
            self.assertNotIn(forbidden, workflow)


if __name__ == "__main__":
    unittest.main()
