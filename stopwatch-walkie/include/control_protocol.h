#pragma once

#include "protocol.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#if defined(ARDUINO)
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#endif

namespace stopwatch {

class SequenceWindow {
public:
    bool accept(uint64_t sequence)
    {
        if (sequence == 0 || sequence <= last_) {
            return false;
        }
        last_ = sequence;
        return true;
    }
    void reset() { last_ = 0; }
    uint64_t last() const { return last_; }

private:
    uint64_t last_ = 0;
};

#if defined(ARDUINO)

inline std::string base64UrlEncode(const uint8_t* data, size_t length)
{
    size_t required = 0;
    mbedtls_base64_encode(nullptr, 0, &required, data, length);
    std::vector<uint8_t> encoded(required + 1);
    size_t written = 0;
    if (mbedtls_base64_encode(encoded.data(), encoded.size(), &written, data, length) != 0) {
        return {};
    }
    std::string out(reinterpret_cast<char*>(encoded.data()), written);
    for (char& c : out) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    while (!out.empty() && out.back() == '=') out.pop_back();
    return out;
}

inline bool base64UrlDecode(const std::string& input, std::vector<uint8_t>& output)
{
    std::string padded = input;
    for (char& c : padded) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    while ((padded.size() % 4) != 0) padded.push_back('=');
    size_t required = 0;
    mbedtls_base64_decode(nullptr, 0, &required,
                          reinterpret_cast<const uint8_t*>(padded.data()), padded.size());
    output.resize(required);
    size_t written = 0;
    if (mbedtls_base64_decode(output.data(), output.size(), &written,
                              reinterpret_cast<const uint8_t*>(padded.data()), padded.size()) != 0) {
        output.clear();
        return false;
    }
    output.resize(written);
    return true;
}

inline bool hmacSha256(const uint8_t* secret, size_t secret_length,
                       const std::string& input, uint8_t output[32])
{
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return info != nullptr &&
           mbedtls_md_hmac(info, secret, secret_length,
                           reinterpret_cast<const uint8_t*>(input.data()), input.size(), output) == 0;
}

inline bool constantTimeEqual(const std::string& left, const std::string& right)
{
    if (left.size() != right.size()) return false;
    uint8_t difference = 0;
    for (size_t i = 0; i < left.size(); ++i) {
        difference |= static_cast<uint8_t>(left[i] ^ right[i]);
    }
    return difference == 0;
}

class ControlCrypto {
public:
    bool configure(const std::string& encoded_secret)
    {
        std::vector<uint8_t> decoded;
        if (!base64UrlDecode(encoded_secret, decoded) || decoded.size() != kControlSecretBytes) {
            configured_ = false;
            return false;
        }
        std::memcpy(secret_, decoded.data(), sizeof(secret_));
        configured_ = true;
        return true;
    }

    bool configured() const { return configured_; }

    std::string proof(const std::string& role, const std::string& device_id,
                      const std::string& device_nonce, const std::string& bridge_nonce,
                      const std::string& session_id) const
    {
        return mac(authMacInput(role, device_id, device_nonce, bridge_nonce, session_id));
    }

    std::string envelopeMac(const std::string& direction, const std::string& session_id,
                            uint64_t sequence, const std::string& body_base64url) const
    {
        return mac(envelopeMacInput(direction, session_id, sequence, body_base64url));
    }

private:
    std::string mac(const std::string& input) const
    {
        uint8_t digest[32]{};
        if (!configured_ || !hmacSha256(secret_, sizeof(secret_), input, digest)) return {};
        return base64UrlEncode(digest, sizeof(digest));
    }

    uint8_t secret_[kControlSecretBytes]{};
    bool configured_ = false;
};

#endif

}  // namespace stopwatch
