#pragma once
// Port of Lively.Common.Services/NAudioVisualizerService.cs — WASAPI loopback
// capture → FFT → smoothed 128-bin spectrum.
//
// C# stack → C++ mapping:
//   NAudio WasapiLoopbackCapture → raw WASAPI (IAudioClient +
//   IAudioCaptureClient), render-endpoint loopback, float samples.
//   MathNet Fourier.Forward → self-contained iterative radix-2 FFT (the input
//   length is a power-of-two multiple; DFT bins identical by construction).
//   MMDeviceEnumerator notifications → IMMNotificationClient (device change
//   restarts capture on the render default).
//
// Processing chain preserved exactly (ProcessAudioData):
//   len = floatBuffer.Length / 8        (first 2 channels of float stream)
//   values[i] = Complex(buffer[i], 0)   then forward FFT
//   vertical smoothing: last 2 frames averaged per-bin magnitude
//   horizontal smoothing: +-1 bin average, divided by ((h+1)*2)
//   output: 128 doubles + AudioDataAvailable event
//
// C# Threading: capture callback thread → dedicated thread feeding the same
// math, event raised from that thread (call-sites marshal).

#include <lively/events.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lively::services {

class NAudioVisualizerService {
public:
    static constexpr int kMaxSample = 128;       // C# maxSample
    static constexpr int kVerticalSmoothness = 2;  // C# vertical_smoothness
    static constexpr int kHorizontalSmoothness = 1; // C# horizontal_smoothness

    NAudioVisualizerService();
    ~NAudioVisualizerService();

    NAudioVisualizerService(const NAudioVisualizerService&) = delete;
    NAudioVisualizerService& operator=(const NAudioVisualizerService&) = delete;

    // C# Start(deviceId = null): deviceId not supported by WASAPI loopback
    // default-render capture unless a device id is given; null → default.
    void start(const std::string& device_id = {});
    void stop();

    // C# AudioDataAvailable event — 128 doubles.
    lively::event<std::vector<double>> audio_data_available;

private:
    void capture_loop();
    void process_audio_data(const std::vector<float>& samples);
    double both_smooth(size_t i);
    static double v_smooth(size_t i, const std::vector<std::vector<double>>& s);

    struct Impl;  // WASAPI details
    std::unique_ptr<Impl> impl_;

    std::jthread thread_;
    std::atomic<bool> running_{false};

    mutable std::mutex smooth_mutex_;
    std::vector<std::vector<double>> smooth_;  // last frames' magnitude spectra
};

} // namespace lively::services
