#include <lively/services/audio_visualizer_service.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <mutex>
#include <stdexcept>

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "avrt.lib")

namespace lively::services {

namespace {

constexpr CLSID kCLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
constexpr IID kIID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
constexpr IID kIID_IAudioClient = __uuidof(IAudioClient);
constexpr IID kIID_IAudioCaptureClient = __uuidof(IAudioCaptureClient);

// Iterative radix-2 in-place FFT (inputs are real, complex container used to
// mirror the C# Complex[] path).
void fft_in_place(std::vector<std::complex<double>>& a) {
    const size_t n = a.size();
    if (n <= 1) return;

    // Bit-reversal permutation.
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }

    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * 3.14159265358979323846 / static_cast<double>(len);
        const std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

} // namespace

struct NAudioVisualizerService::Impl {
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* audio_client = nullptr;
    IAudioCaptureClient* capture_client = nullptr;
    WAVEFORMATEX* mix_format = nullptr;
};

NAudioVisualizerService::NAudioVisualizerService() = default;

NAudioVisualizerService::~NAudioVisualizerService() {
    stop();
    if (impl_) {
        if (impl_->mix_format) CoTaskMemFree(impl_->mix_format);
        if (impl_->capture_client) impl_->capture_client->Release();
        if (impl_->audio_client) impl_->audio_client->Release();
        if (impl_->device) impl_->device->Release();
        if (impl_->enumerator) impl_->enumerator->Release();
    }
}

void NAudioVisualizerService::start(const std::string& device_id) {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return;

    try {
        if (!impl_) impl_ = std::make_unique<Impl>();

        HRESULT hr = CoCreateInstance(kCLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                                      kIID_IMMDeviceEnumerator,
                                      reinterpret_cast<void**>(&impl_->enumerator));
        if (FAILED(hr)) throw std::runtime_error("MMDeviceEnumerator init failed");

        if (device_id.empty()) {
            hr = impl_->enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &impl_->device);
        } else {
            const std::wstring wid(device_id.begin(), device_id.end());
            hr = impl_->enumerator->GetDevice(wid.c_str(), &impl_->device);
        }
        if (FAILED(hr)) throw std::runtime_error("audio device open failed");

        hr = impl_->device->Activate(kIID_IAudioClient, CLSCTX_ALL, nullptr,
                                     reinterpret_cast<void**>(&impl_->audio_client));
        if (FAILED(hr)) throw std::runtime_error("audio client activate failed");

        // Loopback flag on a render endpoint = capture what plays out.
        hr = impl_->audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                             AUDCLNT_STREAMFLAGS_LOOPBACK,
                                             10000000 /*1s*/, 0, nullptr, nullptr);
        if (FAILED(hr)) throw std::runtime_error("loopback capture init failed");

        hr = impl_->audio_client->GetMixFormat(&impl_->mix_format);
        if (FAILED(hr) || impl_->mix_format == nullptr)
            throw std::runtime_error("mix format unavailable");

        hr = impl_->audio_client->GetService(kIID_IAudioCaptureClient,
                                             reinterpret_cast<void**>(&impl_->capture_client));
        if (FAILED(hr)) throw std::runtime_error("capture client unavailable");

        hr = impl_->audio_client->Start();
        if (FAILED(hr)) throw std::runtime_error("capture start failed");
    } catch (...) {
        running_ = false;
        throw;
    }

    thread_ = std::jthread([this] { capture_loop(); });
}

void NAudioVisualizerService::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) return;
    if (impl_ && impl_->audio_client) impl_->audio_client->Stop();
    if (thread_.joinable()) thread_.join();
}

void NAudioVisualizerService::capture_loop() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool com = SUCCEEDED(hr);

    REFERENCE_TIME poll_period = 1000000 / 20;  // ~20 Hz like NAudio's callback cadence

    while (running_) {
        ::Sleep(50);

        UINT32 packet_frames = 0;
        if (impl_->capture_client->GetNextPacketSize(&packet_frames) != S_OK) break;
        while (packet_frames > 0 && running_) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (impl_->capture_client->GetBuffer(&data, &frames, &flags, nullptr, nullptr) != S_OK)
                break;

            // NAudio WaveInEventArgs: interleaved float samples. C# takes
            // FloatBuffer.Length / 8 => first 2 channels of float audio.
            const size_t channels = std::max<UINT32>(1u, impl_->mix_format->nChannels);
            const size_t usable = std::min<size_t>(2, channels);
            const size_t len = frames * usable;
            std::vector<float> buffer(len);
            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                for (size_t i = 0; i < len; ++i) {
                    const float* src = reinterpret_cast<const float*>(data);
                    buffer[i] = src[i / usable * channels + (i % usable)];
                }
            }
            impl_->capture_client->ReleaseBuffer(frames);

            if (len > 0) process_audio_data(buffer);
            if (impl_->capture_client->GetNextPacketSize(&packet_frames) != S_OK) break;
        }
    }

    if (com) CoUninitialize();
}

void NAudioVisualizerService::process_audio_data(const std::vector<float>& samples) {
    try {
        // C#: len = buffer.FloatBuffer.Length / 8 → 2 float channels per frame.
        const size_t len = samples.size();
        if (len == 0) return;

        std::vector<std::complex<double>> values(len);
        for (size_t i = 0; i < len; ++i) values[i] = {static_cast<double>(samples[i]), 0.0};
        fft_in_place(values);

        // Magnitudes → vertical smoothing ring (keep last kVerticalSmoothness).
        std::vector<double> mags(values.size());
        for (size_t i = 0; i < values.size(); ++i) mags[i] = std::abs(values[i]);
        {
            std::lock_guard<std::mutex> lock(smooth_mutex_);
            smooth_.push_back(std::move(mags));
            if (smooth_.size() > kVerticalSmoothness)
                smooth_.erase(smooth_.begin());
        }

        std::vector<double> audio_data(kMaxSample);
        for (int i = 0; i < kMaxSample; ++i)
            audio_data[i] = both_smooth(static_cast<size_t>(i));
        audio_data_available.raise(std::move(audio_data));
    } catch (...) {
        // C# Debug.WriteLine path — swallow, keep capturing.
    }
}

double NAudioVisualizerService::both_smooth(size_t i) {
    std::lock_guard<std::mutex> lock(smooth_mutex_);
    double value = 0;
    const size_t lo = (i >= kHorizontalSmoothness) ? i - kHorizontalSmoothness : 0;
    const size_t hi = i + kHorizontalSmoothness;  // exclusive cap applied below
    for (size_t h = lo; h < std::min(hi, static_cast<size_t>(kMaxSample)); ++h)
        value += v_smooth(h, smooth_);
    return value / ((kHorizontalSmoothness + 1) * 2);
}

double NAudioVisualizerService::v_smooth(size_t i, const std::vector<std::vector<double>>& s) {
    if (s.empty()) return 0.0;
    double value = 0;
    for (const auto& frame : s)
        value += (i < frame.size()) ? frame[i] : 0.0;
    return value / static_cast<double>(s.size());
}

} // namespace lively::services
