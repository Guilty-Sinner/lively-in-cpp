#include <lively/common/base64.h>

#include <array>
#include <cstdint>

namespace lively::common {

namespace {

constexpr char kAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int decode_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

} // namespace

std::string base64_encode(const std::vector<unsigned char>& data) {
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    std::size_t i = 0;
    for (; i + 3 <= data.size(); i += 3) {
        const std::uint32_t chunk = (static_cast<std::uint32_t>(data[i]) << 16) |
                                    (static_cast<std::uint32_t>(data[i + 1]) << 8) |
                                    static_cast<std::uint32_t>(data[i + 2]);
        out.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 6) & 0x3F]);
        out.push_back(kAlphabet[chunk & 0x3F]);
    }
    if (i + 1 == data.size()) {
        const std::uint32_t chunk = static_cast<std::uint32_t>(data[i]) << 16;
        out.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (i + 2 == data.size()) {
        const std::uint32_t chunk = (static_cast<std::uint32_t>(data[i]) << 16) |
                                    (static_cast<std::uint32_t>(data[i + 1]) << 8);
        out.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

std::optional<std::vector<unsigned char>> base64_decode(const std::string& text) {
    // .NET ignores nothing: any character outside the alphabet (including
    // whitespace) makes FromBase64String throw.
    if (text.size() % 4 != 0) return std::nullopt;
    std::vector<unsigned char> out;
    out.reserve(text.size() / 4 * 3);

    for (std::size_t i = 0; i < text.size(); i += 4) {
        int values[4];
        int padding = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = text[i + static_cast<std::size_t>(k)];
            if (c == '=') {
                // Padding is only legal as the last one or two characters.
                if (k < 2 || i + 4 != text.size()) return std::nullopt;
                values[k] = 0;
                ++padding;
            } else {
                if (padding > 0) return std::nullopt;
                values[k] = decode_char(c);
                if (values[k] < 0) return std::nullopt;
            }
        }
        const std::uint32_t chunk = (static_cast<std::uint32_t>(values[0]) << 18) |
                                    (static_cast<std::uint32_t>(values[1]) << 12) |
                                    (static_cast<std::uint32_t>(values[2]) << 6) |
                                    static_cast<std::uint32_t>(values[3]);
        out.push_back(static_cast<unsigned char>((chunk >> 16) & 0xFF));
        if (padding < 2) out.push_back(static_cast<unsigned char>((chunk >> 8) & 0xFF));
        if (padding < 1) out.push_back(static_cast<unsigned char>(chunk & 0xFF));
    }
    return out;
}

} // namespace lively::common
