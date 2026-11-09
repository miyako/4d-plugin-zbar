param(
    [ValidateSet("x64", "Win32")][string]$Platform = "x64",
    [ValidateSet("Release", "Debug")][string]$Configuration = "Release",
    [string]$Toolset = "v143",
    [switch]$Test
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root "build/windows-zbar/$Platform"
$install = Join-Path $build "install/$Configuration"

function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Program failed with exit code $LASTEXITCODE"
    }
}

Invoke-Checked cmake @("-S", "$PSScriptRoot/zbar", "-B", $build,
    "-G", "Visual Studio 17 2022", "-A", $Platform, "-T", $Toolset,
    "-DCMAKE_INSTALL_PREFIX=$install")
Invoke-Checked cmake @("--build", $build, "--config", $Configuration, "--parallel")
Invoke-Checked cmake @("--install", $build, "--config", $Configuration, "--prefix", $install)
if ($Test) {
    Invoke-Checked ctest @("--test-dir", $build, "-C", $Configuration, "--verbose", "--output-on-failure")
}
