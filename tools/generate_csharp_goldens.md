# Generating C# golden fixtures

**Status: operational.** `tools/csharp_probe/` builds against the real
`Lively.Models` + Newtonsoft.Json and emits one `name<TAB>json` line per IPC
fixture. The captured output lives at `tests/goldens/ipc_csharp.txt`, and
`tests/test_ipc_oracle.cpp` diffs the C++ serializer against it on every test
run.

To regenerate after the C# models change:

```bash
cd "lively in C++/tools/csharp_probe"
dotnet run -c Release > ../../tests/goldens/ipc_csharp.txt
cmake --build ../../build && ctest --test-dir ../../build -C Release
```

## Fixtures captured from upstream (upstream pin: `c1036feb`)

`tests/goldens/` holds both the oracle *output* and, where needed, the *input*
that produced it. `mpv_LivelyProperties.json` + `mpv_LivelyProperties.loc.json`
are verbatim copies of
`lively in C#/src/Lively/Lively/Assets/Plugins/Mpv/` (GPL-3.0, same as this
derivative) so `tests/test_lively_props.cpp` can run in a source-only checkout
such as CI, with no C# reference tree. When the upstream assets are present the
test prefers those; the vendored copies are the fallback.

## First capture

All 17 IPC fixtures matched the C++ port
byte-for-byte — Type-first key order, ordinal enums, `null` string emission,
and Newtonsoft's raw-UTF-8 + `\"`/`\n` escaping all confirmed. One test-side
bug (a transposed unicode escape) was caught by the oracle, demonstrating the
loop works.

The same flow (run C# binary + C++ binary on identical input, byte-compare
stdout) is the differential harness described in `README.md` and applies to:
`lively_watchdog` (stdin protocol), `lively_cmd` (parse outcomes), and
`lively_console_demo` (menu flows, once phase 2 links the gRPC clients).

## The library tranche (`library` mode) — PASSED

`dotnet run -c Release --project tools/csharp_probe -- library` emits a 314-line
transcript covering LivelyInfoModel serialization, FileTypes, FileUtil, LinkUtil,
Languages, WindowClassExclusions, WallpaperExtensions, LivelyInfoUtil and
WallpaperLibraryFactory, and writes it to `tests/goldens/library_csharp.txt`.

The probe builds its own fixture tree under `%TEMP%\lively_library_probe` and
relativizes every path to it (`<root>`) plus `%LOCALAPPDATA%` (`<lap>`), so the
golden carries no machine-specific text and is stable across machines. Two
dependencies are intentional:

* **SharpZipLib 1.4.2** (same version as Lively.Common) writes the package
  fixtures, so the port's own central-directory reader is compared against the
  library upstream really uses — including the case-insensitive entry lookup
  (`livelyinfo.JSON` is found) and the fact that a *nested* `sub/LivelyInfo.json`
  is not (`nested` → `false`).
* **Invariant culture**, because `FileUtil.SizeSuffix` formats with `"{0:n1}"`,
  which is culture-sensitive. The port documents that deviation at the function.

The C++ side replays the same transcript (`tests/test_library.cpp`) and is part of
the ordinary suite, so it runs in CI without a C# checkout. The zip fixtures it
uses under `tests/goldens/fixtures/` are committed (built with python's `zipfile`,
same entry names).

## Differential fuzzing (CommandLineParser) — PASSED

`tools/fuzz_differential.py` drives the *real* CommandLineParser 2.9.1 (via
`csharp_probe fuzz`, which references the same NuGet package Lively uses)
against `lively_fuzz_target.exe`. Final result: **3,500 cases across three
seeds — 0 divergences** after re-implementing the parser from the v2.9.1
sources (Group(2) scalar chunking, `explicitlyAssigned` semantics,
`bool?` = Scalar not Switch, deduped unknown-option errors, stable
canonical error ordering).

## Known equivalence risks to probe first

* Newtonsoft emits `1.5f`-derived doubles identically? (C# `double` here, low risk)
* Non-ASCII strings (wallpaper names) — Newtonsoft escapes per its own table;
  verify against `dump()` output for CJK paths.
* `long Hwnd` beyond 2^53: JSON numbers round-trip through parsers; the wire
  bytes must stay integer-literal identical.
