#include "lively/common/log_util.h"

#include <windows.h>

#include <sstream>

namespace lively::common {

std::string LogUtil::GetWin32Error(const std::string& message,
                                   const char* member_name,
                                   const char* file_name,
                                   int line_number) {
    const DWORD err = ::GetLastError();
    std::ostringstream out;
    out << "HRESULT: " << static_cast<int>(err) << ", " << message << " at\n"
        << (file_name ? file_name : "") << " (" << line_number << ")\n"
        << (member_name ? member_name : "");
    return out.str();
}

} // namespace lively::common
