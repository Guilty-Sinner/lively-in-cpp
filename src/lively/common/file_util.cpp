#include <lively/common/file_util.h>

#include <lively/common/path_util.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#endif

namespace lively::common {

namespace fs = std::filesystem;

namespace {

bool is_invalid_filename_char(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u < 32) return true; // chars 0x00-0x1F
    switch (c) {
    case '"':
    case '<':
    case '>':
    case '|':
    case ':':
    case '*':
    case '?':
    case '\\':
    case '/':
        return true;
    default:
        return false;
    }
}

std::string format_index(const std::string& pattern, std::uint64_t index) {
    const std::string token = "{0}";
    const std::size_t at = pattern.find(token);
    if (at == std::string::npos) {
        // C# throws ArgumentException("The pattern must include an index place-holder").
        throw std::invalid_argument("The pattern must include an index place-holder");
    }
    return pattern.substr(0, at) + std::to_string(index) + pattern.substr(at + token.size());
}

std::string get_next_filename(const std::string& pattern) {
    const std::string first = format_index(pattern, 1);
    if (first == pattern) throw std::invalid_argument("pattern has no index placeholder");
    if (!fs::exists(first)) return first;

    std::uint64_t min = 1;
    std::uint64_t max = 2;
    while (fs::exists(format_index(pattern, max))) {
        min = max;
        max *= 2;
    }
    while (max != min + 1) {
        const std::uint64_t pivot = (max + min) / 2;
        if (fs::exists(format_index(pattern, pivot))) {
            min = pivot;
        } else {
            max = pivot;
        }
    }
    return format_index(pattern, max);
}

bool glob_match(const std::string& pattern, const std::string& name) {
    std::size_t p = 0, n = 0, star = std::string::npos, mark = 0;
    while (n < name.size()) {
        if (p < pattern.size() && (pattern[p] == '?' ||
                                   std::tolower(static_cast<unsigned char>(pattern[p])) ==
                                       std::tolower(static_cast<unsigned char>(name[n])))) {
            ++p;
            ++n;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            mark = n;
        } else if (star != std::string::npos) {
            p = star + 1;
            n = ++mark;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

// C# "{0:n<dp>}" with the invariant culture: fixed-point, group separators
// every three digits, rounded half away from zero ("n" formatting is
// MidpointRounding.AwayFromZero, unlike Math.Round's default).
std::string format_n(long double value, int decimals) {
    long double scale = 1.0L;
    long long divisor = 1;
    for (int i = 0; i < decimals; ++i) {
        scale *= 10.0L;
        divisor *= 10;
    }

    const bool negative = value < 0;
    const long long units = static_cast<long long>(std::floor(std::fabs(value) * scale + 0.5L));
    const long long whole_part = units / divisor;
    const long long fraction = units % divisor;

    std::string whole = std::to_string(whole_part);
    std::string grouped;
    for (std::size_t i = 0; i < whole.size(); ++i) {
        if (i > 0 && (whole.size() - i) % 3 == 0) grouped.push_back(',');
        grouped.push_back(whole[i]);
    }

    std::string out = negative ? "-" : "";
    out += grouped;
    if (decimals > 0) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%0*lld", decimals, fraction);
        out.push_back('.');
        out += buffer;
    }
    return out;
}

// Math.Round(decimal, digits) — banker's rounding, used only by the ">= 1000"
// guard below (the final formatting rounds half away from zero).
long double round_to_even(long double value, int digits) {
    long double scale = 1.0L;
    for (int i = 0; i < digits; ++i) scale *= 10.0L;
    const long double scaled = value * scale;
    const long double floor_v = std::floor(scaled);
    const long double diff = scaled - floor_v;
    long double rounded;
    if (diff > 0.5L) {
        rounded = floor_v + 1;
    } else if (diff < 0.5L) {
        rounded = floor_v;
    } else {
        rounded = (std::fmod(floor_v, 2.0L) == 0.0L) ? floor_v : floor_v + 1;
    }
    return rounded / scale;
}

} // namespace

void open_folder(const std::string& path) {
#ifdef _WIN32
    std::error_code ec;
    const bool is_file = fs::is_regular_file(path, ec);
    const bool is_dir = fs::is_directory(path, ec);
    if (!is_file && !is_dir) return; // C# throws FileNotFoundException, caller catches

    std::wstring command_line = L"explorer.exe ";
    const std::wstring wide = fs::path(path).wstring();
    command_line += is_file ? (L"/select, \"" + wide + L"\"") : (L"\"" + wide + L"\"");

    std::vector<wchar_t> buffer(command_line.begin(), command_line.end());
    buffer.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (::CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si,
                         &pi)) {
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);
    }
#else
    (void)path;
#endif
}

