#pragma once
// Standard base64 with padding, matching .NET's Convert.ToBase64String /
// FromBase64String. Newtonsoft serializes a C# `byte[]` as such a string, which
// is how EncryptUtil puts a DPAPI blob on disk (Tokens.dat).

#include <optional>
#include <string>
#include <vector>

namespace lively::common {

std::string base64_encode(const std::vector<unsigned char>& data);

// Returns nullopt for input that is not valid base64 of the right shape.
std::optional<std::vector<unsigned char>> base64_decode(const std::string& text);

} // namespace lively::common
