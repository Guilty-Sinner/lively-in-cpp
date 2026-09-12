# Runs the Catch2 suite.

# Why this script: `lively_tests.exe` links runtime DLLs from MSYS2 UCRT64
# (gRPC/absl/onnxruntime). Those DLLs pull in Windows *API set* forwarders
# (e.g. api-ms-win-crt-utility-l1-1-0.dll) which the Windows loader resolves
# from the OS API-set schema. MSYS2/Cygwin's own PE dependency pre-check does
# not understand API sets and refuses to exec the binary with
# "error while loading shared libraries: api-ms-win-crt-utility-l1-1-0.dll".
# Launching through the Windows loader (PowerShell/CMD) avoids that false negative.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools/run_tests.ps1 [-Filter '[gallery]']

param(
    [string]$Filter = "",
    [string]$BuildDir = "",
    [switch]$ListOnly
)

$ErrorActionPreference = "Continue"
$root = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) {
    $BuildDir = Join-Path $root "build"
} elseif (-not [System.IO.Path]::IsPathRooted($BuildDir)) {
    # A relative build dir is relative to the repo root, not the caller's cwd,
    # so `& $exe` still resolves after Push-Location.
    $BuildDir = Join-Path $root $BuildDir
}
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path

$exe = Join-Path $BuildDir "lively_tests.exe"
if (-not (Test-Path $exe)) {
    Write-Error "test binary not found: $exe (build first)"
    exit 2
}

# Runtime DLL search path: MSYS2 UCRT64 (gRPC, onnxruntime) first, then the
# WinLibs MinGW runtime used to compile the code under test.
$dllDirs = @(
    "C:\msys64\ucrt64\bin",
    (Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin")
)
foreach ($d in $dllDirs) {
    if (Test-Path $d) { $env:PATH = "$d;$env:PATH" }
}

Push-Location $BuildDir
try {
    if ($ListOnly) {
        & $exe --list-tests
    } elseif ($Filter) {
        & $exe $Filter
    } else {
        & $exe
    }
    $code = $LASTEXITCODE
} finally {
    Pop-Location
}

if ($code -ne 0) {
    Write-Host "TESTS FAILED (exit $code)" -ForegroundColor Red
}
exit $code
