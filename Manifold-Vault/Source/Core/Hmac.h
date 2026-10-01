#pragma once

#include <cstddef>
#include <cstdint>

namespace Bunker
{
    enum class HashAlgorithm : std::uint8_t
    {
        Sha1 = 0,
        Sha256,
        Sha512,
    };

    inline constexpr std::size_t kMaxDigestBytes = 64;

    std::size_t DigestBytes(HashAlgorithm algorithm);

    const char* AlgorithmName(HashAlgorithm algorithm);

    bool ParseAlgorithm(const char* text, HashAlgorithm& out);

    bool Hmac(HashAlgorithm algorithm,
              const unsigned char* key, std::size_t keyLen,
              const unsigned char* message, std::size_t messageLen,
              unsigned char (&out)[kMaxDigestBytes]);
}
