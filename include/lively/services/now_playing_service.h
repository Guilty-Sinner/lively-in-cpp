#pragma once
// Port of Lively.Common.Services/NpsmNowPlayingService.cs + INowPlayingService
// + NowPlayingEventArgs (Lively.Models/Services/NowPlayingEventArgs.cs).
//
// HONEST STUB — documented in README. The C# implementation leans on NPSMLib,
// which wraps SystemMediaTransportControlsInterop (undocumented WinRT
// interop); there is no native COM/WinRT surface a plain C++ app can use to
// enumerate media sessions. Windows' GlobalSystemMediaTransportControls is
// WinRT-only, usable from C++/WinRT — deferred to the UI tranche where the
// C++/WinRT toolchain is set up.
//
// The public surface (events + CurrentTrack shape) is fully defined so the
// player call-sites compile against it unchanged.

#include <lively/events.h>

#include <optional>
#include <string>
#include <vector>

namespace lively::services {

// NowPlayingEventArgs (only the fields the C# class carries).
struct NowPlayingEventArgs {
    std::optional<std::string> album_artist;
    std::optional<std::string> album_title;
    std::optional<int> album_track_count;
    std::optional<std::string> artist;
    std::optional<std::vector<std::string>> genres;
    std::optional<std::string> playback_type;
    std::optional<std::string> subtitle;
    std::optional<std::string> thumbnail;  // base64
    std::optional<std::string> title;
    std::optional<int> track_number;
};

class NpsmNowPlayingService {
public:
    lively::event<std::optional<NowPlayingEventArgs>> now_playing_track_changed;

    std::optional<NowPlayingEventArgs> current_track() const { return current_; }

    // C# Start/Stop subscribe to session changes; the stub raises nothing.
    void start() {}
    void stop() {}

private:
    std::optional<NowPlayingEventArgs> current_;
};

} // namespace lively::services
