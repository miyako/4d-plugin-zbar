# Native string ownership audit

## Provenance and scope

Baseline: `c2b188b97f06405c159916c752fdf165dd4705c1` on `master`.
Canonical guidance read through the GitHub contents API at
`miyako/skills@e6b492fcb81e5e1a7f7db723594b420e63bf6eb9`,
`4d-skills/skills/4dplugin/SKILL.md`.
Issue: #4. No SDK upgrade, engine ABI/header rewrite, project, manifest,
version, packaged binary or existing release-workflow changes.

The bundled SDK implementation calls itself v14, while its public headers
call themselves v13; neither is an independently verified upstream release.
Its exact Git blobs, also asserted before every compilation:

| SDK file | Baseline blob |
|---|---|
| `4DPluginAPI.c` | `62f745e91d81ef2c3c901c656ea8e175d3b173e8` |
| `4DPluginAPI.h` | `528bf1d2552df08d6a8b1bc95011a9b72934ad5c` |
| `4DPluginAPI.def` | `5bbdb82fb724b6f8e307feb01d9f158b802de89d` |
| `EntryPoints.h` | `da428d5f6ca23b0a30371c7288a01d29aa3c466a` |
| `Flags.h` | `7a5a35539f27430a3fd9cb711d6b789051316471` |
| `PrivateTypes.h` | `a3098cb662cdbc43ba574fdef72a059c4c6d1cd4` |
| `PublicTypes.h` | `9ae20b001bff9128f690fff3e6c3f873b6238333` |

The immutable Window Style audit at
`miyako/4d-plugin-window-style@b4188f8b5ef1c0dc652ea3f3f3bb103b10d09d13`
was read via API for isolation-runner and synthetic-host prior art. The
SDK blob identities match. This harness, fixtures, dispatcher, counts and
baseline failures are Zbar-specific, not copied results.

## Complete native surface inventory

Both `ZBAR.vcxproj` (all four configurations) and the Xcode Sources phase
compile exactly the complete `4DPluginAPI.c`, `support/4DPlugin-JSON.cpp`
and `4DPlugin-ZBAR.cpp`. Neither builds the Classes wrappers. The unsigned
harness deliberately compiles all of them, including dormant code.

| Surface | Ownership and reachability |
|---|---|
| `4DPlugin-ZBAR.{cpp,h}` (cpp blob `7a73f3c534894aba91002436a44dcd17d8ede254`) | Actual `PluginMain` dispatches selector 1. Manifest `ZBAR(&P;&C):J` has picture/collection inputs and object output, not a text-array export. Picture and filter parameters are borrowed; native image is separately owned. Filter getter results own their content. Barcode text is copied before the scanner/image is destroyed. Result object's ownership transfers through `PA_ReturnObject`. |
| `support/4DPlugin-JSON.{cpp,h}` (cpp blob `74c880fb70dccb2d707fc8372880ff5c3492de41`) | All setters/getters, `json_wconv`, and `ob_stringify` inspected. Live dispatcher uses narrow string, Boolean, number and collection setters. Remaining helpers are dormant but compiled and regression-exercised. Object/collection/picture setters consume the supplied independent owner through their temporary; they do not retain borrowed inputs. Matching collection getter hands its owner to its caller. |
| `Classes/ARRAY_TEXT.{cpp,h}` (cpp `534855e0959020ce9a66cef08f8434799c174495`, h `cf8a458fc8519b824c726a59d346c44d4c26e845`) | Dormant text-array wrapper. Copies borrowed UTF-16 elements, including element zero, to independent standard strings before mutation. Successful exports create distinct strings and transfer them without post-insertion disposal. Owns heap vector; implicit copies were unsafe. |
| `Classes/C_TEXT.{cpp,h}` (cpp `45ea6432dff52b681a5c6a7cea58ad5fc15df66b`, h `6967390757db569ffcd3cfea8696bf14045cc748`) | Dormant scalar wrapper. Independent standard string; parameter/return writes use engine content setter, not array transfer. Existing deleted copy operations are the minimal noncopy prior art. |
| `Classes/C_BLOB.{cpp,h}` (cpp `27bea3172ce90e4da339991c9b318a71ea8609b2`, h `d181534e5a2e9bd46de492a040c1538ed81bf45a`) | Dormant CBytes/C_BLOB wrappers. CBytes stores vector by value; C_BLOB owns its heap CBytes. Hex/base64 helpers route through C_TEXT's converters. Implicit C_BLOB copies were unsafe. Handle allocation-failure behavior is outside the safe string fix. |
| `Classes/C_LONGINT.{cpp,h}`, `Classes/C_TYPES.h` | Dormant value-only scalar and standard-string/vector typedefs. No PA string owner. C_LONGINT remains copyable. |
| Full SDK and its `.def`/headers/Readme | Complete implementation compiled unchanged, not extracted functions or a substitute SDK. `.def` exports `FourDPackex`; it reaches actual PluginMain. All invoked engine selectors are explicitly allowed by the synthetic host; unexpected selectors terminate the test. |
| `windows/scanner.h` (`86495f1cd09f6e72347371380fc41967b759d777`) | Live Windows-only WIC retry and decoder helper. No PA string owners. Retry pixels become zbar-owned via cleanup callback; original grayscale buffer stays borrowed. WIC/COM and real decoding are not exercised by this audit. Actual dispatcher decode boundary is stubbed for isolation. |
| `include/zbar.h`, all nine `include/zbar/*.h` | Bundled C API and inline third-party C++ wrappers (Decoder, Exception, Image, ImageScanner, Processor, Scanner, Symbol, Video, Window). Symbols cache content by `std::string` value; symbol/set iterators explicitly retain/release zbar references. No PA string descriptors cross these wrappers. Plugin calls the C API directly; camera/video/window/processor bindings are not instantiated. This is not a general third-party-library ownership certification. |
| All eleven `include/json/*.h`, `a/libjsoncpp.a` | Bundled JsonCpp declarations/inline helpers own standard strings/JSON values, not PA descriptors. Not included or linked by the production source memberships examined. Not executed. |
| `tests/windows/scanner-test.cpp`, `generate-qr.swift`, legacy CMake target | Standalone decoder/fixture maintenance, not a 4D entry point. Read-only inspection; no fixture files, codecs, WIC, filesystem image loads, 4D databases, real devices or applications were used for this audit. |
| Tracked libraries, `.bundle`, `.4DX`, zip/dmg, test project/data/resources | Preserved; not loaded, rebuilt, repackaged or opened. No additional repository checkout/project was created. |

