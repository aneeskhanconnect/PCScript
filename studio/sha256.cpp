#include "sha256.hpp"
#include <algorithm>
#include <cstring>

namespace studio {
namespace {
const std::uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
    0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
    0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
    0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
    0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

struct Ctx {
    std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::uint8_t buf[64]; size_t n = 0; std::uint64_t total = 0;

    void block(const std::uint8_t* p) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) w[i] = (std::uint32_t(p[i * 4]) << 24) | (std::uint32_t(p[i * 4 + 1]) << 16) | (std::uint32_t(p[i * 4 + 2]) << 8) | p[i * 4 + 3];
        for (int i = 16; i < 64; ++i) {
            std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25), ch = (e & f) ^ (~e & g);
            std::uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22), mj = (a & b) ^ (a & c) ^ (b & c);
            std::uint32_t t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    void update(const std::uint8_t* d, size_t len) {
        total += len;
        while (len) {
            size_t take = std::min(len, 64 - n);
            std::memcpy(buf + n, d, take); n += take; d += take; len -= take;
            if (n == 64) { block(buf); n = 0; }
        }
    }
    Digest finish() {
        std::uint64_t bits = total * 8;
        std::uint8_t pad = 0x80; update(&pad, 1);
        std::uint8_t z = 0;
        while (n != 56) update(&z, 1);
        std::uint8_t lenb[8];
        for (int i = 0; i < 8; ++i) lenb[i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
        update(lenb, 8);
        Digest out;
        for (int i = 0; i < 8; ++i) { out[size_t(i * 4)] = std::uint8_t(h[i] >> 24); out[size_t(i * 4 + 1)] = std::uint8_t(h[i] >> 16); out[size_t(i * 4 + 2)] = std::uint8_t(h[i] >> 8); out[size_t(i * 4 + 3)] = std::uint8_t(h[i]); }
        return out;
    }
};
} // namespace

Digest sha256(const std::uint8_t* data, size_t len) { Ctx c; c.update(data, len); return c.finish(); }

Digest hmacSha256(const std::vector<std::uint8_t>& key, const std::uint8_t* data, size_t len) {
    std::uint8_t k[64] = {0};
    if (key.size() > 64) { Digest d = sha256(key.data(), key.size()); std::memcpy(k, d.data(), 32); }
    else if (!key.empty()) std::memcpy(k, key.data(), key.size());
    std::uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; ++i) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }
    Ctx in; in.update(ipad, 64); in.update(data, len); Digest ih = in.finish();
    Ctx out; out.update(opad, 64); out.update(ih.data(), 32);
    return out.finish();
}

std::vector<std::uint8_t> pbkdf2Sha256(const std::string& password, const std::vector<std::uint8_t>& salt, int iterations, size_t outLen) {
    std::vector<std::uint8_t> key(password.begin(), password.end()), out;
    for (std::uint32_t block = 1; out.size() < outLen; ++block) {
        std::vector<std::uint8_t> s = salt;
        s.push_back(std::uint8_t(block >> 24)); s.push_back(std::uint8_t(block >> 16)); s.push_back(std::uint8_t(block >> 8)); s.push_back(std::uint8_t(block));
        Digest u = hmacSha256(key, s.data(), s.size()), t = u;
        for (int i = 1; i < iterations; ++i) {
            u = hmacSha256(key, u.data(), u.size());
            for (size_t j = 0; j < 32; ++j) t[j] ^= u[j];
        }
        out.insert(out.end(), t.begin(), t.end());
    }
    out.resize(outLen);
    return out;
}

std::string toHex(const std::uint8_t* d, size_t n) {
    static const char* h = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) { s += h[d[i] >> 4]; s += h[d[i] & 15]; }
    return s;
}

bool fromHex(const std::string& hex, std::vector<std::uint8_t>& out) {
    if (hex.size() % 2) return false;
    out.clear();
    auto v = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    for (size_t i = 0; i < hex.size(); i += 2) {
        int a = v(hex[i]), b = v(hex[i + 1]);
        if (a < 0 || b < 0) return false;
        out.push_back(std::uint8_t(a * 16 + b));
    }
    return true;
}

bool constantTimeEqual(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    if (a.size() != b.size()) return false;
    std::uint8_t d = 0;
    for (size_t i = 0; i < a.size(); ++i) d |= a[i] ^ b[i];
    return d == 0;
}

} // namespace studio
