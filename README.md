# 4d-plugin-zbar
Barcode scanner based on zbar 0.11

##Platform

| carbon | cocoa | win32 | win64 |
|:------:|:-----:|:---------:|:---------:|
|🆗|🆗|🚫|🚫|

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

Value of the first found barcode is returned. ``$values``. ``$types``. ``$addonvalues`` area text arrays. ``confidences`` is a long int array.

