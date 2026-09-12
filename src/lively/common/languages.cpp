#include <lively/common/languages.h>

#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace lively::common {

const std::vector<models::LanguageModel>& supported_languages() {
    static const std::vector<models::LanguageModel> languages = {
        {"English", "en-US"},
        {"日本語", "ja-JP"},                    // Japanese
        {"中文", "zh-CN"},                      // Chinese (Simplified)
        {"中文 (繁體)", "zh-Hant"},              // Chinese (Traditional)
        {"한국어", "ko-KR"},                    // Korean
        {"Pусский", "ru-RU"},                  // Russian
        {"Українська", "uk-UA"},               // Ukrainian
        {"Español (España)", "es-ES"},         // Spanish
        {"Español (México)", "es-MX"},         // Spanish (Mexico)
        {"Italiano", "it-IT"},                 // Italian
        {"عربى", "ar-AE"},                      // Arabic (United Arab Emirates)
        {"فارسی", "fa-IR"},                     // Persian
        {"עִברִית", "he-IL"},                     // Hebrew
        {"Français", "fr-FR"},                 // French
        {"Deutsch", "de-DE"},                  // German
        {"Polski", "pl-PL"},                   // Polish
        {"Português (Portugal)", "pt-PT"},     // Portuguese (Portugal)
        {"Português (Brasil)", "pt-BR"},       // Portuguese (Brazil)
        {"Filipino", "fil-PH"},                // Filipino
        {"Finnish", "fi-FI"},                  // Finnish
        {"Bahasa Indonesia", "id-ID"},         // Indonesian
        {"Magyar", "hu-HU"},                   // Hungarian
        {"Svenska", "sv-SE"},                  // Swedish
        {"Bahasa Melayu", "ms-MY"},            // Malay
        {"Nederlands", "nl-NL"},               // Dutch
        {"Tiếng Việt", "vi-VN"},               // Vietnamese
        {"Català", "ca-ES"},                   // Catalan
        {"Türkçe", "tr-TR"},                   // Turkish
        {"Српски језик", "sr-Latn"},           // Serbian (Latin)
        {"Српска ћирилица", "sr-Cyrl"},        // Serbian (Cyrillic)
        {"Ελληνικά", "el-GR"},                 // Greek
        {"हिन्दी", "hi-IN"},                     // Hindi
        {"Azərbaycan", "az-Latn"},             // Azerbaijani (Latin)
        {"Čeština", "cs-CZ"},                  // Czech
        {"Български", "bg-BG"},                // Bulgarian
        {"Norwegian Bokmål", "nb-NO"},         // Norwegian
        {"Lietuvių", "lt-LT"},                 // Lithuanian
        {"Afrikaans", "af-ZA"},                // Afrikaans
        {"Dansk", "da-DK"},                    // Danish
        {"беларуская мова", "be-BY"},          // Belarusian
        {"Galego", "gl-ES"},                   // Galician (Spain)
        {"қазақ тілі", "kk-KZ"},               // Kazakh (Kazakhstan)
        {"မြန်မာဘာသာ", "my-MM"},                 // Burmese
        {"slovenčina", "sk-SK"},               // Slovak (Slovakia)
        {"Gaeilge", "ga-IE"},                  // Irish
        {"Română", "ro-RO"},                   // Romanian
    };
    return languages;
}

std::string ui_language() {
#ifdef _WIN32
    // GetUserDefaultLocaleName returns a BCP-47 name like "en-US" — the same
    // shape as CultureInfo.CurrentUICulture.Name.
    wchar_t buffer[LOCALE_NAME_MAX_LENGTH] = {};
    const int length = ::GetUserDefaultLocaleName(buffer, LOCALE_NAME_MAX_LENGTH);
    if (length <= 1) return std::string();
    const int chars = length - 1; // includes the terminator
    std::string narrow;
    narrow.reserve(static_cast<std::size_t>(chars));
    for (int i = 0; i < chars; ++i) {
        const wchar_t c = buffer[i];
        narrow.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    }
    return narrow;
#else
    return std::string();
#endif
}

} // namespace lively::common