std::string get_safe_filename(const std::string& filename) {
    // string.Join("_", filename.Split(GetInvalidFileNameChars())): split keeps
    // the empty segments, so runs of invalid characters become runs of '_'.
    std::vector<std::string> segments;
    std::string current;
    for (const char c : filename) {
        if (is_invalid_filename_char(c)) {
            segments.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    segments.push_back(current);

    std::string result;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (i > 0) result += '_';
        result += segments[i];
    }
    return result;
}

std::string next_available_filename(const std::string& path) {
    if (!fs::exists(path)) return path;

    const std::string number_pattern = " ({0})";
    if (path::has_extension(path)) {
        const std::string ext = path::get_extension(path);
        const std::size_t at = path.size() - ext.size();
        return get_next_filename(path.substr(0, at) + number_pattern + path.substr(at));
    }
    return get_next_filename(path + number_pattern);
}

std::string get_checksum_sha256(const std::string& file_path) {
#ifdef _WIN32
    std::ifstream in(file_path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open file: " + file_path);

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (::BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
        throw std::runtime_error("BCryptOpenAlgorithmProvider failed");
    }

    DWORD object_size = 0, hash_size = 0, produced = 0;
    std::vector<unsigned char> object;
    std::vector<unsigned char> hash;
    bool ok = ::BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                                  reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size),
                                  &produced, 0) >= 0 &&
              ::BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                                  reinterpret_cast<PUCHAR>(&hash_size), sizeof(hash_size), &produced,
                                  0) >= 0;
    if (ok) {
        object.resize(object_size);
        hash.resize(hash_size);
        BCRYPT_HASH_HANDLE hash_handle = nullptr;
        ok = ::BCryptCreateHash(algorithm, &hash_handle, object.data(), object_size, nullptr, 0, 0) >=
             0;
        if (ok) {
            char buffer[64 * 1024];
            while (in) {
                in.read(buffer, sizeof(buffer));
                const std::streamsize got = in.gcount();
                if (got > 0 && ::BCryptHashData(hash_handle,
                                                reinterpret_cast<PUCHAR>(buffer),
                                                static_cast<ULONG>(got), 0) < 0) {
                    ok = false;
                    break;
                }
            }
            if (ok) {
                ok = ::BCryptFinishHash(hash_handle, hash.data(), hash_size, 0) >= 0;
            }
            ::BCryptDestroyHash(hash_handle);
        }
    }
    ::BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) throw std::runtime_error("SHA-256 computation failed");

    static const char* hex = "0123456789abcdef";
    std::string result;
    result.reserve(hash.size() * 2);
    for (const unsigned char byte : hash) {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 0x0F]);
    }
    return result;
#else
    (void)file_path;
    throw std::runtime_error("SHA-256 is only implemented on Windows");
#endif
}

void empty_directory(const std::string& directory) {
    std::error_code ec;
    if (!fs::exists(directory, ec)) {
        // C# EnumerateFiles on a missing directory throws DirectoryNotFoundException.
        throw std::runtime_error("directory not found: " + directory);
    }
    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        fs::remove_all(entry.path(), ec); // C# deletes files then directories; the result is the same
    }
}

