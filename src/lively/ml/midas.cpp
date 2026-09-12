#include <lively/ml/midas.h>

#include <onnxruntime_c_api.h>

#include <algorithm>
#include <fstream>
#include <stdexcept>

#include <windows.h>
#include <wincodec.h>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace lively::ml {

namespace {

// Negotiate an OrtApi at runtime, noexcept so destructors can use it.
//
// The documented contract is "ask for ORT_API_VERSION; NULL means the runtime
// is older than the headers". That happens here: MSYS2's onnxruntime package
// ships 1.29 headers (ORT_API_VERSION 29) with a 1.17.1 DLL, which advertises
// API versions [1, 17]. Rather than refuse to run, fall back to the newest
// version the runtime does provide — OrtApi is append-only, so every call made
// below (env/session/tensor/run) belongs to the original 1.x block and keeps
// its offset.
const OrtApi* ort_api() noexcept {
    static const OrtApi* api = []() -> const OrtApi* {
        const OrtApiBase* base = OrtGetApiBase();
        if (!base) return nullptr;
        if (const OrtApi* exact = base->GetApi(ORT_API_VERSION)) return exact;

        // GetApi(v) is NULL exactly when v exceeds the runtime's maximum, so
        // binary-search the highest version it does support. (A linear scan
        // would work too, but each rejected probe makes the runtime print a
        // warning — ~log2(N) probes instead of N.)
        const OrtApi* best = nullptr;
        for (int lo = 1, hi = ORT_API_VERSION; lo <= hi;) {
            const int mid = lo + (hi - lo) / 2;
            if (const OrtApi* candidate = base->GetApi(mid)) {
                best = candidate;
                lo = mid + 1;
            } else {
                hi = mid - 1;
            }
        }
        return best;
    }();
    return api;
}

std::string ort_runtime_version() {
    const OrtApiBase* base = OrtGetApiBase();
    return base && base->GetVersionString() ? base->GetVersionString() : std::string{};
}

const OrtApi* ort() {
    const OrtApi* api = ort_api();
    if (!api)
        throw std::runtime_error("onnxruntime: runtime provides no supported API version (headers "
                                 "request " + std::to_string(ORT_API_VERSION) + ")");
    return api;
}

OrtEnv* ort_env() {
    static OrtEnv* env = nullptr;
    if (!env) {
        OrtStatus* status = ort()->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "lively", &env);
        if (status != nullptr) {
            const std::string msg = ort()->GetErrorMessage(status);
            ort()->ReleaseStatus(status);
            throw std::runtime_error("onnxruntime env: " + msg);
        }
    }
    return env;
}

// Throws on failure; message extracted from the status.
void check_status(OrtStatus* status) {
    if (status == nullptr) return;
    const std::string msg = ort()->GetErrorMessage(status);
    ort()->ReleaseStatus(status);
    throw std::runtime_error("onnxruntime: " + msg);
}

// ---- WIC image loading: decode + stretch-resize + BGRA pixels ---------------

struct WicImage {
    std::vector<uint8_t> bgra;
    UINT width = 0, height = 0;
    UINT stride = 0;
    UINT original_width = 0, original_height = 0;
};

IWICImagingFactory* wic_factory() {
    static IWICImagingFactory* factory = nullptr;
    if (!factory) {
        const HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (FAILED(hr)) throw std::runtime_error("WIC factory init failed");
    }
    return factory;
}

std::wstring to_wide(const std::string& s) {
    return std::wstring(s.begin(), s.end());
}

WicImage load_and_resize(const std::string& path, UINT target_w, UINT target_h) {
    IWICImagingFactory* factory = wic_factory();

    IWICBitmapDecoder* decoder = nullptr;
    HRESULT hr = factory->CreateDecoderFromFilename(to_wide(path).c_str(), nullptr, GENERIC_READ,
                                                    WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) throw std::runtime_error("cannot decode image: " + path);

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) { decoder->Release(); throw std::runtime_error("no frame in image: " + path); }

    // Original dims (C# ModelInput(imagePath, inputImage.Width, inputImage.Height)).
    UINT ow = 0, oh = 0;
    frame->GetSize(&ow, &oh);

    // Stretch-resize to model dims (C# MagickGeometry IgnoreAspectRatio).
    IWICBitmapScaler* scaler = nullptr;
    factory->CreateBitmapScaler(&scaler);
    IWICBitmapSource* source = frame;
    if (scaler && SUCCEEDED(scaler->Initialize(frame, target_w, target_h,
                                               WICBitmapInterpolationModeFant))) {
        source = scaler;
    }

    // Convert to 32bppBGRA and read pixels.
    WicImage out;
    out.width = target_w;
    out.height = target_h;
    out.stride = target_w * 4;
    out.bgra.resize(static_cast<size_t>(out.stride) * out.height);
    out.original_width = ow;
    out.original_height = oh;

    IWICFormatConverter* converter = nullptr;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr) ||
        FAILED(converter->Initialize(source, GUID_WICPixelFormat32bppBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom)) ||
        FAILED(converter->CopyPixels(nullptr, out.stride,
                                     static_cast<UINT>(out.bgra.size()), out.bgra.data()))) {
        if (converter) converter->Release();
        if (scaler) scaler->Release();
        frame->Release();
        decoder->Release();
        throw std::runtime_error("pixel format conversion failed: " + path);
    }
    converter->Release();
    if (scaler) scaler->Release();
    frame->Release();
    decoder->Release();
    return out;
}

} // namespace

std::string runtime_version() { return ort_runtime_version(); }

struct MiDaS::Impl {
    OrtSession* session = nullptr;
    OrtSessionOptions* options = nullptr;
    std::string input_name;
    std::string output_name;
};

MiDaS::MiDaS() = default;

