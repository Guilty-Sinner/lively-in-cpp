#pragma once
// Port of Lively.Common/Languages.cs — the languages the UI offers, each with
// the display name shown in menus and the BCP-47 code used for lookups.
//
// C# exposes `ReadOnlyCollection<LanguageModel>` over a private array; the port
// returns a const reference to the same ordered table. Order and content are
// compared entry-by-entry against the C# oracle
// (`languages.<i>` lines in tests/goldens/library_csharp.txt).
//
// The display names are the upstream strings verbatim, including the upstream
// quirk "Pусский" (a Latin capital P followed by Cyrillic) — the golden file
// pins it, so "fixing" the spelling here would fail the oracle.

#include <lively/models/library_model.h>

#include <string>
#include <vector>

namespace lively::common {

const std::vector<models::LanguageModel>& supported_languages();

// Best-effort OS UI language, the C++ stand-in for
// `CultureInfo.CurrentUICulture.Name` (LivelyInfoUtil falls back to it when no
// language code is supplied). Returns "" when the OS reports nothing.
std::string ui_language();

} // namespace lively::common