No `CPicture`, pointer-reference constructor, `createCopy` converter or
`PA_ConvertFromUTF8` implementation is present in these wrappers. Consequently
the analogous picture double-cleanup and bytes-versus-UTF16-units issues in
other repositories are not asserted for this target.

## Exact SDK contracts and fixes

`PA_CreateVariable(eVK_Unistring)` allocates an engine empty using
`PA_CreateUnistring` (SDK lines 4029-4030). `PA_SetStringVariable`
(4254-4259) shallow-overwrites its descriptor. Four JSON string setter
overloads now initialize Undefined instead, avoiding the lost empty owner
while preserving the independently allocated value and final variable clear.
The same allocation-free setup removes the overwritten empty picture in
`ob_set_p`, and the overwritten array handle/element-zero object in the
live barcode-result temporary.

`PA_GetObjectProperty` (637-653) and `PA_GetCollectionElement` (588-603)
explicitly return owning variables requiring clear. JSON getters and the
live filter loop now clear every successful consumed result, including
ignored kinds. `ob_get_c` deliberately does not clear a matching collection:
the returned reference is the independent owner passed to its caller.
Object/collection setters still clear their temporary after the synchronous
property assignment; successful property/element writes preserve the stored
reference under the synthetic host contract.

Errors are captured immediately after getter/command calls, before kind
normalization, adoption, or cleanup. Failed return bytes are unspecified and
are neither read nor cleared. In this SDK raw fType zero normalizes as text;
zero-filled failure is not an owned empty string. Poisoned failed kinds also
produce baseline UBSan enum failures. Successful mismatched callback returns
are cleared normally.

`ob_stringify` executes the same OB Copy command (1225) directly instead of
losing the raw owning result inside `PA_DuplicateObject`. It validates the
successful result before passing its owner to synchronous JSON Stringify
(1217), snapshots the stringify error before clearing the independent
argument, then consumes/clears only a successful result. Caller input is
borrowed and remains alive. The existing failure behavior remains unchanged
output/default return with no new error channel; this is a documented
diagnostic gap, not a successful serialization fallback.

`PA_SetStringInArray` (4933-4947) accepts only Unicode arrays with non-null
data, `0 <= index <= count`, and a non-null descriptor pointer. On acceptance
it disposes the old element and shallow-transfers the new descriptor.
Rejection retains caller ownership. `PA_GetStringInArray` (4738-4755) is a
borrowed alias. The harness verifies all rejection predicates, accepted
index zero, independent elements, replacement after churn, resize and final
clear. No blanket disposal removal was made.

ARRAY_TEXT and C_BLOB now delete only copy construction/assignment, matching
C_TEXT. Each owns a raw heap pointer with a deleting destructor. The exact
baseline's four explicit copy/assignment probes report ASan
heap-use-after-free; fixed traits reject all four. There are no production
copy callers, and these wrappers are not production build members: these
are proven latent defects, not evidence of a current 4D crash.

## Reproducible evidence

Run on macOS with clang++, Python 3.12+ and its standard library:

```sh
python3 -m unittest discover -s .github/tests -p 'test_ownership*.py' -v
```

The actual complete SDK, all four wrappers, JSON helpers and dispatcher
compile together under ASan/UBSan, with frame pointers. Alignment sanitizer
is excluded because the unchanged SDK ABI packs structures to two bytes;
other undefined-behavior checks remain enabled. LeakSanitizer is disabled
on macOS, so explicit string/picture/handle/object/collection/platform/decoder
owner ledgers must all reach zero.

