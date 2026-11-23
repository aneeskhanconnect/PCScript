// SHA-256, HMAC-SHA-256 and PBKDF2-HMAC-SHA-256 (self-contained, used for local account passwords).
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace studio {

using Digest = std::array<std::uint8_t, 32>;
Digest sha256(const std::uint8_t* data, size_t len);
Digest hmacSha256(const std::vector<std::uint8_t>& key, const std::uint8_t* data, size_t len);
std::vector<std::uint8_t> pbkdf2Sha256(const std::string& password, const std::vector<std::uint8_t>& salt, int iterations, size_t outLen = 32);
std::string toHex(const std::uint8_t* d, size_t n);
bool fromHex(const std::string& hex, std::vector<std::uint8_t>& out);
bool constantTimeEqual(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b);

} // namespace studio
