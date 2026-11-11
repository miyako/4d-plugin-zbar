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
        }
        tracked = set(subprocess.check_output(
            ["git", "ls-tree", "-r", "--name-only", BASELINE], cwd=ROOT, text=True).splitlines())
        changed = set(subprocess.check_output(
            ["git", "diff", "--name-only", BASELINE, "--"], cwd=ROOT, text=True).splitlines())
        self.assertEqual((tracked & changed) - editable, set())

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
