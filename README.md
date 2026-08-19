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
- The plugin converts the picture to grayscale differently per platform (Core Graphics on macOS, GDI+ on Windows) and samples a different color channel while doing so on each platform. For ordinary black-and-white barcodes this makes no practical difference; for a heavily tinted or anti-aliased image, the two platforms could theoretically read slightly different intensity values for the same picture.
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
| PDF417 | `57` | Per the plugin's own project notes, this symbology does not reliably decode in this build — don't depend on it. |
| QR Code | `64` | |
| Code 93 | `93` | |
| Code 128 | `128` | |

These codes come from zbar's own public header. Only elements in the `types` collection that 4D reads as a numeric ("Real") value are honored — any other element kind is silently skipped, so build the collection with plain numeric literals.

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
- **PDF417 doesn't reliably decode.** This is a known limitation of the plugin as shipped, not something to troubleshoot on your end — avoid relying on it, and don't restrict a scan to code `57` expecting consistent results.
- **`confidence` is not a percentage or an accuracy score.** Treat it strictly as a relative ranking between symbols detected in the same call — a `confidence` of `40` isn't "40% sure," and isn't comparable across separate calls.
- **A value in the `types` filter seems to be ignored.** Only elements the plugin reads as a `Real` numeric value are honored; make sure the collection was built with ordinary numeric literals (e.g. `New collection(64; 13)`), not values that end up with a different internal numeric kind.
- **Results can differ subtly between macOS and Windows for the same picture.** The two platforms extract grayscale intensity from a different color channel internally. This has no practical effect on plain black-and-white barcodes, but is worth knowing if you see a platform-specific miss on a heavily tinted or anti-aliased image.
- **Large or high-resolution pictures can make a call noticeably slower, particularly on Windows.** The scan runs synchronously on the calling thread; if you routinely scan very large pictures, consider downsizing them (e.g. with `Picture` commands or an image-conversion command appropriate to your 4D version) before calling `ZBAR`.

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
