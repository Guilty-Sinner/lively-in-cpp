#pragma once
// Port of Lively.Common.Services/HttpDownloadService.cs.
//
// C# semantics preserved:
//   * Creates the target directory when missing.
//   * Progress reports TRUNCATED MB values (Math.Truncate(bytes/1MB)), not
//     percentages.
//   * Cancellation deletes the partial file; failures delete too and rethrow.
//
// C# async Task → blocking call on the calling thread (call-sites already
// dispatch on background threads), C# CancellationToken → stop_token/predicate.

#include <functional>
#include <string>

namespace lively::services {

class HttpDownloadService {
public:
    // C# IProgress<(double downloaded, double total)> — MB units.
    using ProgressFn = std::function<void(double downloaded_mb, double total_mb)>;
    // C# CancellationToken — polled per chunk; return true to cancel.
    using CancelFn = std::function<bool()>;

    void download_file(const std::string& url, const std::string& file_path,
                       const ProgressFn& progress = {}, const CancelFn& cancelled = {});
};

} // namespace lively::services
