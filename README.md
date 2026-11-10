![version](https://img.shields.io/badge/version-18%2B-EB8E5F)
![platform](https://img.shields.io/static/v1?label=platform&message=mac-intel%20|%20mac-arm%20|%20win-64&color=blue)
[![license](https://img.shields.io/github/license/miyako/4d-plugin-zbar)](LICENSE)
![downloads](https://img.shields.io/github/downloads/miyako/4d-plugin-zbar/total)

# 4d-plugin-zbar

ZBAR scans a 4D `Picture` for barcodes and returns what it finds as an `Object`. Internally it decodes the picture into an 8-bit grayscale buffer and hands that buffer to the [zbar](https://github.com/mchehab/zbar) barcode-scanning library, which supports a wide range of 1D and 2D symbologies (see the type table below). The plugin exposes exactly one command, `ZBAR`.

## Summary

| Command | Returns | Purpose |
|---|---|---|
| [`ZBAR`](#zbar) | Object | Scan a picture for barcodes, optionally restricted to specific symbologies |

**Platforms:** macOS (Intel and Apple Silicon), Windows 64-bit. Requires 4D v18 or later.

---

## Requirements & platform notes

- The plugin exposes a single command, `ZBAR`. There is no separate initialization/configuration command — every call is self-contained.
- The second parameter (`types`) is optional. The plugin's own test method calls `ZBAR` with only the picture argument, and the plugin explicitly handles a missing/empty collection.
- **`success` being `false` never comes with an error message or code.** It covers three different situations identically: no picture was supplied, the picture couldn't be decoded, or the picture was read fine but no barcode was found in it. See [Error handling](#error-handling--troubleshooting).
- **Confidence values are not a percentage.** zbar itself documents this figure as an unscaled, relative quantity — only the ordering between two values is meaningful, not the absolute number.
- The plugin converts the picture to grayscale differently per platform (Core Graphics on macOS, GDI+ on Windows). Color calibration, JPEG rounding and weak grayscale edges can affect detection even when an image's red, green and blue channels are identical. Windows also has a no-match intensity retry, described below; neither conversion guarantees identical results across platforms.
- This reference documents the current, defensively-hardened version of the plugin's source. An unusually large or corrupt picture used to be able to freeze the host application; the current source guarantees a `success:false` result is always returned instead. If you're running an older compiled build, confirm it was built from the fixed source before relying on that guarantee.

---

## ZBAR

### Syntax

```
ZBAR ( picture ; types ) → Object
```

| Parameter | Type | Description |
|---|---|---|
| `picture` | Picture | The image to scan for barcodes. |
| `types` | Collection | Optional. A collection of numeric symbology codes (see table below) to restrict the scan to. When omitted, or passed as an empty collection, the plugin scans using zbar's own default set of enabled symbologies. When it contains one or more values, **only** the listed symbologies are enabled for that call — every other symbology is disabled, even ones zbar would otherwise scan for by default. |
| Result | Object | See [Result object](#result-object) below. |

### Description

`ZBAR` takes whatever picture you pass it, reduces it to an 8-bit grayscale buffer, and runs it through zbar's decoder. It returns a single `Object`:

#### Result object

| Property | Type | Description |
|---|---|---|
| `success` | Boolean | `True` if at least one barcode was decoded, `False` otherwise (see the three cases below). |
| `values` | Collection | **Only present when `success` is `True`.** A collection of barcode objects (see below), one per symbol found. Not present at all — not even as an empty collection — when `success` is `False`. |

Each object inside `values` has:

| Property | Type | Description |
|---|---|---|
| `data` | Text | The decoded barcode content. |
| `name` | Text | The symbology name (e.g. `"QR-Code"`, `"EAN-13"`), as reported by zbar. |
| `addOn` | Text | The add-on symbology name, if the barcode is a composite/add-on symbol; an empty string otherwise. |
| `confidence` | Real | zbar's own relative quality score for this detection — larger is better than smaller, but the number itself isn't calibrated to any fixed scale. Use it only to rank multiple results against each other from the same call. |

`success` is `False`, with no `values` property at all, in three distinct situations that look identical from the caller's side:

- No picture was passed, or the picture parameter was empty.
- The picture couldn't be decoded into a native bitmap at all.
- The picture decoded fine, but zbar didn't find a barcode in it.

**Filtering by symbology (`types`).** Pass a collection of one or more of the following numeric codes to restrict the scan:

| Symbology | Code | Notes |
|---|---|---|
| EAN-2 (add-on) | `2` | |
| EAN-5 (add-on) | `5` | |
| EAN-8 | `8` | |
| UPC-E | `9` | |
| ISBN-10 | `10` | Derived from an EAN-13 barcode. |
| UPC-A | `12` | |
| EAN-13 | `13` | |
| ISBN-13 | `14` | Derived from an EAN-13 barcode. |
| Composite (EAN/UPC) | `15` | |
| Interleaved 2 of 5 | `25` | |
| GS1 DataBar (RSS) | `34` | |
| GS1 DataBar Expanded | `35` | |
| Codabar | `38` | |
| Code 39 | `39` | |
| PDF417 | `57` | Not supported by the Windows scanner build; the macOS build also has a known decoding limitation. |
| QR Code | `64` | |
| Code 93 | `93` | |
| Code 128 | `128` | |

These codes come from zbar's own public header. Only elements in the `types` collection that 4D reads as a numeric ("Real") value are honored — any other element kind is silently skipped, so build the collection with plain numeric literals.

On Windows, a non-empty filter disables all decoders first, then enables only
the supported requested types. Unsupported types do not re-enable defaults:
`New collection(57)` produces `success:false` with no `values`, while
`New collection(57; 64)` can still return QR results. No new error property is
introduced for unsupported types.
UPC-A and ISBN are derived from EAN-13: to request those output forms, include
the EAN-13 base decoder as well (for example `New collection(13; 12)` for
UPC-A or `New collection(13; 10)` for ISBN-10). Enabling the derived form alone
does not enable its EAN-13 base decoder; this behavior is unchanged from the
former Windows library.

**Windows no-match retry.** The first scan still uses the original GDI+ red-channel
Y800 buffer. A successful first scan is returned unchanged. Only a normal
zero-symbol return triggers one additional scan, with the same dimensions,
scanner configuration and `types` filter. Negative scanner returns are errors,
not no-match results, and never trigger a retry.

For the second scan, Windows Imaging Component (WIC) maps neutral sRGB
intensities to an ICC v2 RGB space with sRGB primaries and a gamma 1.8 curve.
WIC converts a 256-entry intensity ramp once per process; subsequent retries
apply that lookup table to a separate Y800 buffer. There is no resize, crop,
binarization, external color library or change to zbar's edge thresholds.
This is a best-effort intensity adjustment, not a reconstruction of an image's
embedded profile or an emulation of macOS Generic RGB. Applying it unconditionally
can lose low-contrast detections, which is why the original scan always runs
first. A no-match call can take longer and may now return a newly detected symbol;
applications should continue validating decoded content as appropriate.
The macOS conversion and scan path do not use this retry.

### Example

From the plugin's own test method (`TEST.4dm`) — scans every picture file found in the current resources folder:

```4d
//%attributes = {}
$path:=Get 4D folder:C485(Current resources folder:K5:16)

DOCUMENT LIST:C474($path; $paths; Ignore invisible:K24:16 | Absolute path:K24:14)

C_OBJECT:C1216($result)

For ($i; 1; Size of array:C274($paths))
	
	$path:=$paths{$i}
	
	READ PICTURE FILE:C678($path; $image)
	
	$result:=ZBAR($image)
	
	If ($result.success=False:C215)
		
		TRACE:C157
		//pdf417 is not working
	End if 
	
End for 
```

Reading the result and looping over the barcodes found:

```4d
C_PICTURE($image)
C_OBJECT($result)

READ PICTURE FILE($path; $image)

$result:=ZBAR($image)

If ($result.success)
	
	Foreach ($barcode; $result.values)
		
		ALERT($barcode.name+": "+$barcode.data)
		
	End for each
	
End if 
```

Restricting the scan to QR codes and EAN-13 only:

```4d
C_PICTURE($image)
C_COLLECTION($types)
C_OBJECT($result)

READ PICTURE FILE($path; $image)

$types:=New collection(64; 13)  // QR Code, EAN-13

$result:=ZBAR($image; $types)

If ($result.success)
	
	ALERT($result.values[0].data)
	
End if 
```

---

## Error handling & troubleshooting

- **`success` is `False` with no `values` property at all.** This is the response for every failure/no-match case — an omitted or empty picture, an undecodable picture, and "picture decoded fine but no barcode was found" are all indistinguishable from the caller's side. Don't reference `.values` before checking `.success`.
- **Windows conversion or scanner errors.** Failed pixel reads, WIC/COM failures, allocation failures and negative scanner returns produce `success:false`, without adding an error property. Diagnostic messages are emitted to the Windows debugger output (`OutputDebugString`); they do not contain image pixels or decoded content. An ordinary no-match return does not emit an error.
- **PDF417 is not supported on Windows.** Upstream zbar's PDF417 decoder is incomplete and is disabled in this build. The macOS build also has a known PDF417 limitation. Code `57` remains in the public constants for compatibility, but cannot be used to obtain PDF417 results.
- **`confidence` is not a percentage or an accuracy score.** Treat it strictly as a relative ranking between symbols detected in the same call — a `confidence` of `40` isn't "40% sure," and isn't comparable across separate calls.
- **A value in the `types` filter seems to be ignored.** Only elements the plugin reads as a `Real` numeric value are honored; make sure the collection was built with ordinary numeric literals (e.g. `New collection(64; 13)`), not values that end up with a different internal numeric kind.
- **Results can differ between macOS and Windows for the same picture.** Their conversion and retry paths differ. Even neutral grayscale images can be sensitive to small intensity differences around barcode edges; a newer image-decoding API alone does not guarantee identical detection.
- **Large or high-resolution pictures can make a call noticeably slower, particularly on Windows.** The scan runs synchronously on the calling thread. Windows now uses standard upstream zbar without the historical custom scan yield callback. Windows schedules other OS threads, but the calling 4D thread still blocks until decoding finishes; UI responsiveness is not guaranteed by the OS or a newer 4D version. The existing per-row `PA_YieldAbsolute` calls during Windows picture conversion remain. Downsize very large pictures before scanning when appropriate.
- **Windows crashes during scanning in older builds.** The former custom Windows zbar 0.11 library required a yield callback and interval. Builds that used a two-argument declaration with that library could call an invalid callback address and crash with an execute access violation (`0xC0000005`). Version 2.2.7 restored the matching custom ABI. The current source instead builds standard zbar 0.23.95 and uses its matching two-argument header and library together. Rebuild and replace the Windows plugin; removing callback arguments while retaining the old binary library is unsafe. The macOS library is unchanged.

---

## Building on Windows

Install Visual Studio 2022 (or Build Tools) with **Desktop development with C++**,
the v143 MSVC tools, Windows 10/11 SDK, and **C++ CMake tools for Windows**
(CMake 3.24 or newer on `PATH`). PowerShell 5.1 or newer is required. Open a
Developer PowerShell/Command Prompt in this repository, then run:

```powershell
msbuild ZBAR.sln /p:Configuration=Release /p:Platform=x64 /p:WindowsTargetPlatformVersion=10.0 /m
powershell -NoProfile -File windows/build-zbar.ps1 -Configuration Release -Test
```

MSBuild automatically downloads, verifies and builds the dependencies before
compiling the plugin. No separately installed zbar, old `.lib`, or CI-only setup
is needed. The first build needs internet access. Subsequent builds reuse
`build/windows-zbar/x64`; remove that specific build directory when changing the
compiler installation or dependency pins. `Debug` uses separate installed
libraries and the debug CRT. The dependency integration also accepts `Win32` for
legacy project configurations, but supported releases and CI validate x64 only.
Output remains `test/Plugins/ZBAR.bundle/Contents/Windows64/ZBAR.4DX`.
Windows CI and release builds use the `windows-2022` runner explicitly so the
native CMake generator and v143 toolset match the documented local setup.

`windows/zbar/CMakeLists.txt` pins the [zbar 0.23.95 source archive](https://github.com/mchehab/zbar/tree/0.23.95)
and [win-iconv v0.0.11](https://github.com/win-iconv/win-iconv/tree/v0.0.11)
by version and SHA-256. Upstream currently provides Meson/MinGW build wiring,
not a native MSVC project; this small CMake integration compiles unmodified
upstream scanner/image sources with MSVC C11. Both static libraries and the
plugin use `/MT` (`/MTd` in Debug). The plugin includes the header installed from
that exact source build and links explicit paths to the matching libraries;
the macOS `include/zbar.h` and `a/libzbar.a` are untouched.

Enabled decoders are EAN/UPC/ISBN and add-ons, DataBar/Expanded, Code 128,
Code 93, Code 39, Codabar, Interleaved 2 of 5, and QR. PDF417 remains unsupported;
the additional upstream SQ Code decoder is intentionally disabled. QR text
conversion uses statically compiled win-iconv and Windows code-page APIs,
including Shift-JIS; win-iconv's optional DLL delegation is disabled. It is not
an encoding-validation library, and malformed text or uncommon code pages can
differ from GNU iconv. No ImageMagick, JPEG, camera, processor/UI bindings,
Python/Qt/GTK, or other optional library dependencies are built; 4D/GDI+ supplies
the image decoding as before.

The **Windows scanner validation** workflow runs non-release x64 Release and
Debug builds and a native executable linked to the same libraries. It checks
the two-argument declaration and linked version, representative `test/Resources`
decodes, type inclusion/exclusion, repeated calls, blank images, the PDF417
limitation, Japanese text conversion, and an 8192-by-8192 QR image. It does not
run a licensed 4D host, so the existing `TEST.4dm` remains the host-level manual
check for the command's unchanged `success`/`values` object contract.
Non-ASCII QR fixtures include UTF-8 Japanese/accented text and Shift-JIS
Japanese text, with exact expected UTF-8 output bytes. They can be regenerated
on macOS using `swift tests/windows/generate-qr.swift` (Core Image). The Release
job also builds a separate, checksum-verified legacy scanner test from commit
`e3b59753123774b7c14ceb32de4f77b78e2bcc78`, supplying a valid no-op callback
only to that historical test library. It records decoder defaults and runs
the same fixtures for migration comparison; the old library is never linked
into the modern plugin or included in release artifacts.

The modern scanner tests call the same Windows retry helper as the plugin.
They verify that existing fixture successes and 80 synthetic low-contrast
scan cases retain their first-pass results, that a public synthetic QR can be
recovered by retry, that excluded types stay excluded during retry, and that
negative scan returns are not retried. These fixtures contain no private images
or private barcode content.

The supported default enable/config flags match that historical library:
EAN-8, EAN-13, I25, DataBar/Expanded, Codabar, Code 39/93/128, and QR are enabled.
EAN-2/5, UPC-A/E, and ISBN output forms are opt-in; without a filter, UPC/ISBN
pictures are reported through the enabled EAN-13 decoder. The intentional
default difference is PDF417: the former library enabled its incomplete
decoder, whereas the modern build omits it. Both PDF417 resource samples
remain no-match, and supported-only and mixed supported/unsupported filters
retain their behavior.

### Dependency licensing

The plugin source remains MIT licensed. zbar is LGPL-2.1-or-later and win-iconv
is public domain; dependency notices are installed under
`build/windows-zbar/x64/install/<Configuration>/licenses` and included in release
bundles under `Contents/Resources/licenses`. When redistributing static builds,
retain these notices and provide the corresponding source and materials needed
to rebuild/relink with a modified zbar, as required by the LGPL. The pinned
upstream sources and this repository's plugin sources/build scripts are the
rebuild inputs; the release workflow also supplies a Windows relink archive.
That archive includes the plugin source, plugin object files, dependency
libraries/headers/notices, and both dependency source trees. To rebuild offline,
extract it, change the zbar sources as desired, and configure the dependency
build with CMake's `FETCHCONTENT_SOURCE_DIR_ZBAR_SOURCE` and
`FETCHCONTENT_SOURCE_DIR_WIN_ICONV_SOURCE` options pointing to those source trees
(absolute paths). Use the build directory and install paths described above,
then rebuild the plugin with MSBuild. Do not mix headers/libraries from the
former callback-enabled 0.11 distribution into this build.

---

## Quick reference

```4d
C_PICTURE($image)
C_OBJECT($result)

READ PICTURE FILE($path; $image)
$result:=ZBAR($image)

If ($result.success)
	Foreach ($barcode; $result.values)
		// $barcode.data / $barcode.name / $barcode.addOn / $barcode.confidence
	End for each
End if 
```

```4d
// restrict to specific symbologies
$types:=New collection(64; 13; 128)  // QR Code, EAN-13, Code 128
$result:=ZBAR($image; $types)
```
