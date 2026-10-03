#pragma once
#include <string>

namespace wallet {

// SHA-256 (FIPS 180-4) and PBKDF2-HMAC-SHA256 (RFC 8018), implemented from the public
// specifications and checked against published test vectors in tests/.
// Educational: a vetted library (Argon2/bcrypt/libsodium) is the right choice in production.
std::string sha256Hex(const std::string& data);
std::string pbkdf2HmacSha256Hex(const std::string& password, const std::string& salt, int iterations);

class PasswordHasher {
public:
    virtual ~PasswordHasher() = default;
    virtual std::string newSalt() const = 0;
    virtual std::string hash(const std::string& password, const std::string& salt) const = 0;
    bool verify(const std::string& password, const std::string& salt, const std::string& expected) const;
};

class Pbkdf2PasswordHasher : public PasswordHasher {
public:
    explicit Pbkdf2PasswordHasher(int iterations) : iterations_(iterations) {}
    std::string newSalt() const override;
    std::string hash(const std::string& password, const std::string& salt) const override;
private:
    int iterations_;
};

} // namespace wallet
