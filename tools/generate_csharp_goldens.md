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

## The layout files (`layout` mode) — PASSED

`dotnet run -c Release --project tools/csharp_probe -- layout` writes
`tests/goldens/layout_csharp.txt`: the bytes of `WallpaperLayout.json` and
`ScreenSaverLayout.json`, the compact JSON of a single `DisplayMonitor`, an
equality check, and a reload check. It exists because the settings fixture has a
**null** `SelectedDisplay`, so the nested display contract was never covered —
and that contract is not the obvious one (`isStale` first, rectangles as strings,
`HMonitor` as `{"value": n}`). Everything in this fixture is a literal, so it is
machine-independent with no path rewriting.

## Encrypted + rule-file persistence (`persist` mode) — PASSED

`dotnet run -c Release --project tools/csharp_probe -- persist` writes
`tests/goldens/persist_csharp.txt`: `JsonStorage<byte[]>` (the base64/JSON-string
shape, non-empty and empty), `AppRules.json`, `MusicAppExclusionRules.json`, the
plaintext that `EncryptUtil` encrypts, and a real `EncryptUtil.Store`/`Load`
round trip of `Tokens.dat`.

**This fixture is deliberately not byte-comparable in one place**, and that is the
point: a DPAPI blob carries a random key and salt, so two runs of the *C# code
itself* produce different bytes. The golden therefore pins the wrapper shape
(`quoted-string` / `single-line` — a port writing plaintext JSON fails here), the
deterministic plaintext, and the two rule files. The port exercises its own
`CryptProtectData`/`CryptUnprotectData` pair against the same transcript, which is
what proves the entropy (the app alias as UTF-8) matches.

Two divergences this pinned before they were encoded: `Expiration` must be
round-tripped through Newtonsoft's canonical `DateTime` rendering
(`…45.0000000Z` → `…45Z`, and `default(DateTime)` → `0001-01-01T00:00:00` with **no**
zone suffix), and the `Describe` helper's `…fffffffZ` format string emits `Z`
*literally* for every kind — so the MinValue store prints a trailing `Z` even
though the file it wrote has none.

The same mode also covers `theme.json` (`JsonStorage<ThemeModel>`) and
`AppThemeFactory`. Three things this pinned that were not obvious:

* `IsEditable` is `[JsonIgnore]` and must not be serialized at all.
* `Name` is a plain `string` property with no initializer, so a
  default-constructed `ThemeModel` writes `"Name": null` — unlike `AppVersion`,
  which has an initializer and is therefore never null.
* The C# **copy constructor does not assign `AppVersion`**, so it re-runs the
  field initializer and picks up the entry assembly's version.
  `AppThemeFactory` goes through that path, so an installed theme's `theme.json`
  records the *host app's* version rather than the source model's. The fixture
  masks that value (the probe's build yields `1.0.0.0`, the real app declares
  `2.2.1.5`) since it is build metadata, not behaviour; the C++ side asserts the
  mechanism directly in a `[regression]`-tagged test instead.

`AppThemeFactory`'s generated directory is `Path.GetRandomFileName()`, so it is
non-deterministic too: the fixture masks it as `<THEME_DIR>` and the port's test
substitutes its own directory. What remains compared is exactly the interesting
part — `theme.json` storing *relative* filenames while the returned model holds
the absolute path, and `CreateFromDirectory` re-rooting them on read.

The C++ side replays it in `tests/test_persist.cpp` (tagged `[persist]`, part of
the ordinary suite — no C# checkout needed).

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
