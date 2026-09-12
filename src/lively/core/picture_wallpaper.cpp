#include <lively/core/picture_wallpaper.h>

#include <stdexcept>

#ifdef _WIN32
#include <windows.h>

#include <objbase.h>
#include <shobjidl.h>
#endif

namespace lively::core {

DesktopWallpaperPosition picture_scaler_position(models::WallpaperScaler scaler) {
    switch (scaler) {
        case models::WallpaperScaler::none:
            return DesktopWallpaperPosition::Center;
        case models::WallpaperScaler::fill:
            // Lively's "fill" is Windows' Stretch, not Fill.
            return DesktopWallpaperPosition::Stretch;
        case models::WallpaperScaler::uniform:
            return DesktopWallpaperPosition::Fit;
        case models::WallpaperScaler::uniformFill:
            // "not the same, here uniform fill pivot is topleft whereas for
            // windows its center." — upstream comment, kept.
            return DesktopWallpaperPosition::Fill;
        case models::WallpaperScaler::autofit:
            // `auto` => Fill, with a `todo` in the C# source.
            return DesktopWallpaperPosition::Fill;
    }
    return DesktopWallpaperPosition::Fill;
}

DesktopWallpaperPosition picture_show_position(models::WallpaperScaler scaler,
                                               models::WallpaperArrangement arrangement) {
    if (arrangement == models::WallpaperArrangement::span)
        return DesktopWallpaperPosition::Span;
    return picture_scaler_position(scaler);
}

#ifdef _WIN32

namespace {

// Minimal COM ref-count holder. The codebase deliberately avoids
// Microsoft::WRL (it is an MSVC header), and every other COM use here is a
// single CoCreateInstance, so a 15-line holder beats a dependency.
template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    T** put() {
        reset();
        return &ptr_;
    }
    T* get() const { return ptr_; }
    T* operator->() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }

    void reset() {
        if (ptr_) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

private:
    T* ptr_ = nullptr;
};

std::wstring widen(const std::string& utf8) {
    if (utf8.empty())
        return std::wstring();
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                         static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                        out.data(), size);
    return out;
}

std::string narrow(const wchar_t* wide) {
    if (wide == nullptr || *wide == L'\0')
        return std::string();
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
        return std::string();
    std::string out(static_cast<std::size_t>(size - 1), '\0');   // drop the NUL
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), size - 1, nullptr, nullptr);
    return out;
}

// The C# marshals `string` to LPCWSTR/LPWSTR automatically; every
// IDesktopWallpaper call here needs that by hand, including the two that hand
// back a CoTaskMemAlloc'd string the caller must free.
void free_com_string(LPWSTR value) {
    if (value)
        CoTaskMemFree(value);
}

} // namespace

PictureWallpaper::PictureWallpaper(std::string file_path,
                                   models::LibraryModel model,
                                   models::DisplayMonitor display,
                                   models::WallpaperArrangement arrangement,
                                   models::WallpaperScaler scaler)
    : file_path_(std::move(file_path)),
      model_(std::move(model)),
      screen_(std::move(display)),
      arrangement_(arrangement),
      scaler_(scaler) {
    // The C# constructs `new DesktopWallpaperClass()` in the field initializer
    // and keeps it for the object's lifetime; each ShowAsync/SetDesktopPicture
    // call site builds a fresh one. Here the instance lives for the call that
    // needs it, because the only two uses are show() and the ctor's bookkeeping.
    //
    // Bookkeeping: the C# reads the current background of every monitor (span) or
    // of this display only (everything else) into `wallpapersToRestore`. Nothing
    // ever reads that list — RestoreWallpaper is commented out — but the reads are
    // reproduced because they are the only part of the constructor that can fail,
    // and a port that skipped them would differ exactly when a monitor is in a
    // state IDesktopWallpaper refuses to describe.
    ComPtr<IDesktopWallpaper> desktop;
    if (SUCCEEDED(CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL,
                                   IID_PPV_ARGS(desktop.put())))) {
        if (arrangement_ == models::WallpaperArrangement::span) {
            UINT count = 0;
            if (SUCCEEDED(desktop->GetMonitorDevicePathCount(&count))) {
                for (UINT i = 0; i < count; ++i) {
                    LPWSTR id = nullptr;
                    if (FAILED(desktop->GetMonitorDevicePathAt(i, &id)) || id == nullptr)
                        continue;
                    LPWSTR current = nullptr;
                    if (SUCCEEDED(desktop->GetWallpaper(id, &current))) {
                        SystemWallpaper entry;
                        entry.device_id = narrow(id);
                        entry.file_path = narrow(current);
                        wallpapers_to_restore_.push_back(std::move(entry));
                    }
                    free_com_string(current);
                    free_com_string(id);
                }
            }
        } else {
            const std::wstring device = widen(screen_.device_id);
            LPWSTR current = nullptr;
            if (SUCCEEDED(desktop->GetWallpaper(device.c_str(), &current))) {
                SystemWallpaper entry;
                entry.device_id = screen_.device_id;
                entry.file_path = narrow(current);
                wallpapers_to_restore_.push_back(std::move(entry));
            }
            free_com_string(current);
        }
    }
}

