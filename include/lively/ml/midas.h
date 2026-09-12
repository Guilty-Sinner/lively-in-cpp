#pragma once
// Port of Lively.ML/DepthEstimate/MiDaS.cs — ONNX MiDaS depth estimation for
// the 2.5D wallpaper effect.
//
// C# stack → C++ mapping:
//   Microsoft.ML.OnnxRuntime → onnxruntime (C API, MSYS2 package)
//   ImageMagick (MagickImage load/resize/pixels) → WIC (System WIC decoder +
//   IWICImagingFactory scaler, nearest-neighbour-free high-quality path)
//   DenseTensor fill loop → identical row-major [1,3,H,W] float fill
//   NormaliseOutput → min/max linear rescale, identical arithmetic
//
// ModelInput/ModelOutput shapes preserved (ModelOutput.cs).

#include <memory>
#include <string>
#include <vector>

namespace lively::ml {

// ModelOutput.cs
struct ModelOutput {
    std::vector<float> depth;
    int width = 0;            // model input width
    int height = 0;           // model input height
    int original_width = 0;   // source image width
    int original_height = 0;  // source image height
};

// Version string of the linked onnxruntime (e.g. "1.17.1"), or empty when the
// library is unavailable. Exposed so the runtime/header skew is observable
// instead of silently degrading inference to an exception at load time.
std::string runtime_version();

class MiDaS {
public:
    MiDaS();
    ~MiDaS();

    MiDaS(const MiDaS&) = delete;
    MiDaS& operator=(const MiDaS&) = delete;

    // C# LoadModel: creates the InferenceSession, reads input dims [N,C,H,W].
    void load_model(const std::string& path);

    // C# Run(imagePath): decode, resize to model dims, infer, normalise.
    // Throws std::runtime_error when no model loaded or file missing
    // (C# FileNotFoundException).
    ModelOutput run(const std::string& image_path);

    const std::string& model_path() const { return model_path_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string model_path_;
    int width_ = 0;   // model input width
    int height_ = 0;  // model input height
    bool loaded_ = false;
};

} // namespace lively::ml
