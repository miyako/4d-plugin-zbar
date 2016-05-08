# 4d-plugin-zbar
Barcode scanner based on zbar 0.11

##Platform

| carbon | cocoa | win32 | win64 |
|:------:|:-----:|:---------:|:---------:|
|🆗|🆗|🆗|🆗|

Supported barcode types

```
@since 0.11: CODE93, CODABAR, DATABAR
@since 0.10: QRCODE
@since 0.4: ISBN10, ISBN13, I25, CODE39
@since 0.1: EAN2, EAN5, EAN8, UPCE, UPCA, EAN13, COMPOSITE, CODE128
```

Commands
---
```
// --- ZBAR
ZBAR
```

Examples
---
```
READ PICTURE FILE(Get 4D folder(Current resources folder)+$1+".png";$p)
$code:=ZBAR ($p;$values;$types;$addonvalues;$confidences)
```

Value of the first found barcode is returned. ``$values``, ``$types``, ``$addonvalues`` area text arrays. ``confidences`` is a long int array.

