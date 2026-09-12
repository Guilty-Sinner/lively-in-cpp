# Phase 2+ port plan

Phase 1 (this tree) delivered the portability layer, `Lively.Models` (IPC wire
format), and both console utilities with a verification harness. The remaining
modules, in dependency order:

## 2. Lively.Common (9.2k LOC) → `src/lively/common`
* **No WinForms/WPF** here — mostly helpers, so the port is direct:
  `NativeMethods.cs` (1.5k lines of P/Invoke) → plain Win32 calls in C++.
* `Serialization/LivelyProperty` JSON model handling → nlohmann (same golden
  discipline as the IPC messages).
* Services (`IScreensaverService`, `IResourceService`…) → abstract C++ classes +
  the `lively::event` layer.
* NLog logging → `spdlog` (or a thin custom sink replicating the `LogUtil`
  reflection-based property dump as manual `to_json` per type).

## 3. gRPC client layer (1.3k LOC) → `src/lively/rpc`
* The five `.proto` files compile unchanged with `protoc` + `grpc::` C++.
* Each `*Client.cs` becomes a coroutine-based C++ class:
  `await client.GetScreensAsync(...)` → `co_await client.GetScreens()`
  built on `lively::Task<T>`; unary calls via `grpc::CompletionQueue` +
  `task_completion_source`; server streams → `Subscribe*Stream` coroutines
  with `cancellation_token` mapped to `context_->TryCancel()`.
* `SemaphoreSlim.WaitAsync` → `std::binary_semaphore` + TCS awaiter.
* Keep pipe names: `Grpc_LIVELY:DESKTOPWALLPAPERSYSTEM{user}` for wire
  compatibility with the C# core.

## 4. Lively core (15.8k LOC) → `src/lively/core`
* `WinDesktopCore` WorkerW/Progman logic is already raw Win32 in C# — ports
  nearly verbatim (`DesktopUtil.cs`, `SendMessageTimeout(0x052C)` dance).
* Wallpaper players: CEF (`cef` C++ API — remove the C# wrapper layer),
  WebView2 (`WebView2.h` C++), libVLC (`libvlc` C API — the C# bindings were
  wrappers around the same DLLs), WMF (`MediaEngine` via C++/WinRT or COM).
* `async void` event handlers → coroutine lambdas attached to `lively::event`;
  `async void` crashes map to explicit `try/catch` + log policy (C++ has no
  SynchronizationContext; UI marshalling goes through the WinRT dispatcher).
* Watchdog spawning, screensaver logic, tray — direct Win32 ports.

## 5. UI (Lively.UI.Shared + UI.WinUI, 11.3k LOC) → `src/lively/ui`
* WinUI 3 XAML ⇄ **C++/WinRT** (the native language for the same XAML; view-model
  layer ports with `winrt::Microsoft::UI::Xaml::Data` observability replacing
  `ObservableObject`; MVVM Toolkit `ObservableProperty` generators → macros or
  explicit properties).
* Localization `.resw` files are language-agnostic — reuse unchanged; load via
  `Windows.ApplicationModel.Resources` (C++/WinRT supports the same API).
* DI (`Microsoft.Extensions.DependencyInjection` registrations in `App.xaml.cs`)
  → a small hand-rolled container or `std::factory` registry generated at
  compile time (see prompt.txt reflection guidance: pre-generated lookup tables
  instead of runtime reflection).

## 6. Gallery + ML clients
* `GalleryClient` (REST + OAuth): port to `cpr`/WinHTTP + `nlohmann`.
* `Lively.ML` (ONNX depth estimation): use the **ONNX Runtime C++ API**
  directly — the C# package was already a managed wrapper over the same native
  runtime, making this one of the most faithful ports.

## Cross-cutting tasks
* Pin a toolchain in CI (MSVC 19.3x + `/W4 /permissive-`, clang-format,
  clang-tidy) and a CMake preset.
* Build `tools/csharp_probe` and wire `tests/goldens/` (see
  `tools/generate_csharp_goldens.md`).
* Differential fuzz targets: IPC JSON corpus → `JsonConvert.DeserializeObject`
  (C#) vs `from_json` (C++); commandline corpus → both parsers.
* Formal verification targets: watchdog state machine and the playback-policy
  suspend rules (`Core/Suspend/Playback.cs`) as TLA+/SMT-lite models.
