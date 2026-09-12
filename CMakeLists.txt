cmake_minimum_required(VERSION 3.24)
project(LivelyCpp VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static libstdc++/libgcc: executables must not depend on MinGW runtime DLLs
# being resolvable through the caller's PATH (differential harness launches
# them from python subprocess with an inherited environment).
if(MINGW)
  add_link_options(-static-libstdc++ -static-libgcc)
endif()

option(LIVELY_BUILD_TESTS "Build Catch2 tests" ON)
# Builds the gRPC tranche (Lively.Grpc.Common proto contract + client port).
# Auto-detects the gRPC toolchain and degrades gracefully when absent;
# pass -DLIVELY_REQUIRE_RPC=ON to turn a missing toolchain into a hard error.
option(LIVELY_BUILD_RPC "Build gRPC client/server port (requires gRPC toolchain)" ON)
option(LIVELY_REQUIRE_RPC "Fail configuration instead of skipping when gRPC is missing" OFF)

if(LIVELY_BUILD_RPC)
  # gRPC toolchain discovery: works for MSYS2 ucrt64 (CMAKE_PREFIX_PATH) and vcpkg.
  set(protobuf_MODULE_COMPATIBLE ON)
  find_package(gRPC CONFIG QUIET)
  if(NOT gRPC_FOUND)
    if(LIVELY_REQUIRE_RPC)
      message(FATAL_ERROR "LIVELY_REQUIRE_RPC is ON but gRPC was not found. Install gRPC (MSYS2: pacman -S mingw-w64-ucrt-x86_64-grpc) or pass its prefix via CMAKE_PREFIX_PATH.")
    else()
      message(STATUS "gRPC not found - building without the gRPC tranche (lively_rpc). Install it (MSYS2: pacman -S mingw-w64-ucrt-x86_64-grpc) to enable.")
      set(LIVELY_BUILD_RPC OFF)
    endif()
  endif()
endif()

if(LIVELY_BUILD_RPC)
  # Proto generation mirrors Lively.Grpc.Common: same 5 contract protos, C++ bindings.
  set(LIVELY_PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/proto)
  set(LIVELY_PROTO_GEN_DIR ${CMAKE_BINARY_DIR}/gen)
  file(MAKE_DIRECTORY ${LIVELY_PROTO_GEN_DIR})

  find_program(gRPC_CPP_PLUGIN_EXE grpc_cpp_plugin HINTS C:/msys64/ucrt64/bin)
  if(NOT gRPC_CPP_PLUGIN_EXE)
    message(FATAL_ERROR "grpc_cpp_plugin not found; required for LIVELY_BUILD_RPC")
  endif()

  set(LIVELY_PROTO_GENERATED_SRCS)
  set(LIVELY_PROTO_GENERATED_HDRS)
  foreach(proto commands display settings update wallpaper)
    set(pb_cc "${LIVELY_PROTO_GEN_DIR}/${proto}.pb.cc")
    set(pb_h "${LIVELY_PROTO_GEN_DIR}/${proto}.pb.h")
    set(grpc_cc "${LIVELY_PROTO_GEN_DIR}/${proto}.grpc.pb.cc")
    set(grpc_h "${LIVELY_PROTO_GEN_DIR}/${proto}.grpc.pb.h")
    add_custom_command(
      OUTPUT ${pb_cc} ${pb_h} ${grpc_cc} ${grpc_h}
      COMMAND protobuf::protoc
      ARGS
        -I${LIVELY_PROTO_DIR}
        -I${protobuf_DIR}/../../include
        --plugin=protoc-gen-grpc=${gRPC_CPP_PLUGIN_EXE}
        --cpp_out=${LIVELY_PROTO_GEN_DIR}
        --grpc_out=${LIVELY_PROTO_GEN_DIR}
        ${LIVELY_PROTO_DIR}/${proto}.proto
      DEPENDS ${LIVELY_PROTO_DIR}/${proto}.proto
      COMMENT "Generating gRPC C++ bindings for ${proto}.proto"
      VERBATIM)
    list(APPEND LIVELY_PROTO_GENERATED_SRCS ${pb_cc} ${grpc_cc})
    list(APPEND LIVELY_PROTO_GENERATED_HDRS ${pb_h} ${grpc_h})
  endforeach()
endif()

# Dependencies are vendored (vendor/) because some environments cannot reach
# github.com from CMake's downloader. CI may swap these for FetchContent URLs.
include(FetchContent)

find_package(nlohmann_json 3.10 QUIET)
if(NOT nlohmann_json_FOUND)
  FetchContent_Declare(json
    URL ${CMAKE_CURRENT_SOURCE_DIR}/vendor/json-3.11.3.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d)
  FetchContent_MakeAvailable(json)
endif()

if(LIVELY_BUILD_TESTS)
  find_package(Catch2 3 QUIET)
  if(NOT Catch2_FOUND)
    FetchContent_Declare(catch2
      URL ${CMAKE_CURRENT_SOURCE_DIR}/vendor/catch2-3.7.1.tar.gz
      URL_HASH SHA256=c991b247a1a0d7bb9c39aa35faf0fe9e19764213f28ffba3109388e62ee0269c)
    FetchContent_MakeAvailable(catch2)
  endif()
endif()

add_library(lively_models STATIC
  src/lively/models/ipc_message.cpp
  src/lively/models/lively_controls.cpp
  src/lively/models/settings_model.cpp)
target_include_directories(lively_models PUBLIC include)
target_link_libraries(lively_models PUBLIC nlohmann_json::nlohmann_json)
target_compile_definitions(lively_models PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

add_library(lively_common STATIC
  src/lively/common/constants.cpp
  src/lively/common/system_info.cpp
  src/lively/common/log_util.cpp
  src/lively/common/app_lifecycle.cpp
  src/lively/common/pipe_client.cpp)
target_include_directories(lively_common PUBLIC include)
target_link_libraries(lively_common PUBLIC lively_models)
if(WIN32)
  target_link_libraries(lively_common PUBLIC shell32)
endif()

add_library(lively_utility STATIC
  src/lively/utility/commandline/commandline.cpp
  src/lively/utility/watchdog/watchdog_core.cpp)

# Per-player StartArgs contract (Lively.Player.*) — shares the CommandLineParser port.
add_library(lively_players STATIC src/lively/players/start_args.cpp)
target_include_directories(lively_players PUBLIC include)
target_link_libraries(lively_players PUBLIC lively_utility lively_models)
target_include_directories(lively_models PUBLIC include)
target_include_directories(lively_utility PUBLIC include)
target_link_libraries(lively_utility PUBLIC lively_models)

add_executable(lively_watchdog src/lively/utility/watchdog/main.cpp)
target_link_libraries(lively_watchdog PRIVATE lively_utility)

add_executable(lively_cmd src/lively/utility/commandline/main.cpp)
target_link_libraries(lively_cmd PRIVATE lively_utility)

add_executable(lively_fuzz_target src/lively/utility/commandline/fuzz_target.cpp)
target_link_libraries(lively_fuzz_target PRIVATE lively_utility)

add_executable(lively_console_demo src/lively/utility/console_demo/main.cpp)
target_link_libraries(lively_console_demo PRIVATE lively_utility)

if(LIVELY_BUILD_RPC)
  add_library(lively_rpc STATIC
    src/lively/rpc/commands_client.cpp
    src/lively/rpc/desktop_core_client.cpp
    src/lively/rpc/display_manager_client.cpp
    src/lively/rpc/app_updater_client.cpp
    src/lively/rpc/user_settings_client.cpp
    ${LIVELY_PROTO_GENERATED_SRCS})
  target_include_directories(lively_rpc PUBLIC include ${LIVELY_PROTO_GEN_DIR})
  target_link_libraries(lively_rpc PUBLIC gRPC::grpc++ protobuf::libprotobuf nlohmann_json::nlohmann_json)
  # grpc++ headers require a Windows 7+ API level.
  target_compile_definitions(lively_rpc PUBLIC _WIN32_WINNT=0x0601 NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# HTTP client core (WinHTTP) — Gallery + updater + download services.
add_library(lively_net STATIC
  src/lively/net/http_client.cpp)
target_include_directories(lively_net PUBLIC include)
if(WIN32)
  target_link_libraries(lively_net PUBLIC winhttp)
endif()

# Gallery REST client port (Lively.Gallery.Client).
add_library(lively_gallery STATIC
  src/lively/gallery/gallery_client.cpp)
target_include_directories(lively_gallery PUBLIC include)
target_link_libraries(lively_gallery PUBLIC lively_net lively_models nlohmann_json::nlohmann_json)
if(WIN32)
  target_link_libraries(lively_gallery PUBLIC ws2_32)
endif()

# ML tranche (Lively.ML) — optional: needs onnxruntime + WIC.
option(LIVELY_BUILD_ML "Build MiDaS depth estimation (needs onnxruntime)" ON)
if(LIVELY_BUILD_ML)
  find_package(onnxruntime CONFIG QUIET)
  if(NOT onnxruntime_FOUND)
    message(STATUS "onnxruntime not found - building without the Lively.ML port (MSYS2: pacman -S mingw-w64-ucrt-x86_64-onnxruntime)")
    set(LIVELY_BUILD_ML OFF)
  endif()
endif()

if(LIVELY_BUILD_ML)
  add_library(lively_ml STATIC
    src/lively/ml/midas.cpp)
  target_include_directories(lively_ml PUBLIC include)
  target_link_libraries(lively_ml PUBLIC onnxruntime::onnxruntime)
endif()

# Common services (Lively.Common.Services): updater + downloader + audio viz.
add_library(lively_services STATIC
  src/lively/services/github_updater_service.cpp
  src/lively/services/http_download_service.cpp
  src/lively/services/audio_visualizer_service.cpp)
target_include_directories(lively_services PUBLIC include)
target_link_libraries(lively_services PUBLIC lively_net lively_models)
if(WIN32)
  target_link_libraries(lively_services PUBLIC ole32 avrt mmdevapi uuid)
endif()

if(LIVELY_BUILD_TESTS)
  add_executable(lively_tests
    tests/test_events_task.cpp
    tests/test_ipc_messages.cpp
    tests/test_ipc_oracle.cpp
    tests/test_commandline.cpp
    tests/test_watchdog.cpp
    tests/test_common.cpp
    tests/test_lively_props.cpp
    tests/test_settings.cpp
    tests/test_pipe_json.cpp
    tests/test_players.cpp)
  target_link_libraries(lively_tests PRIVATE lively_models lively_common lively_utility lively_players
    Catch2::Catch2WithMain)
  if(LIVELY_BUILD_RPC)
    target_sources(lively_tests PRIVATE tests/test_rpc.cpp)
    target_link_libraries(lively_tests PRIVATE lively_rpc)
    target_compile_definitions(lively_tests PRIVATE LIVELY_RPC_ENABLED=1)
  endif()
  target_compile_definitions(lively_tests PRIVATE
    LIVELY_GOLDENS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/goldens"
    LIVELY_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
    # C# oracle probe (built by dotnet) + scratch dir for cross-language tests.
    LIVELY_PROBE_EXE="${CMAKE_CURRENT_SOURCE_DIR}/tools/csharp_probe/bin/Release/net8.0/csharp_probe.exe"
    LIVELY_CROSSLANG_DIR="${CMAKE_CURRENT_BINARY_DIR}/crosslang")
  if(LIVELY_BUILD_ML)
    target_sources(lively_tests PRIVATE tests/test_midas.cpp)
    target_link_libraries(lively_tests PRIVATE lively_ml)
    target_compile_definitions(lively_tests PRIVATE LIVELY_ML_ENABLED=1)
  endif()
  target_sources(lively_tests PRIVATE tests/test_services.cpp tests/test_gallery.cpp)
  target_link_libraries(lively_tests PRIVATE lively_services lively_gallery)
  include(CTest)
  include(Catch)
  # PRE_TEST: run the discovery step at `ctest` time, not as a post-build step.
  # The test exe depends on runtime DLLs (gRPC/onnxruntime) that may not be on
  # PATH during a bare compile, and a post-build failure deletes the binary.
  catch_discover_tests(lively_tests DISCOVERY_MODE PRE_TEST)
endif()