PictureWallpaper::~PictureWallpaper() {
    // IWallpaper : IDisposable, and PictureWinApi.Dispose() calls Terminate().
    terminate();
}

NativeHandle PictureWallpaper::handle() const {
    return nullptr;
}

NativeHandle PictureWallpaper::input_handle() const {
    return nullptr;
}

const std::string& PictureWallpaper::lively_property_copy_path() const {
    return empty_property_path_;
}

void PictureWallpaper::show() {
    ComPtr<IDesktopWallpaper> desktop;
    if (FAILED(CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(desktop.put())))) {
        // The C# lets the COM exception escape ShowAsync; the core turns that into
        // a WallpaperError. Same shape here: show() throws, the caller decides.
        throw std::runtime_error("Failed to create the DesktopWallpaper COM instance.");
    }

    const DesktopWallpaperPosition position = picture_show_position(scaler_, arrangement_);
    desktop->SetPosition(static_cast<DESKTOP_WALLPAPER_POSITION>(position));

    // The C# passes `null` for the monitor id in the span arrangement, which
    // means "the whole desktop" — not "every monitor". Passing an empty string
    // instead is a different request, so the null is preserved.
    if (arrangement_ == models::WallpaperArrangement::span) {
        desktop->SetWallpaper(nullptr, widen(file_path_).c_str());
    } else {
        desktop->SetWallpaper(widen(screen_.device_id).c_str(), widen(file_path_).c_str());
    }

    loaded.raise();
}

void PictureWallpaper::close() {
    // RestoreWallpaper() — the C# body is commented out. Kept as a named step so
    // the divergence is one place to change if the user ever wants the old
    // background back (see the header).
    is_exited_ = true;
    exited.raise();
}

void PictureWallpaper::screen_capture(const std::string&) {
    throw std::logic_error("PictureWinApi.ScreenCapture is not implemented (C# throws NotImplementedException).");
}

#else   // !_WIN32

PictureWallpaper::PictureWallpaper(std::string file_path,
                                   models::LibraryModel model,
                                   models::DisplayMonitor display,
                                   models::WallpaperArrangement arrangement,
                                   models::WallpaperScaler scaler)
    : file_path_(std::move(file_path)),
      model_(std::move(model)),
      screen_(std::move(display)),
      arrangement_(arrangement),
      scaler_(scaler) {}

PictureWallpaper::~PictureWallpaper() = default;

NativeHandle PictureWallpaper::handle() const { return nullptr; }
NativeHandle PictureWallpaper::input_handle() const { return nullptr; }
const std::string& PictureWallpaper::lively_property_copy_path() const { return empty_property_path_; }

void PictureWallpaper::show() {
    throw std::runtime_error("PictureWallpaper requires Windows (IDesktopWallpaper).");
}

void PictureWallpaper::close() {
    is_exited_ = true;
    exited.raise();
}

void PictureWallpaper::screen_capture(const std::string&) {
    throw std::logic_error("PictureWinApi.ScreenCapture is not implemented.");
}

#endif

} // namespace lively::core
