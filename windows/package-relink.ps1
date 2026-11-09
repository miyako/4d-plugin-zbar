$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    $stage = "build/windows-relink"
    if (Test-Path "build/windows-relink") {
        Remove-Item -Recurse -Force "build/windows-relink"
    }
    New-Item -ItemType Directory -Force "$stage/plugin-source" | Out-Null
    git archive --format=zip --output=build/plugin-source.zip HEAD -- `
        "4D Plugin API" 4DPlugin-ZBAR.cpp 4DPlugin-ZBAR.h support include windows tests `
        test/Resources test/Plugins/ZBAR.bundle/Contents/Resources `
        test/Plugins/ZBAR.bundle/Contents/Info.plist ZBAR.sln ZBAR.vcxproj `
        ZBAR.vcxproj.filters README.md LICENSE .gitignore manifest.json constants.json constants.xlf
    if ($LASTEXITCODE -ne 0) { throw "Source archive failed" }
    Expand-Archive -Force build/plugin-source.zip "$stage/plugin-source"
    Copy-Item -Recurse -Force build/windows-zbar/x64/_deps/zbar_source-src "$stage/zbar-source"
    Copy-Item -Recurse -Force build/windows-zbar/x64/_deps/win_iconv_source-src "$stage/win-iconv-source"
    Copy-Item -Recurse -Force Objs/Release "$stage/plugin-objects"
    Copy-Item -Recurse -Force build/windows-zbar/x64/install/Release "$stage/dependencies"
    Compress-Archive -Force "$stage/*" "build/windows-relink.zip"
} finally {
    Pop-Location
}