Every graphics/image/path boundary is synthetic before compilation; path
calls terminate immediately. CoreFoundation text conversion is also
synthetic, retaining exact input byte lengths, UTF-16 code-unit lengths,
capacity units, and terminator boundaries. The decoder accepts only a
synthetic in-memory 2x2 grayscale image and returns two synthetic symbols.
Unexpected engine selectors fail. `nm -u` import gating rejects real
graphics/path/text conversion, zbar, device, file, process, network and
dispatch operations before any test case executes. Foundation text-only
NSString/NSMutableString and NSObject imports support dormant Objective-C
method compilation; no application, device or private data is accessed.

Actual Windows conditional consumers also compile with synthetic GDI+,
conversion and decoder declarations. This is syntax coverage only, using
the Mac SDK types, not Windows execution, native engine ABI validation, WIC
retry coverage or a reproduction of a Windows 4D host crash.

The same final harness runs against a source-only `git archive` of the
immutable baseline, without binaries, data or image fixtures:

| Group | Baseline failures | Baseline passing controls |
|---|---:|---:|
| Four string setters | 20 | 20 |
| Picture setter | 3 | 0 |
| Four getters | 44 | 36 |
| Stringify | 10 | 13 |
| Actual dispatcher success/filter cases | 22 | 0 |
| No-picture, empty/throwing decode, native-null | 2 | 2 |
| Array transfer/rejection, scalar/blob wrappers | 0 | 24 |
| SDK failure predicates, null/value controls | 0 | 3 |
| Fixed copy traits / baseline-only ASan copies | 5 | 0 |
| **Total** | **106** | **98** |

Fixed source: **200/200 named leaves pass**. Baseline: **204 leaves,
106 expected failures, 98 controls**. Four additional leaves are deliberately
baseline-only unsafe copies. Each baseline leaf's expected pass/fail is
asserted, not merely a nonzero process exit. Cases cover ASCII, BMP,
supplementary characters, empty strings, allocated/null engine empties,
successful wrong-kind owning returns, poisoned/zeroed failures, setter
rejection, callback allocation failure, matching collection handoff, scalar
and array controls, actual result object shapes and exception cleanup.
Five preservation tests separately protect SDK blobs, public manifest,
all production build memberships, baseline protected paths and the narrow
new workflow. Temporary unsigned executables/archives are automatically
cleaned; optional JSON/log outputs can be retained as session artifacts.
Local evidence was recorded with Python 3.12.12 and Apple clang 21.0.0
(`clang-2100.1.1.101`), arm64-apple-darwin25.6.0. CI explicitly selects
Python 3.12 and installs no Python packages.

## Concrete limits and release safety

* SDK `PA_ResizeArray` writes the requested count even when handle resizing
  fails (line 4603). The failure-predicate control proves the mismatch;
  it repairs only its synthetic test descriptor before cleanup. Dormant
  ARRAY_TEXT still has unsafe SDK OOM/failed-resize paths, including rejected
  insertion cleanup when no array data exists. A preservation-safe recovery
  contract has not been established; SDK edits are forbidden. Successful
  tests do not certify OOM safety.
* Dormant CBytes output replaces/disposes the old handle before checking
  allocation success; no broad handle-failure rewrite is included.
  Exception-time allocation failures in existing helper conversions also
  are not certified by normal-allocation owner tests.
* The supplied SDK's signed-char eVK_Null normalization can trip UBSan when
  routed through PA_GetVariableKind. Value controls compare its raw null tag
  instead. SDK enum/ABI fixes are outside scope.
* `ob_set_p/o/c` retain their existing consuming-input behavior. A borrowed
  picture/object/collection must not be handed to an independently cleared
  temporary without first obtaining an independent owner. No production
  caller of ob_set_p exists in this target.
* Every existing workflow, build script, MSBuild target/CMake file, Xcode
  phase and shared scheme was inspected. Xcode Deployment invokes signing,
  DMG/ZIP packaging and notarization; the shared launch/profile scheme uses
  Deployment. MSBuild invokes dependency downloads/build/install.
* Existing `windows.yml` runs on every non-master branch push and every PR,
  builds the plugin, uses filesystem/image-codec fixture operations,
  packages relink materials and uploads binaries/archives. No push or PR
  is authorized by the isolated-test permission alone. Explicit coordinator
  or user approval is required before triggering that workflow; existing
  release/project files are not changed to bypass it.
* Existing `release.yml` is tags/manual only; `bump-version.yml` is manual.
  Neither is dispatched. The new ownership workflow is read-only,
  unsigned, standard-library-only, and limited to relevant paths and the
  exact audit branch/master push events, with full history for baseline
  preservation checks. No workflow rerun, signing, packaging, release,
  binary publication, device use or real platform operation is part of
  this audit.
