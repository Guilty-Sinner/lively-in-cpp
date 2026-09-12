// MiDaS port tests. Inference needs a real ONNX model (model file is not
// committed — see tools/generate_csharp_goldens.md for the download spec);
// error-path behaviour is verified unconditionally.

#include <catch2/catch_test_macros.hpp>

#include <lively/ml/midas.h>

#include <filesystem>
#include <fstream>

#ifdef LIVELY_ML_ENABLED
TEST_CASE("MiDaS negotiates a usable onnxruntime API", "[ml]") {
    // The runtime must expose *some* API version the port can drive. (MSYS2
    // currently ships 1.29 headers with a 1.17.1 DLL — the port falls back to
    // the runtime's newest supported version rather than failing outright.)
    const std::string version = lively::ml::runtime_version();
    INFO("onnxruntime runtime: " << version);
    REQUIRE_FALSE(version.empty());

    lively::ml::MiDaS midas;
    // A missing model file must fail on the *file*, not on API negotiation.
    try {
        midas.load_model("nonexistent_model.onnx");
        FAIL("expected load_model to throw");
    } catch (const std::runtime_error& e) {
        // The failure must come from ORT itself (bad model file), never from
        // API negotiation — that would mean inference is impossible on this host.
        const std::string what = e.what();
        INFO("load_model error: " << what);
        CHECK(what.find("no supported API version") == std::string::npos);
    }
}

TEST_CASE("MiDaS Run normalises output into [0,1]", "[ml][.heavy]") {
    // Model path from env (set in CI); skip when absent so local builds pass.
    const char* model = std::getenv("LIVELY_MIDAS_MODEL");
    if (!model || !std::filesystem::exists(model)) {
        SKIP("no MiDaS ONNX model available (set LIVELY_MIDAS_MODEL)");
    }
    const char* image = std::getenv("LIVELY_MIDAS_IMAGE");
    if (!image || !std::filesystem::exists(image)) {
        SKIP("no test image available (set LIVELY_MIDAS_IMAGE)");
    }

    lively::ml::MiDaS midas;
    midas.load_model(model);
    const auto output = midas.run(image);

    CHECK(output.width > 0);
    CHECK(output.height > 0);
    CHECK(output.depth.size() == static_cast<size_t>(output.width) * output.height);
    float mn = 1.f, mx = 0.f;
    for (const float v : output.depth) { mn = std::min(mn, v); mx = std::max(mx, v); }
    CHECK(mn >= 0.f);
    CHECK(mx <= 1.f);
}
#endif

TEST_CASE("MiDaS error paths match C# exceptions", "[ml]") {
    lively::ml::MiDaS midas;

    // C# Run throws FileNotFoundException("ONNX file not provided") when no
    // model was loaded.
    CHECK_THROWS_AS(midas.run("whatever.png"), std::runtime_error);

    // C# LoadModel throws on a non-model file.
#ifdef LIVELY_ML_ENABLED
    CHECK_THROWS_AS(midas.load_model("nonexistent_model.onnx"), std::runtime_error);
#endif
}
