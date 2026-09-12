#pragma once
// Port of Lively.Common/Exceptions/* — the typed exceptions raised by the
// desktop-core client when the server reports a WallpaperErrorResponse.
// All carry the server's ErrorMsg, exactly like the C# base(message) ctors.

#include <stdexcept>
#include <string>

namespace lively::common {

class WorkerWException : public std::runtime_error {
public:
    explicit WorkerWException(const std::string& message) : std::runtime_error(message) {}
};

class WallpaperNotFoundException : public std::runtime_error {
public:
    explicit WallpaperNotFoundException(const std::string& message) : std::runtime_error(message) {}
};

class WallpaperNotAllowedException : public std::runtime_error {
public:
    explicit WallpaperNotAllowedException(const std::string& message) : std::runtime_error(message) {}
};

class WallpaperPluginNotFoundException : public std::runtime_error {
public:
    explicit WallpaperPluginNotFoundException(const std::string& message) : std::runtime_error(message) {}
};

class WallpaperPluginException : public std::runtime_error {
public:
    explicit WallpaperPluginException(const std::string& message) : std::runtime_error(message) {}
};

class WallpaperPluginMediaCodecException : public std::runtime_error {
public:
    explicit WallpaperPluginMediaCodecException(const std::string& message) : std::runtime_error(message) {}
};

class ScreenNotFoundException : public std::runtime_error {
public:
    explicit ScreenNotFoundException(const std::string& message) : std::runtime_error(message) {}
};

class WallpaperWebView2NotFoundException : public std::runtime_error {
public:
    explicit WallpaperWebView2NotFoundException(const std::string& message) : std::runtime_error(message) {}
};

class WallpaperFileException : public std::runtime_error {
public:
    explicit WallpaperFileException(const std::string& message) : std::runtime_error(message) {}
};

} // namespace lively::common
