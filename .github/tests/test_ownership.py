"""Unsigned actual-source ownership tests; standard library only, macOS clang."""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(os.environ.get("OWNERSHIP_SOURCE_ROOT", ROOT)).resolve()
SDK = {
    "4DPluginAPI.c": "62f745e91d81ef2c3c901c656ea8e175d3b173e8",
    "4DPluginAPI.h": "528bf1d2552df08d6a8b1bc95011a9b72934ad5c",
    "4DPluginAPI.def": "5bbdb82fb724b6f8e307feb01d9f158b802de89d",
    "EntryPoints.h": "da428d5f6ca23b0a30371c7288a01d29aa3c466a",
    "Flags.h": "7a5a35539f27430a3fd9cb711d6b789051316471",
    "PrivateTypes.h": "a3098cb662cdbc43ba574fdef72a059c4c6d1cd4",
    "PublicTypes.h": "9ae20b001bff9128f690fff3e6c3f873b6238333",
}
BASELINE = "c2b188b97f06405c159916c752fdf165dd4705c1"


@unittest.skipUnless(sys.platform == "darwin", "macOS synthetic host")
class OwnershipTests(unittest.TestCase):
    results = []

    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="zbar-ownership-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binary = Path(cls.temp.name) / "ownership"
        api = SOURCE / "4D Plugin API"
        for name, expected in SDK.items():
            contents = (api / name).read_bytes()
            blob = hashlib.sha1(f"blob {len(contents)}\0".encode() + contents).hexdigest()
            if blob != expected:
                raise AssertionError(f"Re-audit changed SDK: {name}: {blob}")
        include = [
            "-I", str(api), "-I", str(api / "Classes"), "-I", str(SOURCE / "support"),
            "-I", str(SOURCE / "include"), "-I", str(SOURCE),
        ]
        consumers = [
            *map(str, sorted((api / "Classes").glob("*.cpp"))),
            str(SOURCE / "support/4DPlugin-JSON.cpp"), str(SOURCE / "4DPlugin-ZBAR.cpp"),
        ]
        common = [
            "clang++", "-std=c++14", "-x", "objective-c++", "-fblocks",
            "-include", str(ROOT / ".github/tests/ownership_platform.h"), *include,
            "-Wno-deprecated-declarations", "-Wno-return-type-c-linkage",
            "-Wno-ignored-attributes", "-Wno-writable-strings",
        ]
        copy = ["-DOWNERSHIP_BASELINE_COPY"] if os.environ.get("OWNERSHIP_BASELINE_COPY") else []
        result = subprocess.run([
            *common, *copy, "-fsanitize=address,undefined", "-fno-sanitize=alignment",
            "-fno-omit-frame-pointer", "-g", str(api / "4DPluginAPI.c"),
            *consumers, str(ROOT / ".github/tests/ownership_host.mm"),
            "-framework", "Cocoa", "-o", str(cls.binary),
        ], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        # The production source uses a Windows backslash include. This isolated
        # boundary shim substitutes only the decoder, not a plugin/SDK source.
        (Path(cls.temp.name) / "windows\\scanner.h").write_text(
            "#ifndef ZBAR_WINDOWS_SCANNER_H\n#error Missing synthetic scanner gate\n#endif\n")
        windows = subprocess.run([
            *common, "-include", str(ROOT / ".github/tests/ownership_windows.h"),
            "-I", cls.temp.name, "-fsyntax-only", *consumers,
        ], capture_output=True, text=True)
        if windows.returncode:
            raise AssertionError(windows.stdout + windows.stderr)
        imports = subprocess.check_output(["nm", "-u", str(cls.binary)], text=True)
        if os.environ.get("OWNERSHIP_IMPORTS"):
            Path(os.environ["OWNERSHIP_IMPORTS"]).write_text(imports)
        linked = {line.split()[-1] for line in imports.splitlines() if line.split()}
        allowed_classes = {"_OBJC_CLASS_$_NSObject", "_OBJC_CLASS_$_NSString", "_OBJC_CLASS_$_NSMutableString"}
        denied = {n for n in linked if n.startswith("_OBJC_CLASS_$_") and n not in allowed_classes}
        denied |= {n for n in linked if n.startswith((
            "_CFURL", "_CFString", "_CFRelease", "_CG", "_AX", "_IO", "_dispatch_", "_zbar_",
        ))}
        denied |= {
            "_system", "_popen", "_execve", "_posix_spawn", "_kill", "_NSApp",
            "_open", "_fopen", "_socket", "_connect", "_getenv", "_remove", "_unlink",
            "_rename", "_rmdir", "_opendir", "_readdir", "_stat", "_lstat",
        } & linked
        runtime = allowed_classes | {
            "_OBJC_METACLASS_$_NSObject", "__DefaultRuneLocale", "__Unwind_Resume",
            "___CFConstantStringClassReference", "___gxx_personality_v0", "___maskrune",
            "___stack_chk_fail", "___stack_chk_guard", "__objc_empty_cache",
            "__tlv_bootstrap", "_calloc", "_exit", "_free", "_memchr", "_memcmp",
            "_memcpy", "_memmove", "_memset", "_realloc", "_sprintf", "_strlen",
            "_wcslen", "_objc_alloc", "_objc_msgSend", "_objc_release",
            "_kCFAllocatorDefault", "_kCGColorSpaceGenericRGB",
            "__ZdaPv", "__ZdlPv", "__Znam", "__Znwm", "__ZSt9terminatev",
        }
        runtime_prefixes = (
            "___asan_", "___ubsan_", "___cxa_", "___sanitizer_",
            "__ZNSt3__1", "__ZNKSt3__1", "__ZTI", "__ZTV",
            "__ZNSt11logic_error", "__ZNSt11range_error", "__ZNSt12length_error",
            "__ZNSt12out_of_range", "__ZNSt13runtime_error", "__ZNSt20bad_array_new_length",
        )
        denied |= {n for n in linked if n not in runtime and not n.startswith(runtime_prefixes)}
        denied |= {n for n in linked if any(word in n for word in (
            "filesystem", "basic_fstream", "basic_ifstream", "basic_ofstream",
        ))}
        if denied:
            raise AssertionError(f"Real platform boundary linked: {denied}")

    @classmethod
    def tearDownClass(cls):
        passes = sum(r["passed"] for r in cls.results)
        expected = 204 if os.environ.get("OWNERSHIP_BASELINE_COPY") else 200
        if len(cls.results) != expected:
            raise AssertionError(f"Expected {expected} named leaves, got {len(cls.results)}")
        print(f"LEAF runs={len(cls.results)} passed={passes} failed={len(cls.results) - passes}",
              file=sys.stderr)
        if os.environ.get("OWNERSHIP_RESULTS"):
            Path(os.environ["OWNERSHIP_RESULTS"]).write_text(json.dumps(cls.results, indent=2))

    def case(self, name, text="ASCII", empty="allocated", kind="string"):
        result = subprocess.run(
            [str(self.binary), name, text, empty, kind],
            env={"ASAN_OPTIONS": "detect_leaks=0", "UBSAN_OPTIONS": "halt_on_error=1"},
            capture_output=True, text=True,
        )
        type(self).results.append({
            "name": name, "text": text, "empty": empty, "kind": kind,
            "passed": result.returncode == 0, "output": result.stdout + result.stderr,
        })
        self.assertEqual(result.returncode, 0,
                         f"{name}/{text}/{empty}/{kind}: {result.stdout}{result.stderr}")
        self.assertIn("strings=0 pictures=0 handles=0 references=0 platform=0 decoder=0", result.stdout)

    def test_string_setters(self):
        for name in ("set-narrow", "set-wide", "set-wchars", "set-utf16"):
            for text in ("ASCII", "\u65e5", "\U0001f600", ""):
                for empty in ("allocated", "null"):
                    with self.subTest(name=name, text=text, empty=empty):
                        self.case(name, text, empty)
            for empty in ("allocated", "null"):
                with self.subTest(name=name, empty=empty, kind="set-error"):
                    self.case(name, empty=empty, kind="set-error")
        for kind in ("string", "set-error"):
            with self.subTest(name="set-picture", kind=kind):
                self.case("set-picture", kind=kind)
        with self.subTest(name="set-picture", empty="null"):
            self.case("set-picture", empty="null")

    def test_getters(self):
        for name in ("get-a", "get-b", "get-n", "get-c"):
            for kind in ("string", "picture", "object", "collection", "boolean", "real",
                         "error-poison", "error-kind0", "absent"):
                for empty in ("allocated", "null"):
                    with self.subTest(name=name, kind=kind, empty=empty):
                        self.case(name, "\u65e5\U0001f600", empty, kind)
            for empty in ("allocated", "null"):
                with self.subTest(name=name, empty=empty):
                    self.case(name, "", empty)

    def test_stringify(self):
        for text in ("ASCII", "\u65e5", "\U0001f600", ""):
            for empty in ("allocated", "null"):
                with self.subTest(text=text, empty=empty):
                    self.case("stringify", text, empty)
        for kind in ("picture", "object", "collection", "boolean", "real",
                     "error-poison", "error-kind0", "error-copy", "copy-string",
                     "copy-picture", "copy-collection", "copy-boolean", "copy-real",
                     "error-first-handle", "error-handle"):
            with self.subTest(kind=kind):
                self.case("stringify", kind=kind)

    def test_actual_dispatch_and_wrappers(self):
        for text in ("ASCII", "\u65e5", "\U0001f600", ""):
            for empty in ("allocated", "null"):
                for name in ("array", "wrappers", "rejections", "dispatch"):
                    with self.subTest(name=name, text=text, empty=empty):
                        self.case(name, text, empty)
        for kind in ("picture", "object", "collection", "boolean", "real",
                     "error-poison", "error-kind0"):
            for empty in ("allocated", "null"):
                with self.subTest(kind=kind, empty=empty):
                    self.case("dispatch", empty=empty, kind=kind)
        for name in ("no-picture", "decode-empty", "decode-throws", "native-null",
                     "sdk-failures", "copy-traits", "null-controls", "values"):
            with self.subTest(name=name):
                self.case(name)

    @unittest.skipUnless(os.environ.get("OWNERSHIP_BASELINE_COPY"), "baseline-only unsafe copies")
    def test_baseline_copy_sanitizer(self):
        for name in ("copy-array", "assign-array", "copy-blob", "assign-blob"):
            with self.subTest(name=name):
                self.case(name)


if __name__ == "__main__":
    unittest.main()
