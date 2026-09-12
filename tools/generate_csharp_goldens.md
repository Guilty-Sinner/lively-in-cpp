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

First capture (2026-09): **all 17 fixtures matched the C++ port
byte-for-byte** — Type-first key order, ordinal enums, `null` string emission,
and Newtonsoft's raw-UTF-8 + `\"`/`\n` escaping all confirmed. One test-side
bug (a transposed unicode escape) was caught by the oracle, demonstrating the
loop works.

The same flow (run C# binary + C++ binary on identical input, byte-compare
stdout) is the differential harness described in `README.md` and applies to:
`lively_watchdog` (stdin protocol), `lively_cmd` (parse outcomes), and
`lively_console_demo` (menu flows, once phase 2 links the gRPC clients).

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
