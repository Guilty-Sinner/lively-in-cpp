#pragma once
// Port of Lively.Common/Helpers/LivelyInfoUtil.cs — resolves the localized
// livelyinfo.json content for a language.
//
// Lookup order (matched exactly, including case sensitivity):
//   1. exact key, e.g. "zh-CN"
//   2. base language, e.g. "zh" — the part before the first '-'
//   3. otherwise null
// Every failure (missing file, malformed JSON, no Languages object, no match)
// returns nullopt rather than throwing, mirroring the C# try/catch.
//
// The C# parameterless-override behaviour (`CultureInfo.CurrentUICulture.Name`
// when no code is given) becomes lively::common::ui_language().

#include <lively/models/lively_info.h>

#include <optional>
#include <string>

namespace lively::common {

// `language_code` empty → the OS UI language (ui_language()).
std::optional<models::LivelyInfoModel> get_localized_info(const std::string& loc_path,
                                                         const std::string& language_code = "");

} // namespace lively::common
