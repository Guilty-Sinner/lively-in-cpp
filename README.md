# Lively — C# → C++ Re-engineering

Port of [Lively Wallpaper](https://github.com/rocksdanister/lively) (C#/.NET, WinUI 3)
to C++, following *"From C# to C++: A Blueprint for Architectural Re-engineering and
Verifying 100% Functional Equivalence"* (see `prompt.txt` at the repo root).

**This is a re-architecture, not a transpilation.** The C# sources are treated as the
requirements document; every mapping below is deliberate.

## Status

| C# Project | LOC | C++ status | C++ location |
|---|---|---|---|
| Lively.Models | 2,072 | **Ported** — IPC wire format + LivelyProperty pipeline + SettingsModel (74 fields, byte-identical to C# Newtonsoft output) + `ApplicationRulesModel` / `AppMusicExclusionRuleModel` / `ThemeModel`, all verified against captured C# output (oracle tests) | `src/lively/models` |
| Lively.Utility.Watchdog | 159 | **Ported** — protocol + Win32 main, tests green | `src/lively/utility/watchdog` |
| Lively.Utility.Commandline | 79 | **Ported** — all 7 verbs, tests green | `src/lively/utility/commandline` |
| Lively.Utility.ConsoleDemo | 121 | **Ported** (menu + phase-2 stubs for gRPC calls) | `src/lively/utility/console_demo` |
| Lively.Common (helpers) | ~3,700 of 9,253 | **Ported** — Constants, SystemInfo, LogUtil, AppLifeCycleUtil, `JsonUtil`/`JsonStorage`, `PipeClient` (Win32 named pipe); `FileTypes` (incl. a zip central-directory reader replacing SharpZipLib), `FileUtil`, `LinkUtil`, `Languages`, `WindowClassExclusions`, `WallpaperExtensions`, `LivelyInfoUtil`, `System.IO.Path` primitives, `EncryptUtil` (DPAPI) + base64 — all transcript-verified against the C# oracle | `src/lively/common` |
| Lively (core) — library/metadata slice | ~800 of 15,815 | **Ported** — `LivelyInfoModel` (+ its byte-exact `livelyinfo.json` writer), `LibraryModel`, `LivelyInfoLocalizationFile`, `WallpaperLibraryFactory` (`GetMetadata`, `CreateFromDirectory`, `CreateFromMetadata`, `CreateWallpaperPackage`, `ConvertAbsoluteToRelativePath`), and the layout files: `WallpaperLayoutModel` / `ScreenSaverLayoutModel` + their `WallpaperLayout.json`/`ScreenSaverLayout.json` persistence. This is the headless slice: what a library entry *is*, every path rule that fills it, and how a multi-display arrangement is persisted. The rest of the core (WorkerW, the placement decisions themselves, wallpaper lifecycle) is not started | `src/lively/models`, `src/lively/common/factories` |
| Lively.Common.Services | 830 | **Ported** — `GithubUpdaterService`, `HttpDownloadService`, `NAudioVisualizerService` (WASAPI loopback + FFT); `NpsmNowPlayingService` is a documented stub (NPSMLib wraps undocumented WinRT internals with no native surface) | `src/lively/services` |
| Lively.Grpc.Client | 1,261 | **Ported (complete — all 5 clients)**: `CommandsClient`, `DesktopCoreClient`, `DisplayManagerClient`, `AppUpdaterClient`, `UserSettingsClient` — coroutine gRPC clients with server-streaming subscriptions, events, typed error mapping, full 74-field settings proto mapping; wire-compatibility proven against a real C# server ([.crosslang] oracle test) | `src/lively/rpc`, `proto/` |
| Lively.Gallery.Client | 687 | **Ported** — full REST client over WinHTTP (search/subscriptions/auth/refresh/health), incl. the C# quirks (both providers POST to `auth/google-token`, unencoded tags, truncated-MB progress), the shared `net::HttpClient`, and `JsonTokenStore` (DPAPI-encrypted `Tokens.dat`) | `src/lively/gallery`, `src/lively/net` |
| Lively.Player.Wmf | 425 | `StartArgs` contract **ported + oracle-verified**; window/rendering not started | `src/lively/players` |
| Lively.Player.Vlc | 832 | `StartArgs` contract **ported + oracle-verified**; window/rendering not started | `src/lively/players` |
| Lively.Player.WebView2 | 1,199 | `StartArgs` contract **ported + oracle-verified**; window/rendering not started | `src/lively/players` |
| Lively.Player.CefSharp | 1,346 | `StartArgs` contract **ported + oracle-verified**; window/rendering not started | `src/lively/players` |
| Lively.ML | 193 | **Ported** — MiDaS depth estimation over the onnxruntime C API with WIC image decode/resize; runtime API-version negotiation (see *Toolchain note*) | `src/lively/ml` |
| Lively.Common | 9,253 | Remaining: Helpers/{Shell,Hardware,Pinvoke,MVVM}, COM interop — Phase 2 | — |
| Lively (core) | 15,815 | Remaining: WorkerW desktop integration, per-display placement, wallpaper lifecycle — Phase 2 | — |
| Lively.UI.Shared / UI.WinUI | 11,284 | Not started (Phase 3, C++/WinRT) | — |

## Translation map (per `prompt.txt`)

| C# construct | C++ construct (here) |
|---|---|
| `event EventHandler<T>` | `lively::event<TArgs>` (`include/lively/events.h`) — thread-safe, lock-then-copy invocation |
| single-cast delegate | `std::function<S>` |
| `async Task` / `await` | C++20 coroutines: `lively::Task<T>` + `task_completion_source<T>` |
| `CancellationToken` | `lively::cancellation_token` + `cancellation_token_source` |
| GC objects | `std::unique_ptr` / `std::shared_ptr` (`std::weak_ptr` for back-refs) |
| `using` / `finally` | RAII scopes/destructors |
| `Enum` | `enum class` with identical member names (wire JSON is string-based) |
| `GrpcDotNetNamedPipes` channel | stock `grpc::Channel` (named-pipe transport is managed-only; proto contract ported exactly) |
| gRPC stubs (`Async*`) | generated callback API wrapped in `lively::Task` via `task_completion_source` |
| gRPC exceptions | failed `grpc::Status` → `RpcException` rethrown from the awaited Task |
| `JsonProperty` models | `nlohmann::json` `to_json`/`from_json` with **byte-identical field names** |

## Invariants being verified

1. **IPC JSON wire format**: `{"Type":"volume","Value":0.5}` must serialize identically
   (key names, casing, enum strings) — verified by golden-master tests against fixtures
   captured from the C# `JsonConvert` output.
2. **CommandLine protocol**: `livelycmd` argument → request mapping must be 1:1 — parser
   re-implemented from CommandLineParser **2.9.1** sources; 3,500 differential-fuzz cases,
   0 divergences.
3. **Watchdog behaviour**: ADD/RMV/CLR stdin protocol + parent-exit kill list order.
4. **gRPC contract**: identical request bytes on every `CommandsService` method — C++
   client ↔ C++ server and C++ client ↔ **real C# server** (from `Lively.Grpc.Common`
   bindings) produce identical server-side records (`[.crosslang]` test).
5. **HTTP call semantics**: every non-2xx branch of the C# call-sites is preserved —
   `SendAsync<T>` throws on `Errors`, 401 triggers exactly one refresh+retry pass,
   and `HttpClient.GetAsync`'s implicit `EnsureSuccessStatusCode()` makes the
   downloader throw before consuming the error body. (The status code reaching the
   call-sites at all is itself an invariant — a `send()` that dropped the streamed
   status/headers made every non-200 path silently look like success.)
6. **Settings persistence**: `settings.json` is byte-identical to the C#
   `JsonConvert` default output (oracle fixture `tests/goldens/settings_csharp.json`).
7. **Library/metadata rules**: `library_csharp.txt` is a 314-line transcript of the C#
   side of the library tranche — `FileTypes.GetFileType` over every supported extension,
   zip package detection (archives written by the real SharpZipLib), the exhaustive
   `WallpaperExtensions` matrix (12 types × 8 predicates), `LinkUtil` last-segment/stable-host
   rules, `FileUtil` filename/index/size-formatting behaviour, the 46-entry language table,
   `livelyinfo.json` bytes, and `WallpaperLibraryFactory.CreateFromDirectory` for the four
   metadata shapes (absolute local, relative local, online, media). The port reproduces it
   line for line — including the traps: a media wallpaper falls back to the bundled player
   properties under `%LOCALAPPDATA%`, and with `IsAbsolutePath` the property file is looked
   up next to the *executable*, not in the wallpaper folder.
8. **Layout persistence**: `WallpaperLayout.json` / `ScreenSaverLayout.json` are JSON arrays
   whose entries nest a `DisplayMonitor`, and Newtonsoft writes that class in a way
   nobody would guess: the public *field* `isStale` is serialized **first**, the
   `Bounds`/`WorkingArea` rectangles become the *string* `"x, y, width, height"`
   (System.Drawing's TypeConverter), and `IntPtr HMonitor` becomes `{"value": n}`.
   The settings fixture never caught this because its `SelectedDisplay` is null, so
   `settings.json` written by the port was unreadable by C# for any real display —
   now pinned by `tests/goldens/layout_csharp.txt` and a settings regression test.
9. **Player launch contract**: the four `Lively.Player.*/StartArgs` option sets parse
   identically to CommandLineParser 2.9.1 — verified by a 1,500-case differential run
   (`[.players-oracle]`). This is the interface the core uses to launch a wallpaper
   player, so its quirks are load-bearing, e.g. `bool` is a *switch* (so
   `--wallpaper-hardware-decoding false` still means `true`), enum names are matched
   case-sensitively, and a scalar only ever consumes the token immediately after it.
10. **Encrypted + rule-file persistence** (`tests/goldens/persist_csharp.txt`, from
   `csharp_probe persist`): `Tokens.dat`, `AppRules.json` and
   `MusicAppExclusionRules.json`. This fixture is deliberately *partial*, and the
   reason is the interesting part: a DPAPI blob carries a random key and salt, so
   `Tokens.dat` can never be byte-compared between the two implementations. What the
   golden pins instead is (a) the wrapper shape — a JSON **string** of base64, single
   line, i.e. not plaintext JSON, (b) the plaintext that gets encrypted, which *is*
   deterministic, and (c) the two rule files byte for byte. That caught two real
   divergences: `Expiration` had to round-trip through Newtonsoft's canonical
   `DateTime` rendering (`2030-06-01T12:30:45.0000000Z` → `…45Z`, and `default(DateTime)`
   → `0001-01-01T00:00:00` — *no* `Z`, which is not the string the port originally
   wrote), and the two `List<T>` rule files are indented/CRLF like everything else
   `JsonStorage<T>` writes.
11. **Theme files**: `theme.json` + `AppThemeFactory`, the last `JsonStorage<T>` type
   the app persists. Two members a hand-written port gets wrong: `IsEditable` carries
   `[JsonIgnore]` and must never reach the file, and the C# **copy constructor does not
   copy `AppVersion`** (it re-runs the field initializer), so an installed theme records
   the *host app's* version even when the source model had a different one. The port
   models that as `ThemeModel::copy_constructor` rather than a compiler-generated copy,
   so the divergence is visible at each call site. `Name` is nullable while `AppVersion`
   is not (it has an initializer) — a default-constructed model writes `"Name": null`.
   The fixture masks the entry-assembly version, because that value is build metadata
   (`1.0.0.0` for the probe, `2.2.1.5` for the real app).

## Verification strategy

- **Golden master / characterization tests** (`tests/goldens`): fixtures generated from the
  C# binaries (run `dotnet run --project "lively in C#/src/Lively/Lively.Utility.ConsoleDemo"`)
  are compared byte-for-byte against C++ output. Regenerate a specific one with the probe:
  `dotnet run -c Release --project tools/csharp_probe -- <mode>` where `<mode>` is
  `library` / `layout` / `persist` / `props` / `settings` / `players` / a bare run for the
  IPC fixtures — see `tools/generate_csharp_goldens.md`.
- **Differential fuzzing** (`tools/fuzz_differential.py`): the same generated argv is
  fed to the real C# `CommandLineParser` (via `tools/csharp_probe fuzz`) and to the C++
  binary; the canonical outcome lines must match. 4,500+ verb cases and 1,500 player-args
  cases run clean. The corpus deliberately includes adversarial tokens (`--=`, `-abc`,
  `--name=`, `--x==y`); widening it is what surfaced four genuine parser divergences,
  documented in the code at each fix site:
  1. `--=` yields the option NAME `"="` (unknown-option error), not a format error;
  2. a single dash takes exactly one character as the name (`-abc` → `Name("a")` +
     `Value("bc")`, so `-Infinity` reports unknown option `I`);
  3. the partitioner is a state machine, not "group the stream in twos" — a scalar name
     is only cleared by the next token, and a switch name resets the scan;
  4. duplicate errors that render to the same message are collapsed.
- **Formal methods**: targeted (protocol state machines only) — deferred.

## Building

```bash
# Requires: CMake ≥ 3.24, C++20 compiler (MSVC 19.3x / clang-cl / MinGW-w64 GCC 13+).
# nlohmann/json + Catch2 are vendored in vendor/ (see note below).
# gRPC (optional): auto-detected via CMAKE_PREFIX_PATH (MSYS2: pacman -S
#   mingw-w64-ucrt-x86_64-grpc); build degrades gracefully without it.
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
# Run the suite through the Windows loader (see "Running the tests" below):
powershell -ExecutionPolicy Bypass -File tools/run_tests.ps1
```

Targets: `lively_models` (static lib), `lively_utility`, `lively_watchdog` (exe),
`lively_cmd` (exe), `lively_console_demo` (exe), `lively_tests` (Catch2),
and with gRPC: `lively_rpc` (proto bindings + the five gRPC clients).
Libraries: `lively_net` (WinHTTP), `lively_gallery`, `lively_services`, `lively_ml`,
`lively_players`.

The oracle tests that shell out to the C# probe are hidden from a plain run
(`[.crosslang]`, `[.players-oracle]`), so `ctest` never discovers them; run them
explicitly once the probe is built:
`powershell -File tools/run_tests.ps1 -Filter "[.players-oracle]"`. The library
tranche oracle (`[library][oracle]`) needs no probe — it replays a committed
transcript — so it is part of the normal suite.

Two cache variables locate the reference oracle (both default to the local dev
layout, so a sibling `lively in C#` checkout needs no flags):

- `LIVELY_CSHARP_ROOT` — the upstream C# checkout.
- `LIVELY_PROBE_EXE_PATH` — the built `csharp_probe.exe`.

Only the hidden oracle tests need either of them; the rest of the suite is
self-contained (see *Continuous integration*).

### Continuous integration

`.github/workflows/ci.yml` implements the two verification layers described above:

- **`build-test`** — MSYS2 UCRT64 toolchain, matrix over `LIVELY_BUILD_RPC=ON|OFF`,
  `ctest` (87 / 80 cases). **No upstream C# checkout is required**: the oracle
  *goldens* are committed and the LivelyProperty input fixtures are vendored, so
  this runs on every push and pull request.
- **`oracle`** — checks out upstream Lively at the pinned commit, builds
  `tools/csharp_probe` with dotnet, and runs the differential oracle tests:
  player start-args against the real CommandLineParser 2.9.1, and the C++ gRPC
  client against a real C# server.

Running the whole oracle layer in CI is what makes the equivalence claim
reproducible by anyone with the repository, rather than a claim about one
developer's machine.

### Running the tests

Use `tools/run_tests.ps1` rather than invoking `lively_tests.exe` from an MSYS2
shell. Binary targets link MSYS2 runtime DLLs (gRPC/absl) which import Windows
*API set* forwarders (`api-ms-win-crt-*`); the Windows loader resolves those from
the OS API-set schema, but MSYS2/Cygwin's own PE dependency pre-check does not
and refuses to exec the binary with
`error while loading shared libraries: api-ms-win-crt-utility-l1-1-0.dll`.
The script only prepends the runtime DLL directories to `PATH` and launches
through the Windows loader.

Test discovery uses `catch_discover_tests(... DISCOVERY_MODE PRE_TEST)` so a
build never depends on those DLLs being on `PATH`, and a discovery failure can
no longer delete the freshly linked test binary.

Current verified environment: MinGW-w64 GCC 16.1 (WinLibs UCRT, via winget
`BrechtSanders.WinLibs.POSIX.UCRT`), CMake 4.4, MSYS2 UCRT64 gRPC 1.82 +
protobuf 35.1 —

- **RPC-off build**: 80 test cases / 771 assertions, all passing.
- **RPC-on build**: 87 test cases / 879 assertions, all passing (stable across reruns),
  including the `[.crosslang]` C++-client ↔ C#-server oracle test and the
  DesktopCore / DisplayManager / AppUpdater / UserSettings streaming + event tests.
- **CI-like build** (no C# checkout on the include path): 87 / 879 — the same
  numbers as RPC-on, which is what makes the self-contained claim real.
- **Gallery / services / HTTP**: a raw loopback HTTP server exercises the WinHTTP
  layer and the gallery client — token refresh on 401, the `AlreadySubscribed`
  rethrow path, subscription events, health, download progress and the
  non-2xx failure path.
- **SettingsModel JSON** is byte-identical to the real C# `JsonConvert` output
  (oracle fixture `tests/goldens/settings_csharp.json`; the oracle caught and
  fixed a wrong `DisplayIdentificationMode` ordinal during the port).
- **The library tranche** (models + `Lively.Common` helpers + the library factory)
  replays a 314-line C# transcript line for line (`tests/goldens/library_csharp.txt`,
  generated by `tools/csharp_probe library`) — no C# checkout needed to run it, so it
  is part of the ordinary CI suite rather than an opt-in oracle.

The cross-language test needs the C# probe built first:
`dotnet build tools/csharp_probe -c Release` (requires the sibling `lively in C#`
checkout + .NET SDK 8; the test fails with a clear message when absent).

### Vendored dependencies

CMake's bundled downloader failed TLS verification against github.com in this
environment, so `nlohmann/json` 3.11.3 and Catch2 3.7.1 tarballs are checked in
under `vendor/` with SHA-256 pins. On networks where CMake can reach GitHub,
the `FetchContent_Declare` URLs can be swapped back to remote ones.

## Toolchain note

The suite is built and run on Windows (MSYS2/MinGW-w64) in CI, which is the
toolchain the Win32 layers target. Compiling under MSVC (`/W4` + `/permissive-`)
and clang-cl is still desirable as an additional strictness pass — the code uses
only standard C++20 plus Win32 (guarded for non-Windows where trivial).

**onnxruntime version skew (known, worked around):** MSYS2's
`mingw-w64-ucrt-x86_64-onnxruntime 1.29.0-1` installs 1.29 headers
(`ORT_API_VERSION 29`) alongside a `onnxruntime.dll` that reports itself as
`1.17.1` and advertises API versions `[1, 17]`. Per ONNX Runtime's contract,
`GetApi(ORT_API_VERSION)` therefore returns `NULL`. `lively::ml` negotiates at
runtime: it asks for the header version and binary-searches down to the newest
version the DLL actually provides (OrtApi is append-only, so the calls used
belong to the original 1.x block). `lively::ml::runtime_version()` exposes the
loaded runtime for diagnostics, and a test asserts negotiation succeeds so the
skew is visible instead of surfacing as a spurious model-load failure.
End-to-end inference still needs a real MiDaS model plus an API-compatible
runtime (set `LIVELY_MIDAS_MODEL` / `LIVELY_MIDAS_IMAGE`); the `[.heavy]` test
skips without them.

## Legal

Port of GPL-3.0 software (Lively Wallpaper by rocksdanister); this derivative remains
GPL-3.0. The full license text is in [`LICENSE`](LICENSE).

This repository contains only the C++ port. The original C# sources are a separate
checkout used as the reference oracle for the tests in `tools/csharp_probe` -
they are not vendored here, with two exceptions: the oracle *output* fixtures in
`tests/goldens/`, and the two small `Assets/Plugins/Mpv/LivelyProperties*.json`
inputs they were generated from (copied so the property-pipeline tests can run
without the C# checkout). Both are upstream GPL-3.0 content, as is this port.
