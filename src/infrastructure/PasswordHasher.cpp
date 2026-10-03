#include "infrastructure/PasswordHasher.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace wallet {

namespace {
using Bytes = std::vector<std::uint8_t>;
using Digest = std::array<std::uint8_t, 32>;

constexpr std::uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

Digest sha256(const std::uint8_t* data, std::size_t len) {
    std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    Bytes msg(data, data + len);
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    std::uint64_t bits = static_cast<std::uint64_t>(len) * 8;
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<std::uint8_t>(bits >> (i * 8)));

    for (std::size_t off = 0; off < msg.size(); off += 64) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (std::uint32_t(msg[off + 4 * i]) << 24) | (std::uint32_t(msg[off + 4 * i + 1]) << 16) |
                   (std::uint32_t(msg[off + 4 * i + 2]) << 8) | std::uint32_t(msg[off + 4 * i + 3]);
        for (int i = 16; i < 64; ++i) {
            std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            std::uint32_t ch = (e & f) ^ (~e & g);
            std::uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            std::uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    Digest out{};
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 4; ++j) out[i * 4 + j] = static_cast<std::uint8_t>(h[i] >> (24 - 8 * j));
    return out;
}

Digest hmac(const Bytes& key, const Bytes& message) {
    Bytes k = key;
    if (k.size() > 64) { Digest d = sha256(k.data(), k.size()); k.assign(d.begin(), d.end()); }
    k.resize(64, 0);
    Bytes inner(64), outer(64);
    for (int i = 0; i < 64; ++i) { inner[i] = k[i] ^ 0x36; outer[i] = k[i] ^ 0x5c; }
    inner.insert(inner.end(), message.begin(), message.end());
    Digest ih = sha256(inner.data(), inner.size());
    outer.insert(outer.end(), ih.begin(), ih.end());
    return sha256(outer.data(), outer.size());
}

std::string toHex(const std::uint8_t* d, std::size_t n) {
    static const char* hex = "0123456789abcdef";
    std::string s;
    for (std::size_t i = 0; i < n; ++i) { s += hex[d[i] >> 4]; s += hex[d[i] & 15]; }
    return s;
}
Bytes toBytes(const std::string& s) { return Bytes(s.begin(), s.end()); }
}  // namespace

std::string sha256Hex(const std::string& data) {
    Digest d = sha256(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
    return toHex(d.data(), d.size());
}

// PBKDF2 with a 32-byte derived key = exactly one block (index 1).
std::string pbkdf2HmacSha256Hex(const std::string& password, const std::string& salt, int iterations) {
    Bytes key = toBytes(password);
    Bytes first = toBytes(salt);
    first.insert(first.end(), {0, 0, 0, 1});
    Digest u = hmac(key, first);
    Digest t = u;
    for (int i = 1; i < iterations; ++i) {
        u = hmac(key, Bytes(u.begin(), u.end()));
        for (std::size_t j = 0; j < t.size(); ++j) t[j] ^= u[j];
    }
    return toHex(t.data(), t.size());
}

bool PasswordHasher::verify(const std::string& password, const std::string& salt, const std::string& expected) const {
    std::string actual = hash(password, salt);
    if (actual.size() != expected.size()) return false;
    unsigned diff = 0;  // constant-time comparison
    for (std::size_t i = 0; i < actual.size(); ++i) diff |= static_cast<unsigned>(actual[i] ^ expected[i]);
    return diff == 0;
}

std::string Pbkdf2PasswordHasher::newSalt() const {
    std::random_device rd;
    std::array<std::uint8_t, 16> raw{};
    for (auto& b : raw) b = static_cast<std::uint8_t>(rd());
    return toHex(raw.data(), raw.size());
}

std::string Pbkdf2PasswordHasher::hash(const std::string& password, const std::string& salt) const {
    return pbkdf2HmacSha256Hex(password, salt, iterations_);
}

} // namespace wallet