MiDaS::~MiDaS() {
    // Never throw from a destructor (prompt.txt: destructor exceptions during
    // stack unwinding call std::terminate). ort_api() is noexcept.
    const OrtApi* api = ort_api();
    if (impl_ && api) {
        if (impl_->session) api->ReleaseSession(impl_->session);
        if (impl_->options) api->ReleaseSessionOptions(impl_->options);
    }
}

void MiDaS::load_model(const std::string& path) {
    ort_env();
    if (!impl_) impl_ = std::make_unique<Impl>();

    if (impl_->session) {
        ort()->ReleaseSession(impl_->session);
        impl_->session = nullptr;
    }
    if (!impl_->options) {
        check_status(ort()->CreateSessionOptions(&impl_->options));
    }

    check_status(ort()->CreateSession(ort_env(), to_wide(path).c_str(), impl_->options,
                                      &impl_->session));

    // ORT name access: (session, index, allocator, out).
    OrtAllocator* allocator = nullptr;
    check_status(ort()->GetAllocatorWithDefaultOptions(&allocator));

    char* name_raw = nullptr;
    check_status(ort()->SessionGetInputName(impl_->session, 0, allocator, &name_raw));
    impl_->input_name = name_raw;
    ort()->AllocatorFree(allocator, name_raw);

    name_raw = nullptr;
    check_status(ort()->SessionGetOutputName(impl_->session, 0, allocator, &name_raw));
    impl_->output_name = name_raw;
    ort()->AllocatorFree(allocator, name_raw);

    // Input dims [N, C, H, W] — C# reads Dimensions[2] as width, [3] as height
    // (axis-swapped vs convention; replicated exactly for behaviour parity).
    OrtTypeInfo* type_info = nullptr;
    check_status(ort()->SessionGetInputTypeInfo(impl_->session, 0, &type_info));
    const OrtTensorTypeAndShapeInfo* tensor_info = nullptr;
    check_status(ort()->CastTypeInfoToTensorInfo(type_info, &tensor_info));
    size_t rank = 0;
    check_status(ort()->GetDimensionsCount(tensor_info, &rank));
    std::vector<int64_t> dims(rank, 0);
    check_status(ort()->GetDimensions(tensor_info, dims.data(), rank));
    ort()->ReleaseTypeInfo(type_info);

    if (dims.size() < 4)
        throw std::runtime_error("unexpected onnx input rank (need NCHW)");

    width_ = static_cast<int>(dims[2]);
    height_ = static_cast<int>(dims[3]);
    model_path_ = path;
    loaded_ = true;
}

ModelOutput MiDaS::run(const std::string& image_path) {
    if (!loaded_)
        throw std::runtime_error("ONNX file not provided");
    {
        std::ifstream probe(image_path, std::ios::binary);
        if (!probe) throw std::runtime_error("image not found: " + image_path);
    }

    const WicImage img = load_and_resize(image_path,
                                         static_cast<UINT>(width_),
                                         static_cast<UINT>(height_));

    // DenseTensor<float> [1,3,H,W] fill — channel planes in R,G,B order.
    const size_t plane = static_cast<size_t>(height_) * width_;
    std::vector<float> tensor(plane * 3);
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const size_t px = (static_cast<size_t>(y) * width_ + x) * 4;  // BGRA
            tensor[0 * plane + static_cast<size_t>(y) * width_ + x] = img.bgra[px + 2] / 255.0f;
            tensor[1 * plane + static_cast<size_t>(y) * width_ + x] = img.bgra[px + 1] / 255.0f;
            tensor[2 * plane + static_cast<size_t>(y) * width_ + x] = img.bgra[px + 0] / 255.0f;
        }
    }

    const int64_t shape[4] = {1, 3, height_, width_};
    OrtValue* input_tensor = nullptr;
    check_status(ort()->CreateTensorWithDataAsOrtValue(
        nullptr, tensor.data(), tensor.size() * sizeof(float), shape, 4,
        ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input_tensor));

    const char* input_names[] = {impl_->input_name.c_str()};
    const char* output_names[] = {impl_->output_name.c_str()};

    OrtValue* output_tensor = nullptr;
    OrtStatus* status = ort()->Run(impl_->session, nullptr, input_names,
                                   &input_tensor, 1, output_names, 1, &output_tensor);
    ort()->ReleaseValue(input_tensor);
    if (status != nullptr) {
        const std::string msg = ort()->GetErrorMessage(status);
        ort()->ReleaseStatus(status);
        throw std::runtime_error("onnx inference failed: " + msg);
    }

    ModelOutput result;
    {
        float* data = nullptr;
        check_status(ort()->GetTensorMutableData(output_tensor, reinterpret_cast<void**>(&data)));
        OrtTensorTypeAndShapeInfo* out_info = nullptr;
        check_status(ort()->GetTensorTypeAndShape(output_tensor, &out_info));
        size_t elements = 0;
        ort()->GetTensorShapeElementCount(out_info, &elements);
        ort()->ReleaseTensorTypeAndShapeInfo(out_info);
        result.depth.assign(data, data + elements);
    }
    ort()->ReleaseValue(output_tensor);

    // NormaliseOutput: min/max linear rescale to [0,1] (identity transform
    // ((1-n)*0 + n*1) preserved from C#).
    if (!result.depth.empty()) {
        const auto [mn, mx] = std::minmax_element(result.depth.begin(), result.depth.end());
        const float range = *mx - *mn;
        if (range != 0.f) {
            for (float& v : result.depth) v = (v - *mn) / range;
        }
    }

    result.width = width_;
    result.height = height_;
    result.original_width = static_cast<int>(img.original_width);
    result.original_height = static_cast<int>(img.original_height);
    return result;
}

} // namespace lively::ml