std::vector<std::string> get_files(const std::string& path, const std::string& search_pattern,
                                   bool recursive) {
    std::vector<std::string> patterns;
    std::size_t start = 0;
    while (true) {
        const std::size_t bar = search_pattern.find('|', start);
        patterns.push_back(search_pattern.substr(start, bar == std::string::npos ? bar : bar - start));
        if (bar == std::string::npos) break;
        start = bar + 1;
    }

    std::error_code ec;
    if (!fs::exists(path, ec)) {
        // Directory.GetFiles throws DirectoryNotFoundException for a missing path.
        throw std::runtime_error("directory not found: " + path);
    }

    std::vector<std::string> files;
    if (recursive) {
        for (const auto& entry : fs::recursive_directory_iterator(path, ec)) {
            if (entry.is_regular_file(ec)) files.push_back(entry.path().string());
        }
    } else {
        for (const auto& entry : fs::directory_iterator(path, ec)) {
            if (entry.is_regular_file(ec)) files.push_back(entry.path().string());
        }
    }

    std::vector<std::string> matched;
    for (const auto& pattern : patterns) {
        for (const auto& file : files) {
            if (glob_match(pattern, fs::path(file).filename().string())) matched.push_back(file);
        }
    }
    std::sort(matched.begin(), matched.end());
    return matched;
}

bool is_file_greater(const std::string& file_path, std::int64_t bytes) {
    std::error_code ec;
    const auto size = fs::file_size(file_path, ec);
    if (ec) return false; // C# catch { }
    return static_cast<std::int64_t>(size) > bytes;
}

void directory_copy(const std::string& source_dir, const std::string& dest_dir, bool copy_sub_dirs) {
    std::error_code ec;
    if (!fs::exists(source_dir, ec)) {
        throw std::runtime_error("Source directory does not exist or could not be found: " +
                                 source_dir);
    }
    fs::create_directories(dest_dir, ec);
    for (const auto& entry : fs::directory_iterator(source_dir, ec)) {
        if (entry.is_regular_file(ec)) {
            // C# file.CopyTo(path, overwrite: false) — an existing destination throws.
            fs::copy_file(entry.path(), fs::path(dest_dir) / entry.path().filename());
        }
    }
    if (copy_sub_dirs) {
        for (const auto& entry : fs::directory_iterator(source_dir, ec)) {
            if (entry.is_directory(ec)) {
                directory_copy(entry.path().string(),
                               (fs::path(dest_dir) / entry.path().filename()).string(), true);
            }
        }
    }
}

std::int64_t get_directory_size(const std::string& path) {
    std::int64_t total = 0;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(path, ec)) {
        if (entry.is_regular_file(ec)) {
            const auto size = fs::file_size(entry.path(), ec);
            if (!ec) total += static_cast<std::int64_t>(size);
        }
    }
    return total;
}

std::string size_suffix(std::int64_t value, int decimal_places) {
    if (decimal_places < 0) throw std::out_of_range("decimalPlaces");
    if (value < 0) {
        if (value == std::numeric_limits<std::int64_t>::min()) {
            return "-" + size_suffix(std::numeric_limits<std::int64_t>::max(), decimal_places);
        }
        return "-" + size_suffix(-value, decimal_places);
    }
    if (value == 0) return format_n(0.0L, decimal_places) + " bytes";

    static const char* suffixes[] = {"bytes", "KB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB"};

    // C# Math.Log(value, 1024) computed in double, then truncated by the cast.
    int mag = static_cast<int>(std::log(static_cast<double>(value)) / std::log(1024.0));
    if (mag < 0) mag = 0;

    // (decimal)value / (1L << (mag * 10)) — division by a power of two is exact.
    // mag >= 7 is unreachable for int64 (max 9.22e18 < 1024^7), so the shift is
    // always defined; it is clamped anyway rather than trusting the bound.
    const int shift = std::min(mag, 6) * 10;
    long double adjusted =
        static_cast<long double>(value) / static_cast<long double>(std::int64_t{1} << shift);

    // "make adjustment when the value is large enough that it would round up to
    // 1000 or more" — Math.Round(decimal, dp), i.e. banker's rounding.
    if (round_to_even(adjusted, decimal_places) >= 1000.0L) {
        ++mag;
        adjusted /= 1024.0L;
    }
    if (mag > 8) mag = 8;
    return format_n(adjusted, decimal_places) + " " + suffixes[mag];
}

bool try_delete_directory(const std::string& folder_path, int initial_delay_ms, int retry_delay_ms) {
    std::error_code ec;
    if (!fs::exists(folder_path, ec)) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(initial_delay_ms));
    fs::remove_all(folder_path, ec);
    if (!ec && !fs::exists(folder_path)) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms));
    fs::remove_all(folder_path, ec);
    return !fs::exists(folder_path);
}

} // namespace lively::common
