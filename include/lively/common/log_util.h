#pragma once
// Port of Lively.Common/Helpers/LogUtil.cs (the parts usable without the UI).
//
// C# PropertyList uses runtime reflection (obj.GetType().GetProperties());
// per prompt.txt that cannot exist in C++, so types opt in by providing a
// `to_property_list()` free function or by deriving from PropertyListable.
// Output format is identical: "Name: value\n" per property (AppendLine).

#include <string>

namespace lively::common {

class PropertyListable {
public:
    virtual ~PropertyListable() = default;
    // Returns "Name: value\nName2: value2\n" (C# StringBuilder.AppendLine order).
    virtual std::string to_property_list() const = 0;
};

class LogUtil {
public:
    // C#: PropertyList(object) — overload resolution replaces reflection.
    static std::string PropertyList(const PropertyListable& obj) {
        try {
            return obj.to_property_list();
        } catch (...) {
            return "Failed to retrive properties of object."; // C# message (sic)
        }
    }

    // C#: GetWin32Error(message, [CallerMemberName], [CallerFilePath],
    // [CallerLineNumber]) — __func__/__FILE__/__LINE__ are the direct mapping.
    // Format: "HRESULT: {err}, {message} at\n{file} ({line})\n{member}"
    static std::string GetWin32Error(const std::string& message,
                                     const char* member_name = "",
                                     const char* file_name = "",
                                     int line_number = 0);

    // ExtractLogFiles (diagnostics zip) is ported with the core phase; it needs
    // the ZipCreate helper (miniz) and is intentionally deferred.
};

} // namespace lively::common

// Convenience macro mirroring the C# caller-info attributes.
#define LIVELY_WIN32_ERROR(msg) \
    ::lively::common::LogUtil::GetWin32Error((msg), __func__, __FILE__, __LINE__)
